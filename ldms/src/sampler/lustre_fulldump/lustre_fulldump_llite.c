/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
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

// #define _GNU_SOURCE

#define MAXMETRICNAMESIZE 128
#define MAXLISTSIZE 256

/* locations where llite stats might be found */
static const char *const llite_paths[] = {
    "/proc/fs/lustre/llite",         /* lustre pre-2.12 */
    "/sys/kernel/debug/lustre/llite" /* lustre 2.12 and later */
};
static const int llite_paths_len = sizeof(llite_paths) / sizeof(llite_paths[0]);

static struct llite_extra {
  /* red-black tree root for llites */
  struct rbt llite_tree;
};

struct llite_data {
  char *fs_name;
  char *name;
  char *path;
  char *stats_path;
  ldms_set_t metric_set; /* a pointer */
  struct rbn llite_tree_node;
};

static struct ldms_metric_template_s llite_schema_metric_record_templlate[] = {
    {"metric_name", 0, LDMS_V_CHAR_ARRAY, "", MAXMETRICNAMESIZE},
    {"count", 0, LDMS_V_U64, "", 1},
    // {"min", 0, LDMS_V_U64, "", 1},
    // {"max", 0, LDMS_V_U64, "", 1},
    {"sum", 0, LDMS_V_U64, "", 1},
    // {"sum2", 0, LDMS_V_U64, "", 1},
    {0},
};
static int llite_schema_metric_record_ids[ARRAY_LEN(llite_schema_metric_record_templlate)];
enum {
  METRIC_NAME_ID,
  METRIC_COUNT_ID,
  METRIC_SUM_ID,
};

/* metric templates for the set schema */
static struct ldms_metric_template_s llite_schema_templlate[] = {
    {"metric_record", 0, LDMS_V_RECORD_TYPE, "", /* set rec_def later */},
    {"fs_name", 0, LDMS_V_CHAR_ARRAY, "", MAXNAMESIZE},
    {"snapshot_sec", 0, LDMS_V_U64, "", 1},
    {"snapshot_ns", 0, LDMS_V_U64, "", 1},
    {"metric_list", 0, LDMS_V_LIST, "", /* set heap_sz later */},
    {0},
};
static int llite_schema_ids[ARRAY_LEN(llite_schema_templlate)];
enum {
  METRIC_RECORD_ID,
  FS_NAME_ID,
  SNAPSHOT_SEC_ID,
  SNAPSHOT_NS_ID,
  METRIC_LIST_ID
};

static int string_comparator(void *a, const void *b)
{
  return strcmp((char *)a, (char *)b);
}


int llite_stats_schema_init(fulldump_sub_ctxt_p self)
{
  // TODO: what to do with the metric ids?

  ldms_schema_t sch;
  int rc;
  ldmsd_msg_log_f log_fn = self->sampl_ctxt_p->log_fn;
  log_fn(LDMSD_LDEBUG, SAMP ": llite_stats_schema_init()\n");

  // Finish defining the schema template
  struct ldms_metric_template_s *rec_entry = &llite_schema_templlate[0];
  struct ldms_metric_template_s *list_entry = &llite_schema_templlate[ARRAY_LEN(llite_schema_templlate) - 2];
  rec_def = ldms_record_from_template(rec_entry->name, llite_schema_metric_record_templlate, llite_schema_metric_record_ids);
  if (!rec_def) {
    log_fn(LDMSD_LERROR, SAMP ": llite_stats_schema_init() failed to create record template\n");
    goto err1;
  }
  rec_entry->rec_def = rec_def;
  size_t sz = ldms_record_heap_size_get(rec_def);
  list_entry->len = MAXLISTSIZE * sz;

  // Create the schema
  sch = ldms_schema_new("lustre_fulldump_llite_stats");
  if (sch == NULL) {
    log_fn(LDMSD_LERROR, SAMP
           ": lustre_fulldump_llite_stats schema new failed"
           " (out of memory)\n");
    goto err2;
  }
  const char *field;
  field = "component id";
  rc = comp_id_helper_schema_add(sch, &self->cid);
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
  rc = ldms_schema_metric_add_template(sch, llite_schema_templlate, llite_schema_ids);
  if (rc < 0) {
    log_fn(LDMSD_LERROR, SAMP ": llite_stats_schema_init() failed to add %s. (%s)\n",
           field, STRERROR(-rc));
    goto err3;
  }
  self->schema = sch;

  return 0;

err3:
  log_fn(LDMSD_LERROR, SAMP ": lustre_llite_general schema creation failed to add %s. (%s)\n",
         field, STRERROR(-rc));
  ldms_schema_delete(sch);
err2:
  ldms_record_delete(rec_def);
err1:
  return -1;
}

void llite_general_schema_fini(fulldump_sub_ctxt_p self)
{
  ldmsd_msg_log_f log_fn = self->sampl_ctxt_p->log_fn;
  log_fn(LDMSD_LDEBUG, SAMP ": llite_general_schema_fini()\n");
  if (self->schema != NULL) {
    ldms_schema_delete(self->schema);
    self->schema = NULL;
  }
}


ldms_set_t llite_set_create(ldmsd_msg_log_f log_fn,
                            const char *producer_name,
                            const char *fs_name,
                            const struct base_auth *auth,
                            const comp_id_t cid,
                            const ldms_schema_t schema)
{
  ldms_set_t set;
  int index;
  char instance_name[LDMS_PRODUCER_NAME_MAX + 64];

  log_fn(LDMSD_LDEBUG, SAMP ": llite_set_create()\n");
  snprintf(instance_name, sizeof(instance_name), "%s/%s",
           producer_name, fs_name);
  set = ldms_set_new(instance_name, schema);
  if (!set) {
    errno = ENOMEM;
    return NULL;
  }
  ldms_set_producer_name_set(set, producer_name);
  base_auth_set(auth, set);
  index = ldms_metric_by_name(set, "fs_name");
  ldms_metric_array_set_str(set, index, fs_name);
  comp_id_helper_metric_update(set, cid);
  ldms_set_publish(set);
  ldmsd_set_register(set, SAMP);
  return set;
}


static struct llite_data *llite_create(const char *llite_name, const char *basedir, fulldump_sub_ctxt_p self)
{
  struct llite_data *llite;
  char path_tmp[PATH_MAX];
  char *state;

  ldmsd_msg_log_f log_fn = self->sampl_ctxt_p->log_fn;
  log_fn(LDMSD_LDEBUG, SAMP " llite_create() %s from %s\n",
         llite_name, basedir);
  llite = calloc(1, sizeof(*llite));
  if (llite == NULL)
    goto out1;

  // TODO: truncate the llite name (to the first dash) and use that as the fs name

  llite->name = strdup(llite_name);
  if (llite->name == NULL)
    goto out2;
  snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, llite_name);
  llite->path = strdup(path_tmp);
  if (llite->path == NULL)
    goto out3;
  snprintf(path_tmp, PATH_MAX, "%s/stats", llite->path);
  llite->stats_path = strdup(path_tmp);
  if (llite->stats_path == NULL)
    goto out4;

  llite->fs_name = strdup(llite_name);
  if (llite->fs_name == NULL)
    goto out5;
  if (strtok_r(llite->fs_name, "-", &state) == NULL) {
    log_fn(LDMSD_LWARNING, SAMP " unable to parse filesystem name from \"%s\"\n",
           llite->fs_name);
    goto out6;
  }
  llite->metric_set = llite_set_create(log_fn, self->sampl_ctxt_p->producer_name,
                                                   llite->fs_name, 
                                                   &self->sampl_ctxt_p->auth, &self->cid, self->schema);
  if (llite->metric_set == NULL)
    goto out6;
  rbn_init(&llite->llite_tree_node, llite->name);

  return llite;
out6:
  free(llite->fs_name);
out5:
  free(llite->stats_path);
out4:
  free(llite->path);
out3:
  free(llite->name);
out2:
  free(llite);
out1:
  return NULL;
}


static void llite_destroy(struct llite_data *llite, ldmsd_msg_log_f log_fn)
{
  log_fn(LDMSD_LDEBUG, SAMP " llite_destroy() %s\n", llite->name);
  llite_general_destroy(llite->metric_set);
  free(llite->fs_name);
  free(llite->stats_path);
  free(llite->path);
  free(llite->name);
  free(llite);
}


static void llites_destroy(struct rbn *llite_tree_p, ldmsd_msg_log_f log_fn)
{
  struct rbn *rbn;
  struct llite_data *llite;

  while (!rbt_empty(llite_tree_p)) {
    rbn = rbt_min(llite_tree_p);
    llite = container_of(rbn, struct llite_data,
                         llite_tree_node);
    rbt_del(llite_tree_p, rbn);
    llite_destroy(llite);
  }
}

/* Different versions of Lustre put the llite client stats in different place.
   Returns a pointer to a path, or NULL if no llite directory found anywhere.
 */
static const char *const find_llite_path(ldmsd_msg_log_f log_fn)
{
  static const char *previously_found_path = NULL;
  struct stat sb;
  int i;

  for (i = 0; i < llite_paths_len; i++) {
    if (stat(llite_paths[i], &sb) == -1 || !S_ISDIR(sb.st_mode))
      continue;
    if (previously_found_path != llite_paths[i]) {
      /* just for logging purposes */
      previously_found_path = llite_paths[i];
      log_fn(LDMSD_LDEBUG, SAMP " find_llite_path() found %s\n",
             llite_paths[i]);
    }
    return llite_paths[i];
  }

  log_fn(LDMSD_LWARNING, SAMP " no llite directories found\n");
  return NULL;
}

static int dir_once_log;


/** List subdirectories in llite_path to get list of
 * LLITE names.  Create llite_data structures for any LLITEs that we
 * have not seen, and delete any that we no longer see. 
 */
static int llites_refresh(struct rbt *llite_tree_p, ldmsd_msg_log_f log_fn)
  //FIXME: Make sure the error handling is correct
{
  const char *llite_path;
  struct dirent *dirent;
  DIR *dir;
  struct rbt new_llite_tree;
  int err = 0;

  llite_path = find_llite_path();
  if (llite_path == NULL) {
    return 0;
  }

  rbt_init(&new_llite_tree, string_comparator);

  /* Make sure we have llite_data objects in the new_llite_tree for
     each currently existing directory.  We can find the objects
     cached in the old llite_tree (in which case we move them
     from llite_tree to new_llite_tree), or they can be newly allocated
     here. */
  dir = opendir(llite_path);
  if (dir == NULL) {
    if (!dir_once_log) {
      log_fn(LDMSD_LDEBUG, SAMP " unable to open llite dir %s\n",
             llite_path);
      dir_once_log = 1;
    }
    return 0;
  }
  dir_once_log = 0;
  while ((dirent = readdir(dir)) != NULL) {
    struct rbn *rbn;
    struct llite_data *llite;

    if (dirent->d_type != DT_DIR ||
        strcmp(dirent->d_name, ".") == 0 ||
        strcmp(dirent->d_name, "..") == 0)
      continue;
    rbn = rbt_find(llite_tree_p, dirent->d_name);
    errno = 0;
    if (rbn) {
      llite = container_of(rbn, struct llite_data,
                           llite_tree_node);
      rbt_del(llite_tree_p, &llite->llite_tree_node);
    } else {
      llite = llite_create(dirent->d_name, llite_path);
    }
    if (llite == NULL) {
      err = errno;
      continue;
    }
    rbt_ins(&new_llite_tree, &llite->llite_tree_node);
  }
  closedir(dir);

  /* destroy any llites remaining in the old llite_tree since we
     did not see their associated directories this time around */
  llites_destroy(llite_tree_p, log_fn);

  /* copy the new_llite_tree into place over the global llite_tree */
  memcpy(llite_tree_p, &new_llite_tree, sizeof(struct rbt));

  return err;
}


static int llite_stats_sample(const char *stats_path, ldms_set_t metric_set)
{
  FILE *sf;
  char buf[512];
  char str1[MAXNAMESIZE + 1];
  int err_code = 0;
  int rc;
  ldms_mval_t handle;
  uint64_t val1, val2, val3, val4;
  int index;

  sf = fopen(stats_path, "r");
  if (sf == NULL) {
    log_fn(LDMSD_LWARNING, SAMP ": file %s not found\n",
           stats_path);
    return ENOENT;
  }

  /* The first line should always be "snapshot_time"
     we will ignore it because it always contains the time that we read
     from the file, not any information about when the stats last
     changed */
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, SAMP ": failed on read from %s\n",
           stats_path);
    err_code = ENOMSG;
    goto out1;
  }
  rc = sscanf(buf, "%64s %lu.%lu", str1, &val1, &val2);
  if (rc != 2) {
    log_fn(LDMSD_LWARNING, SAMP ": first line in %s is not \"snapshot_time\": %s\n",
           stats_path, buf);
    err_code = ENOMSG;
    goto out1;

  } 
  ldms_transaction_begin(metric_set);
  jobid_helper_metric_update(metric_set);
  ldms_metric_set_u64(metric_set, llite_schema_ids[SNAPSHOT_SEC_ID], val1);
  ldms_metric_set_u64(metric_set, llite_schema_ids[SNAPSHOT_NS_ID], val2);
  handle = ldms_metric_get(metric_set, llite_schema_ids[METRIC_LIST_ID]);
  ldms_list_purge(metric_set, handle);
  while (fgets(buf, sizeof(buf), sf)) {
    rc = sscanf(buf, "%64s %lu samples [%*[^]]] %*u %*u %lu",
                str1, &val1, &val2);
    if (rc == 2) {
      val2 = 0;
    } else if (rc != 3) {
      log_fn(LDMSD_LWARNING, SAMP ": failed to parse line in %s: %s\n",
             stats_path, buf);
      err_code = ENOMSG;
      goto out2;

    }
    ldms_mval_t rec_inst = ldms_record_alloc(metric_set, llite_schema_ids[METRIC_RECORD_ID]);
    if (!rec_inst) {
      err_code = ENOTSUP;  // FIXME: implement resize
      goto out2;
      
    }
    ldms_mval_t name_handle = ldms_record_metric_get(rec_inst, llite_schema_metric_record_ids[METRIC_NAME_ID]);
    snprintf(name_handle->a_char, MAXMETRICNAMESIZE, "%s", str1);
    ldms_record_set_u64(rec_inst, llite_schema_metric_record_ids[METRIC_COUNT_ID], val1);
    ldms_record_set_u64(rec_inst, llite_schema_metric_record_ids[METRIC_SUM_ID], val2);
    ldms_list_append_record(metric_set, handle, rec_inst);
  }

  out2:
    ldms_transaction_end(metric_set);
  out1:
    fclose(sf);
    return err_code;
  }

static void llites_sample(rbn *llite_tree_p, ldmsd_msg_log_f log_fn)
{
  struct rbn *rbn;

  /* walk tree of known LLITEs */
  RBT_FOREACH(rbn, llite_tree_p)
  {
    struct llite_data *llite;
    llite = container_of(rbn, struct llite_data, llite_tree_node);
    llite_stats_sample(llite->stats_path, llite->metric_set);
  }
}


static int config(fulldump_sub_ctxt_p self)
{
  ldmsd_msg_log_f log_fn = self->sampl_ctxt_p->log_fn;
  log_fn(LDMSD_LDEBUG, " config() called\n");
  struct llite_extra *extra = malloc(sizeof(struct llite_extra));
  if (extra == NULL) {
    log_fn(LDMSD_LERROR, SAMP " llite config: out of memory\n");
    return ENOMEM;
  }
  rbt_init(&extra->llite_tree, string_comparator);
  self->extra = extra;
  return 0;
}

static int sample(fulldump_sub_ctxt_p self)
{
  ldmsd_msg_log_f log_fn = self->sampl_ctxt_p->log_fn;
  log_fn(LDMSD_LDEBUG, SAMP " llite sample() called\n");
  struct llite_extra *extra = self->extra;
  struct llite_extra *my_extra = self->extra;
  if (self->schema == NULL) {
    if (llite_stats_schema_init(self) < 0) {
      log_fn(LDMSD_LERROR, SAMP " general schema create failed\n");
      return ENOMEM;

      int err = llites_refresh(&extra->llite_tree, log_fn);
      if (err) /* running out of set memory is an error */
        return err;

    }
  }
  llites_sample(&extra->llite_tree, log_fn);

  return 0;
}

static void term(fulldump_sub_ctxt_p self)
{
  // TODO

  log_fn(LDMSD_LDEBUG, SAMP " term() called\n");
  struct llite_extra *extra = self->extra;
  llites_destroy(&extra->llite_tree, self->sampl_ctxt_p->log_fn);
  llite_general_schema_fini(self);
}

int lustre_fulldump_llite_config(fulldump_sub_ctxt_p self)
{
  self->sample = sample;
  self->term = term;
  return config(self);
}
