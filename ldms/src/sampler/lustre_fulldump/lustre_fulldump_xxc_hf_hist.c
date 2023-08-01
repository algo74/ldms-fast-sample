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
#include "lustre_fulldump_xxc_hf_hist.h"

// #define _GNU_SOURCE


#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


/* Convenient functions to convert timeval <--> u64 */
static inline struct timeval u64_to_tv(uint64_t x)
{
  struct timeval tv;
  tv.tv_sec = x >> 32;
  tv.tv_usec = x & 0xFFFFFFFF;
  return tv;
}


static int string_comparator(void *a, const void *b)
{
  return strcmp((char *)a, (char *)b);
}


static void _hf_timer_cb(tsampler_timer_t timer)
{
  struct rbn *rbn;
  struct source_data *server;
  struct xxc_extra *extra = timer->ctxt;
  struct hf_data_hanldler *handler = extra->hf_data_handler;
  pthread_mutex_lock(&extra->mutex);

  /* walk tree of known locations */
  struct rbt *source_tree = &extra->source_tree;
  RBT_FOREACH(rbn, source_tree)
  {
    server = container_of(rbn, struct source_data, tree_node);
    handler->on_timer(server);
  }
  pthread_mutex_unlock(&extra->mutex);
}


int hf_hist_extra_config(fulldump_sub_ctxt_p self, char *source_category, struct hf_data_hanldler *handler)
{
  log_fn(LDMSD_LDEBUG, "%s %s config() called\n", SAMP, __func__);
  struct xxc_extra *extra = malloc(sizeof(struct xxc_extra));
  if (extra == NULL) {
    log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
    return ENOMEM;
  }
  pthread_mutex_init(&extra->mutex, NULL);
  extra->source_category = source_category;
  rbt_init(&extra->source_tree, string_comparator);
  // extras specific to adding hf sampling (hist sampling data is in the sub-context)
  extra->hf_data_handler = handler;
  extra->hf_schema = NULL;
  extra->hf_cid = self->cid;
  extra->hf_timer.interval = u64_to_tv(100000); // TODO: make this configurable
  extra->hf_timer.cb = _hf_timer_cb;
  extra->hf_timer.ctxt = extra;
  pthread_mutex_lock(&extra->mutex);
  int rc = tsampler_timer_add(&extra->hf_timer);
  if (rc) {
    log_fn(LDMSD_LERROR, "%s %s: tsampler_timer_add() failed with rc=%d\n", SAMP, __func__, rc);
    pthread_mutex_unlock(&extra->mutex);
    free(extra);
    return rc;
  }
  self->extra = extra;
  pthread_mutex_unlock(&extra->mutex);
  log_fn(LDMSD_LDEBUG, "%s : exiting normally\n", SAMP, __func__);
  return 0;
}


int hf_schema_init(fulldump_sub_ctxt_p self, char *schema_name, struct ldms_metric_template_s *schema_template, int *schema_ids)
{
  // TODO: what to do with the metric ids?

  ldms_schema_t sch;
  int rc;
  log_fn(LDMSD_LDEBUG, "%s: %s()\n", SAMP, __func__);
  struct xxc_extra *extra = self->extra;

  // Create the schema
  sch = ldms_schema_new(schema_name);
  if (sch == NULL) {
    log_fn(LDMSD_LERROR, "%s: %s schema new failed"
           " (out of memory)\n", SAMP,
           __func__);
    goto err2;
  }
  const char *field;
  field = "component id";
  rc = comp_id_helper_schema_add(sch, &extra->hf_cid);
  if (rc) {
    rc = -rc;
    goto err3;
  }
  field = "job data";
  rc = jobid_helper_schema_add(sch);
  if (rc < 0) {
    goto err3;
  }
  field = "metric record";
  rc = ldms_schema_metric_add_template(sch, schema_template, schema_ids);
  if (rc < 0) {
    goto err3;
  }
  extra->hf_schema = sch;

  return 0;

err3:
  log_fn(LDMSD_LERROR, "%s: %s schema creation failed to add %s. (%s)\n", SAMP,
         __func__, field, STRERROR(-rc));
  ldms_schema_delete(sch);
err2:
err1:
  return -1;
}


static ldms_set_t _hist_set_create(fulldump_sub_ctxt_p self, struct source_data *server)
{
  ldms_set_t set;
  int index;
  char instance_name[LDMS_PRODUCER_NAME_MAX + 64];

  log_fn(LDMSD_LDEBUG, "%s: %s()\n", SAMP, __func__);
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
    log_fn(LDMSD_LWARNING, "%s %s: unable to find field \"fs_name\" in schema %s - field not set\n", SAMP,
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_array_set_str(set, index, server->fs_name);
  }
  index = ldms_metric_by_name(set, "server_idx");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, "%s %s: unable to find field \"server_idx\" in schema %s - field not set\n", SAMP,
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_set_u64(set, index, server->server_idx);
  }
  return set;
}


static ldms_set_t _hf_set_create(fulldump_sub_ctxt_p self, struct source_data *server)
{
  ldms_set_t set;
  int index;
  char instance_name[LDMS_PRODUCER_NAME_MAX + 64];

  log_fn(LDMSD_LDEBUG, "%s: %s()\n", SAMP, __func__);
  struct xxc_extra *extra = self->extra;
  char *category = extra->source_category;
  snprintf(instance_name, sizeof(instance_name), "%s/%s/%s/%s/%s_hf",
           self->sampl_ctxt_p->producer_name, SAMP, server->fs_name, server->server_id, category);
  set = fulldump_general_create_set(log_fn, self->sampl_ctxt_p->producer_name, instance_name, &self->sampl_ctxt_p->auth, &extra->hf_cid, extra->hf_schema);
  if (!set) {
    log_fn(LDMSD_LERROR, "%s%s %s: unable to create set\n", SAMP, extra->source_category, __func__);
    return NULL;
  }
  index = ldms_metric_by_name(set, "fs_name");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, "%s %s: unable to find field \"fs_name\" in schema %s - field not set\n", SAMP,
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_array_set_str(set, index, server->fs_name);
  }
  index = ldms_metric_by_name(set, "server_idx");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, "%s %s: unable to find field \"server_idx\" in schema %s - field not set\n", SAMP,
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_set_u64(set, index, server->server_idx);
  }
  return set;
}


static struct source_data *_server_create(const char *server_dir, const char *basedir, fulldump_sub_ctxt_p self)
{
  struct source_data *server;
  char path_tmp[PATH_MAX];
  char *state;

  log_fn(LDMSD_LDEBUG, "%s %s() %s from %s\n", SAMP,
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
    log_fn(LDMSD_LWARNING, "%s%s: unable to parse server name \"%s\"; return code=%d\n", SAMP,
           __func__, server->name, rc);
    goto out5;
  }

  server->metric_set = _hist_set_create(self, server);
  if (server->metric_set == NULL)
    goto out6;
  server->hf_metric_set = _hf_set_create(self, server);
  if (server->hf_metric_set == NULL)
    goto out7;
  // add hf_data
  server->hf_data = extra->hf_data_handler->create();
  if (server->hf_data == NULL)
    goto out8;
  // TODO: start hi frequency sampling
  rbn_init(&server->tree_node, server->name);
  return server;

out8:
  fulldump_general_destroy_set(server->hf_metric_set);
out7:
  fulldump_general_destroy_set(server->metric_set);
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


static void _server_destroy(struct source_data *server, fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s() %s\n", SAMP, __func__, server->name);
  fulldump_general_destroy_set(server->metric_set);
  fulldump_general_destroy_set(server->hf_metric_set);
  struct xxc_extra *extra = self->extra;
  extra->hf_data_handler->destroy(server->hf_data);
  free(server->server_id);
  free(server->fs_name);
  free(server->file_path);
  free(server->dir_path);
  free(server->name);
  free(server);
}


void hf_hist_servers_destroy(struct rbt *source_tree, fulldump_sub_ctxt_p self)
{
  struct rbn *rbn;
  struct source_data *server;

  while (!rbt_empty(source_tree)) {
    rbn = rbt_min(source_tree);
    server = container_of(rbn, struct source_data, tree_node);
    rbt_del(source_tree, rbn);
    _server_destroy(server, self);
  }
}


/** List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 */
int hf_hist_servers_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self, const char *path)
// FIXME: Make sure the error handling is correct
{
  static int dir_once_log = 0;
  struct dirent *dirent;
  DIR *dir;
  struct rbt new_tree;
  int err = 0;
  struct xxc_extra *extra = self->extra;

  rbt_init(&new_tree, string_comparator);

  /* Make sure we have data objects in the new_tree for
     each currently existing directory.  We can find the objects
     cached in the old tree (in which case we move them
     to new_tree), or they can be newly allocated
     here. */
  dir = opendir(path);
  if (dir == NULL) {
    if (!dir_once_log) {
      log_fn(LDMSD_LDEBUG, "%s%s: unable to open dir %s\n", SAMP,
             __func__, path);
      dir_once_log = 1;
    }
    return 0;
  }
  dir_once_log = 0;

  pthread_mutex_lock(&extra->mutex);
  while ((dirent = readdir(dir)) != NULL) {
    struct rbn *rbn;
    struct source_data *server;
    if (dirent->d_type != DT_DIR ||
        strcmp(dirent->d_name, ".") == 0 ||
        strcmp(dirent->d_name, "..") == 0)
      continue;
    rbn = rbt_find(source_tree, dirent->d_name);
    errno = 0;
    if (rbn) {
      server = container_of(rbn, struct source_data, tree_node);
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
  hf_hist_servers_destroy(source_tree, self);
  /* copy the new_tree into place over the global source_tree */
  memcpy(source_tree, &new_tree, sizeof(struct rbt));
  pthread_mutex_unlock(&extra->mutex);
  return err;
}


void hf_servers_sample(struct xxc_extra *extra)
{
  struct rbn *rbn;
  struct source_data *server;
  struct hf_data_hanldler *handler = extra->hf_data_handler;
  pthread_mutex_lock(&extra->mutex);

  /* walk tree of known locations */
  struct rbt *source_tree = &extra->source_tree;
  RBT_FOREACH(rbn, source_tree)
  {
    server = container_of(rbn, struct source_data, tree_node);
    handler->on_sample(server);
  }
  pthread_mutex_unlock(&extra->mutex);
}


void hist_servers_sample(struct xxc_extra *xxc_extra, int (*single_sample)(const char *, ldms_set_t))
{
  struct rbn *rbn;
  struct source_data *server;

  /* walk tree of known locations */
  struct rbt *source_tree = &xxc_extra->source_tree;
  RBT_FOREACH(rbn, source_tree)
  {
    server = container_of(rbn, struct source_data, tree_node);
    // FIXME: Make sure the error handling is correct
    single_sample(server->file_path, server->metric_set);
  }
}


void hf_hist_term(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s() called\n", SAMP, __func__);
  struct xxc_extra *extra = self->extra;
  pthread_mutex_lock(&extra->mutex);
  tsampler_timer_remove(&extra->hf_timer);
  hf_hist_servers_destroy(&extra->source_tree, self);
  pthread_mutex_unlock(&extra->mutex);
  fulldump_general_schema_fini(self);
  if (extra->hf_schema != NULL) {
    ldms_schema_delete(extra->hf_schema);
    extra->hf_schema = NULL;
  }
}