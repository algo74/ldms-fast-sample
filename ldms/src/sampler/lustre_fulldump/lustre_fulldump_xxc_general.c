/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <coll/rbt.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "comp_id_helper.h"
#include "sampler_base.h"
#include "config.h"
#include "jobid_helper.h"
// #include "ldms.h"
// #include "ldmsd.h"
#include "lustre_fulldump.h"
#include "lustre_fulldump_general.h"
#include "lustre_fulldump_xxc_general.h"

// #define _GNU_SOURCE


#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


static int string_comparator(void *a, const void *b)
{
  return strcmp((char *)a, (char *)b);
}


int server_extra_config(fulldump_sub_ctxt_p self, char *source_category)
{
  log_fn(LDMSD_LDEBUG, SAMP " %s config() called\n", __func__);
  struct xxc_extra *extra = malloc(sizeof(struct xxc_extra));
  if (extra == NULL) {
    log_fn(LDMSD_LERROR, SAMP " %s: out of memory\n", __func__);
    return ENOMEM;
  }
  extra->source_category = source_category;
  rbt_init(&extra->source_tree, string_comparator);
  self->extra = extra;
  log_fn(LDMSD_LDEBUG, SAMP " : exiting normally\n", __func__);
  return 0;
}


static ldms_set_t _set_create(fulldump_sub_ctxt_p self, struct server_data *server)
{
  ldms_set_t set;
  int index;
  char instance_name[LDMS_PRODUCER_NAME_MAX + 64];

  log_fn(LDMSD_LDEBUG, SAMP ": %s()\n", __func__);
  struct xxc_extra *extra = self->extra;
  char *category = extra->source_category;
  snprintf(instance_name, sizeof(instance_name), "%s/%s/%s/%s/%s",
           self->sampl_ctxt_p->producer_name, SAMP, server->fs_name, server->server_id, category);
  set = fulldump_general_create_set(log_fn, self->sampl_ctxt_p->producer_name, instance_name, &self->sampl_ctxt_p->auth, &self->cid, self->schema);
  if (!set) {
    return NULL;
  }
  index = ldms_metric_by_name(set, "fs_name");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, SAMP " %s: unable to find field \"fs_name\" in schema %s - field not set\n",
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_array_set_str(set, index, server->fs_name);
  }
  index = ldms_metric_by_name(set, "server_idx");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, SAMP " %s: unable to find field \"server_idx\" in schema %s - field not set\n",
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_set_u64(set, index, server->server_idx);
  }
  return set;
}


static struct server_data *_server_create(const char *server_dir, const char *basedir, fulldump_sub_ctxt_p self)
{
  struct server_data *server;
  char path_tmp[PATH_MAX];
  char *state;

  log_fn(LDMSD_LDEBUG, SAMP " %s() %s from %s\n",
         __func__, server_dir, basedir);
  server = calloc(1, sizeof(*server));
  if (server == NULL)
    goto out1;

  server->name = strdup(server_dir);
  if (server->name == NULL)
    goto out2;

  snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, server_dir);
  server->dir_path = strdup(path_tmp);
  if (server->dir_path == NULL)
    goto out3;

  struct xxc_extra *extra = self->extra;
  snprintf(path_tmp, PATH_MAX, "%s/%s", server->dir_path, extra->source_category);
  server->file_path = strdup(path_tmp);
  if (server->file_path == NULL)
    goto out4;

  int rc = fulldump_split_server_name(server->name, &server->fs_name, &server->server_idx, &server->server_id);
  if (rc != 3) {
    log_fn(LDMSD_LWARNING, SAMP "%s: unable to parse server name \"%s\"; return code=%d\n",
           __func__, server->name, rc);
    goto out5;
  }

  server->metric_set = _set_create(self, server);
  if (server->metric_set == NULL)
    goto out6;
  rbn_init(&server->tree_node, server->name);
  return server;

out6:
  free(server->fs_name);
out5:
  free(server->file_path);
out4:
  free(server->dir_path);
out3:
  free(server->name);
out2:
  free(server);
out1:
  return NULL;
}


static void _server_destroy(struct server_data *server)
{
  log_fn(LDMSD_LDEBUG, SAMP " %s() %s\n", __func__, server->name);
  fulldump_general_destroy_set(server->metric_set);
  free(server->fs_name);
  free(server->file_path);
  free(server->dir_path);
  free(server->name);
  free(server);
}


void servers_destroy(struct rbt *source_tree)
{
  struct rbn *rbn;
  struct server_data *server;

  while (!rbt_empty(source_tree)) {
    rbn = rbt_min(source_tree);
    server = container_of(rbn, struct server_data, tree_node);
    rbt_del(source_tree, rbn);
    _server_destroy(server);
  }
}


/** List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 */
int servers_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self, const char *path)
// FIXME: Make sure the error handling is correct
{
  static int dir_once_log = 0;
  struct dirent *dirent;
  DIR *dir;
  struct rbt new_tree;
  int err = 0;

  rbt_init(&new_tree, string_comparator);

  /* Make sure we have data objects in the new_tree for
     each currently existing directory.  We can find the objects
     cached in the old tree (in which case we move them
     to new_tree), or they can be newly allocated
     here. */
  dir = opendir(path);
  if (dir == NULL) {
    if (!dir_once_log) {
      log_fn(LDMSD_LDEBUG, SAMP "%s: unable to open dir %s\n",
             __func__, path);
      dir_once_log = 1;
    }
    return 0;
  }
  dir_once_log = 0;
  while ((dirent = readdir(dir)) != NULL) {
    struct rbn *rbn;
    struct server_data *server;

    if (dirent->d_type != DT_DIR ||
        strcmp(dirent->d_name, ".") == 0 ||
        strcmp(dirent->d_name, "..") == 0)
      continue;
    rbn = rbt_find(source_tree, dirent->d_name);
    errno = 0;
    if (rbn) {
      server = container_of(rbn, struct server_data, tree_node);
      rbt_del(source_tree, &server->tree_node);
    } else {
      server = _server_create(dirent->d_name, path, self);
    }
    if (server == NULL) {
      err = errno;
      continue;
    }
    rbt_ins(&new_tree, &server->tree_node);
  }
  closedir(dir);

  /* destroy any items remaining in the old source_tree since we
     did not see their associated directories this time around */
  servers_destroy(source_tree);

  /* copy the new_tree into place over the global source_tree */
  memcpy(source_tree, &new_tree, sizeof(struct rbt));

  return err;
}


void servers_sample(struct xxc_extra *xxc_extra, int (*single_sample)(const char *, ldms_set_t))
{
  struct rbn *rbn;
  struct server_data *server;

  /* walk tree of known locations */
  struct rbt *source_tree = &xxc_extra->source_tree;
  RBT_FOREACH(rbn, source_tree)
  {
    server = container_of(rbn, struct server_data, tree_node);
    // FIXME: Make sure the error handling is correct
    single_sample(server->file_path, server->metric_set);
  }
}


void server_general_term(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, SAMP " %s() called\n", __func__);
  struct xxc_extra *extra = self->extra;
  servers_destroy(&extra->source_tree);
  fulldump_general_schema_fini(self);
}