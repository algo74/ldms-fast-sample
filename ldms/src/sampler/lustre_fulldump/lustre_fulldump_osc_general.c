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

// #define _GNU_SOURCE


#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


struct osc_extra {
  struct rbt source_tree; /* red-black tree root for sources */
  char *source_category;  /* filename of the source */
};

struct osc_data {
  char *name;
  char *fs_name;
  int ost_id;
  char *dir_path;
  char *file_path;
  ldms_set_t metric_set; /* a pointer */
  struct rbn tree_node;
};


static int string_comparator(void *a, const void *b)
{
  return strcmp((char *)a, (char *)b);
}


int osc_extra_config(fulldump_sub_ctxt_p self, char *source_category)
{
  log_fn(LDMSD_LDEBUG, SAMP " %s config() called\n", __func__);
  struct osc_extra *extra = malloc(sizeof(struct osc_extra));
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


static ldms_set_t osc_set_create(fulldump_sub_ctxt_p self, struct osc_data *osc)
{
  ldms_set_t set;
  int index;
  char instance_name[LDMS_PRODUCER_NAME_MAX + 64];

  log_fn(LDMSD_LDEBUG, SAMP ": %s()\n", __func__);
  struct osc_extra *extra = self->extra;
  char *category = extra->source_category;
  snprintf(instance_name, sizeof(instance_name), "%s/%s/OST%d/%s",
           self->sampl_ctxt_p->producer_name, osc->fs_name, osc->ost_id, category);
  set = fulldump_general_create_set(log_fn, self->sampl_ctxt_p->producer_name, instance_name, &self->sampl_ctxt_p->auth, &self->cid, self->schema);
  if (!set) {
    return NULL;
  }
  index = ldms_metric_by_name(set, "fs_name");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, SAMP " %s: unable to find field \"fs_name\" in schema %s - field not set\n",
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_array_set_str(set, index, osc->fs_name);
  }
  index = ldms_metric_by_name(set, "ost_id");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, SAMP " %s: unable to find field \"ost_id\" in schema %s - field not set\n",
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_set_u64(set, index, osc->ost_id);
  }
  return set;
}


static struct osc_data *osc_create(const char *osc_dir, const char *basedir, fulldump_sub_ctxt_p self)
{
  struct osc_data *osc;
  char path_tmp[PATH_MAX];
  char *state;

  log_fn(LDMSD_LDEBUG, SAMP " %s() %s from %s\n",
         __func__, osc_dir, basedir);
  osc = calloc(1, sizeof(*osc));
  if (osc == NULL)
    goto out1;

  osc->name = strdup(osc_dir);
  if (osc->name == NULL)
    goto out2;

  snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, osc_dir);
  osc->dir_path = strdup(path_tmp);
  if (osc->dir_path == NULL)
    goto out3;

  struct osc_extra *extra = self->extra;
  snprintf(path_tmp, PATH_MAX, "%s/%s", osc->dir_path, extra->source_category);
  osc->file_path = strdup(path_tmp);
  if (osc->file_path == NULL)
    goto out4;

  int rc = fulldump_split_osc_name(osc->name, &osc->fs_name, &osc->ost_id);
  if (rc != 3) {
    log_fn(LDMSD_LWARNING, SAMP "%s: unable to parse OSC name \"%s\"; return code=%d\n",
           __func__, osc->name, rc);
    goto out5;
  }

  osc->metric_set = osc_set_create(self, osc);
  if (osc->metric_set == NULL)
    goto out6;
  rbn_init(&osc->tree_node, osc->name);
  return osc;

out6:
  free(osc->fs_name);
out5:
  free(osc->file_path);
out4:
  free(osc->dir_path);
out3:
  free(osc->name);
out2:
  free(osc);
out1:
  return NULL;
}


static void osc_destroy(struct osc_data *osc)
{
  log_fn(LDMSD_LDEBUG, SAMP " osc_destroy() %s\n", osc->name);
  fulldump_general_destroy_set(osc->metric_set);
  free(osc->fs_name);
  free(osc->file_path);
  free(osc->dir_path);
  free(osc->name);
  free(osc);
}


void oscs_destroy(struct rbt *osc_tree)
{
  struct rbn *rbn;
  struct osc_data *osc;

  while (!rbt_empty(osc_tree)) {
    rbn = rbt_min(osc_tree);
    osc = container_of(rbn, struct osc_data, tree_node);
    rbt_del(osc_tree, rbn);
    osc_destroy(osc);
  }
}


/** List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 */
int oscs_refresh(struct rbt *osc_tree, fulldump_sub_ctxt_p self, const char *path)
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
    struct osc_data *osc;

    if (dirent->d_type != DT_DIR ||
        strcmp(dirent->d_name, ".") == 0 ||
        strcmp(dirent->d_name, "..") == 0)
      continue;
    rbn = rbt_find(osc_tree, dirent->d_name);
    errno = 0;
    if (rbn) {
      osc = container_of(rbn, struct osc_data, tree_node);
      rbt_del(osc_tree, &osc->tree_node);
    } else {
      osc = osc_create(dirent->d_name, path, self);
    }
    if (osc == NULL) {
      err = errno;
      continue;
    }
    rbt_ins(&new_tree, &osc->tree_node);
  }
  closedir(dir);

  /* destroy any oscs remaining in the old osc_tree since we
     did not see their associated directories this time around */
  oscs_destroy(osc_tree);

  /* copy the new_tree into place over the global osc_tree */
  memcpy(osc_tree, &new_tree, sizeof(struct rbt));

  return err;
}


void oscs_sample(struct osc_extra *osc_extra, int (*single_sample)(const char *, ldms_set_t))
{
  struct rbn *rbn;
  struct osc_data *osc;

  /* walk tree of known oscs */
  struct rbt *osc_tree = &osc_extra->source_tree;
  RBT_FOREACH(rbn, osc_tree)
  {
    osc = container_of(rbn, struct osc_data, tree_node);
    // FIXME: Make sure the error handling is correct
    single_sample(osc->file_path, osc->metric_set);
  }
}


void osc_general_term(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, SAMP " %s() called\n", __func__);
  struct osc_extra *extra = self->extra;
  oscs_destroy(&extra->source_tree);
  fulldump_general_schema_fini(self);
}