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
#include "ldms_missing.h"
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


int xxc_extra_config(fulldump_sub_ctxt_p self,
                     const char *source_category,
                     const char *source_root,
                     enum node_type type,
                     int (*single_sample)(fulldump_sub_ctxt_p self, struct source_data *source, void *virtual_args),
                     void *virtual_args)
{
  int rc = 0;
  log_fn(LDMSD_LDEBUG, "%s  %s called\n", SAMP, __func__);
  struct xxc_extra *extra = calloc(1, sizeof(struct xxc_extra));
  if (extra == NULL) {
    log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
    return ENOMEM;
  }
  extra->source_category = source_category;
  extra->source_root = source_root;
  extra->type = type;
  extra->single_sample = single_sample;
  extra->virtual_args = virtual_args;
  if (NODE_TYPE_SINGLE_SOURCE == type) {
    rc = asprintf(&extra->single_source.file_path, "%s/%s", source_root, source_category);
    if (rc < 0) {
      log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
      free(extra);
      return rc;
    }
    // NOTE: here we simply copy dir_path, so we don't free it when destroying
    // extra->single_source.dir_path = source_root;
    // TODO: what would be the proper name for the single source?
    extra->single_source.name = extra->single_source.file_path;
    extra->single_source.fs_name = "";
    extra->single_source.server_id = "";
  } else {
    rbt_init(&extra->source_tree, string_comparator);
  }
  self->extra = extra;
  log_fn(LDMSD_LDEBUG, "%s : exiting normally\n", SAMP, __func__);
  return 0;
}


int xxc_legacy_extra_config(fulldump_sub_ctxt_p self, const char *source_category, const char *source_root)
{
  log_fn(LDMSD_LDEBUG, "%s  %s called\n", SAMP, __func__);
  return xxc_extra_config(self, source_category, source_root, NODE_TYPE_LEGACY, NULL, NULL);
  // struct xxc_extra *extra = calloc(1, sizeof(struct xxc_extra));
  // if (extra == NULL) {
  //   log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
  //   return ENOMEM;
  // }
  // extra->type = NODE_TYPE_LEGACY;
  // extra->source_category = source_category;
  // extra->source_root = source_root;
  // rbt_init(&extra->source_tree, string_comparator);
  // self->extra = extra;
  // log_fn(LDMSD_LDEBUG, "%s : exiting normally\n", SAMP, __func__);
  // return 0;
}


// TODO: remove this function as it is not used
// int single_source_extra_config(fulldump_sub_ctxt_p self, char *source_category, char *source_root)
// {
//   int rc = 0;
//   log_fn(LDMSD_LDEBUG, "%s  %s config() called\n", SAMP, __func__);
//   struct xxc_extra *extra = calloc(1, sizeof(struct xxc_extra));
//   if (extra == NULL) {
//     log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
//     return ENOMEM;
//   }
//   extra->source_category = source_category;
//   extra->source_root = source_root;
//   // combine the path and the category
//   rc = asprintf(&extra->single_source.file_path, "%s/%s", extra->source_root, source_category);
//   if (rc < 0) {
//     log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
//     goto out1;
//   }
//   // NOTE: the single source struct has no metric set nor schema configured; do it in "sample"
//   self->extra = extra;
//   log_fn(LDMSD_LDEBUG, "%s : exiting normally\n", SAMP, __func__);
//   return 0;

// out1:
//   extra->single_source.file_path = NULL;
//   free(extra);
//   return rc;
// }


ldms_set_t xxc_general_set_create(fulldump_sub_ctxt_p self, struct source_data *node)
{
  ldms_set_t set;
  int index;
  char instance_name[LDMS_PRODUCER_NAME_MAX + 64];

  log_fn(LDMSD_LDEBUG, "%s: %s()\n", SAMP, __func__);
  struct xxc_extra *extra = self->extra;
  char *schema_name = ldms_schema_name_get(self->schema);
  if (NODE_TYPE_SINGLE_SOURCE == extra->type) {
    snprintf(instance_name, sizeof(instance_name), "%s/%s",
             self->sampl_ctxt_p->producer_name, schema_name);
  } else {
    const char *category = extra->source_category;
    if (NODE_TYPE_FS == extra->type) {
      snprintf(instance_name, sizeof(instance_name), "%s/%s/%s",
              self->sampl_ctxt_p->producer_name, schema_name, node->fs_name);
    } else {
      snprintf(instance_name, sizeof(instance_name), "%s/%s/%s/%s",
              self->sampl_ctxt_p->producer_name, schema_name, node->fs_name, node->server_id);
    }
  }
  log_fn(LDMSD_LDEBUG, "%s: %s() instance_name=%s\n", SAMP, __func__, instance_name);
  set = fulldump_general_create_set(log_fn, self->sampl_ctxt_p->producer_name, instance_name, &self->sampl_ctxt_p->auth, &self->cid, self->schema);
  if (!set) {
    return NULL;
  }
  index = ldms_metric_by_name(set, "fs_name");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, "%s %s: unable to find field \"fs_name\" in schema %s - field not set\n", SAMP,
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_array_set_str(set, index, node->fs_name);
  }
  index = ldms_metric_by_name(set, "server_idx");
  if (-1 == index) {
    log_fn(LDMSD_LWARNING, "%s %s: unable to find field \"server_idx\" in schema %s - field not set\n", SAMP,
           __func__, ldms_set_schema_name_get(set));
  } else {
    ldms_metric_set_u64(set, index, node->server_idx);
  }
  return set;
}


static int _split_fs_name(char *input, char **fs_name)
{
  char *name_;
  char *first = strchr(input, '-');
  if (first == NULL) {
    first = input + strlen(input);
  }
  name_ = strndup(input, first - input);
  if (name_ == NULL) {
    return -1;
  }
  // return the results
  *fs_name = name_;
  return 1;
}


static int _node_init(struct source_data *node,
                      const char *server_dir,
                      const char *basedir,
                      fulldump_sub_ctxt_p self,
                      enum node_type type)
{
  char path_tmp[PATH_MAX];
  char *state;

  log_fn(LDMSD_LDEBUG, "%s %s() %s from %s\n", SAMP,
         __func__, server_dir, basedir);

  node->name = strdup(server_dir);
  if (node->name == NULL)
    goto out2;

  //TODO: check if asprintf is better
  struct xxc_extra *extra = self->extra;
  snprintf(path_tmp, PATH_MAX, "%s/%s/%s", basedir, server_dir, extra->source_category);
  node->file_path = strdup(path_tmp);
  if (node->file_path == NULL)
    goto out4;

  if (NODE_TYPE_SERVER == type || NODE_TYPE_LEGACY == type) {
    int rc = fulldump_split_server_name(node->name, &node->fs_name, &node->server_idx, &node->server_id);
    if (rc != 3) {
      log_fn(LDMSD_LWARNING, "%s%s: unable to parse server name \"%s\"; return code=%d\n", SAMP,
            __func__, node->name, rc);
      goto out5;
    }
  } else if (NODE_TYPE_FS == type) {
    int rc = _split_fs_name(node->name, &node->fs_name);
    if (rc != 1) {
      log_fn(LDMSD_LWARNING, "%s%s: unable to parse fs name \"%s\"; return code=%d\n", SAMP,
            __func__, node->name, rc);
      goto out5;
    }
  } else {
    log_fn(LDMSD_LERROR, "%s%s: unknown node type\n", SAMP, __func__);
    goto out5;
  }

  /* legacy samples expect the sets created for them but new samples create sets themselves when needed */
  if (NODE_TYPE_LEGACY == type) {
    node->metric_set = xxc_general_set_create(self, node);
    if (node->metric_set == NULL)
      goto out6;
  }
  rbn_init(&node->tree_node, node->name);
  return 0;

out6:
  free(node->fs_name);
out5:
  free(node->file_path);
out4:
  // free(node->dir_path);
out3:
  free(node->name);
out2:
  return 1;
}


static struct source_data *_node_create(const char *server_dir, const char *basedir, fulldump_sub_ctxt_p self, enum node_type type)
{
  struct source_data *node;

  log_fn(LDMSD_LDEBUG, "%s %s() %s from %s\n", SAMP,
         __func__, server_dir, basedir);
  node = calloc(1, sizeof(*node));
  if (node == NULL)
    return NULL;

  int rc = _node_init(node, server_dir, basedir, self, type);
  if (rc) {
    free(node);
    return NULL;
  }
  return node;
}


static struct source_data *_server_create(const char *server_dir, const char *basedir, fulldump_sub_ctxt_p self)
{
  return _node_create(server_dir, basedir, self, NODE_TYPE_SERVER);
}


static struct source_data *_fs_create(const char *fs_dir, const char *basedir, fulldump_sub_ctxt_p self)
{
  return _node_create(fs_dir, basedir, self, NODE_TYPE_FS);
}


static void _node_destroy(struct source_data *server)
{
  log_fn(LDMSD_LDEBUG, "%s %s() %s\n", SAMP, __func__, server->name);
  fulldump_general_destroy_set(server->metric_set);
  if (server->name) free(server->name);
  if (server->fs_name) free(server->fs_name);
  if (server->server_id) free(server->server_id);
  // if (server->dir_path) free(server->dir_path);
  if (server->file_path) free(server->file_path);
  free(server);
}


void multisource_destroy(struct rbt *source_tree)
{
  struct rbn *rbn;
  struct source_data *node;

  while (!rbt_empty(source_tree)) {
    rbn = rbt_min(source_tree);
    node = container_of(rbn, struct source_data, tree_node);
    rbt_del(source_tree, rbn);
    _node_destroy(node);
  }
}


void single_source_destroy(struct xxc_extra *extra)
{
  if (extra->single_source.file_path) free(extra->single_source.file_path);
  fulldump_general_destroy_set(extra->single_source.metric_set);
}


/** List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 */
int multisource_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self, enum node_type type)
// FIXME: Make sure the error handling is correct
{
  log_fn(LDMSD_LDEBUG, "%s %s: type %d\n", SAMP, __func__, type);
  int err = 0;
  if (NODE_TYPE_SINGLE_SOURCE == type) {
    log_fn(LDMSD_LDEBUG, "%s %s: single source refresh - doing nothing\n", SAMP, __func__);
    return 0;
  }

  struct dirent *dirent;
  DIR *dir;
  struct rbt new_tree;
  struct xxc_extra *extra = self->extra;
  const char *path = extra->source_root;

  rbt_init(&new_tree, string_comparator);

  /* Make sure we have data objects in the new_tree for
     each currently existing directory.  We can find the objects
     cached in the old tree (in which case we move them
     to new_tree), or they can be newly allocated
     here. */
  dir = opendir(path);
  if (dir == NULL) {
    if (!extra->dir_once_log) {
      log_fn(LDMSD_LDEBUG, "%s%s: unable to open dir %s\n", SAMP,
             __func__, path);
      extra->dir_once_log = 1;
    }
    return 0;
  }
  extra->dir_once_log = 0;
  while ((dirent = readdir(dir)) != NULL) {
    struct rbn *rbn;
    struct source_data *node;

    if (dirent->d_type != DT_DIR ||
        strcmp(dirent->d_name, ".") == 0 ||
        strcmp(dirent->d_name, "..") == 0)
      continue;
    rbn = rbt_find(source_tree, dirent->d_name);
    errno = 0;
    if (rbn) {
      node = container_of(rbn, struct source_data, tree_node);
      rbt_del(source_tree, &node->tree_node);
    } else {
      node = _node_create(dirent->d_name, path, self, type);
    }
    if (node == NULL) {
      err = errno;
      continue;
    }
    rbt_ins(&new_tree, &node->tree_node);
  }
  closedir(dir);

  /* destroy any items remaining in the old source_tree since we
     did not see their associated directories this time around */
  multisource_destroy(source_tree);

  /* copy the new_tree into place over the global source_tree */
  memcpy(source_tree, &new_tree, sizeof(struct rbt));

  return err;
}


int xxc_legacy_servers_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self)
{
  return multisource_refresh(source_tree, self, NODE_TYPE_LEGACY);
}


// int fs_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self)
// {
//   return multisource_refresh(source_tree, self, NODE_TYPE_FS);
// }


/**
 * Legacy sample function does not refresh the source tree;
 * it relies on the caller to do so.
 *
*/
void xxc_legacy_sample(struct xxc_extra *xxc_extra, int (*single_sample)(const char *, ldms_set_t))
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


void xxc_general_sample(fulldump_sub_ctxt_p self)
{
  struct xxc_extra *extra = self->extra;
  enum node_type type = extra->type;
  if (NODE_TYPE_LEGACY == type) {
    log_fn(LDMSD_LERROR, "%s %s: did nothing; need to call xxc_legacy_sample for NODE_TYPE_LEGACY\n", SAMP, __func__);
  } else if (NODE_TYPE_SINGLE_SOURCE == type) {
    log_fn(LDMSD_LDEBUG, "%s %s: single source sample\n", SAMP, __func__);
    extra->single_sample(self, &extra->single_source, extra->virtual_args);
  } else if (NODE_TYPE_SERVER == type || NODE_TYPE_FS == type) {
    log_fn(LDMSD_LDEBUG, "%s %s: multisource sample\n", SAMP, __func__);
    struct rbt *source_tree = &extra->source_tree;
    struct rbn *rbn;
    struct source_data *node;
    int rc = multisource_refresh(source_tree, self, type);
    if (rc) {
      log_fn(LDMSD_LERROR, "%s %s: multisource_refresh failed\n", SAMP, __func__);
      return;
    }
    RBT_FOREACH(rbn, source_tree) {
      node = container_of(rbn, struct source_data, tree_node);
      rc = extra->single_sample(self, node, extra->virtual_args);
      if (rc) {
        log_fn(LDMSD_LERROR, "%s %s: single_sample failed for file %s\n",
                SAMP, __func__, node->file_path);
      }
    }
  } else {
    log_fn(LDMSD_LERROR, "%s %s: unknown node type\n", SAMP, __func__);
  }
}


void xxc_general_multisource_term(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s() called\n", SAMP, __func__);
  struct xxc_extra *extra = self->extra;
  multisource_destroy(&extra->source_tree);
  fulldump_general_schema_fini(self);
}


void xxc_general_single_source_term(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s() called\n", SAMP, __func__);
  struct xxc_extra *extra = self->extra;
  single_source_destroy(extra);
  fulldump_general_schema_fini(self);
}


void xxc_general_term(fulldump_sub_ctxt_p self)
{
  struct xxc_extra *extra = self->extra;
  if (NODE_TYPE_SINGLE_SOURCE == extra->type) {
    xxc_general_single_source_term(self);
  } else {
    xxc_general_multisource_term(self);
  }
}
