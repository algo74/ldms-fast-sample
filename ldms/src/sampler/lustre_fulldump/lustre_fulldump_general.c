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

// #define _GNU_SOURCE

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// ldmsd_msg_log_f log_fn;


int fulldump_general_schema_init(fulldump_sub_ctxt_p self, char *schema_name,
      struct ldms_metric_template_s *schema_template, int *schema_ids,
      struct ldms_metric_template_s *metric_record_template, int *metric_record_ids,
      int record_idx, int list_idx, size_t maxlistsize)
{
  // TODO: what to do with the metric ids?

  ldms_schema_t sch;
  int rc;
  log_fn(LDMSD_LDEBUG, SAMP ": llite_stats_schema_init()\n");

  // Finish defining the schema template
  struct ldms_metric_template_s *rec_entry = &schema_template[record_idx];
  struct ldms_metric_template_s *list_entry = &schema_template[list_idx];
  ldms_record_t rec_def = ldms_record_from_template(rec_entry->name, metric_record_template, metric_record_ids);
  if (!rec_def) {
    log_fn(LDMSD_LERROR, SAMP ": fulldump_general_schema_init() failed to create record template\n");
    goto err1;

  }
  rec_entry->rec_def = rec_def;
  size_t sz = ldms_record_heap_size_get(rec_def);
  list_entry->len = maxlistsize * sz;

  // Create the schema
  sch = ldms_schema_new(schema_name);
  if (sch == NULL) {
    log_fn(LDMSD_LERROR, SAMP
           ": fulldump_general_schema_init schema new failed"
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
  rc = ldms_schema_metric_add_template(sch, schema_template, schema_ids);
  if (rc < 0) {
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


ldms_set_t fulldump_general_create_set(ldmsd_msg_log_f log_fn,
                                        const char *producer_name,
                                        const char *instance_name,
                                        const struct base_auth *auth,
                                        const comp_id_t cid,
                                        const ldms_schema_t schema)
{
  ldms_set_t set;
  int index;
  set = ldms_set_new(instance_name, schema);
  if (!set) {
    errno = ENOMEM;
    return NULL;
  }
  ldms_set_producer_name_set(set, producer_name);
  base_auth_set(auth, set);
  comp_id_helper_metric_update(set, cid);
  ldms_set_publish(set);
  ldmsd_set_register(set, SAMP);
  return set;
}


void fulldump_general_destroy_set(ldms_set_t set)
{
  ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
  ldms_set_unpublish(set);
  ldms_set_delete(set);
}


void fulldump_general_schema_fini(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, SAMP ": fulldump_general_schema_fini()\n");
  if (self->schema != NULL) {
    ldms_schema_delete(self->schema);
    self->schema = NULL;
  }
}



int fulldump_split_osc_name(char *osc_name, char **fs_name, int *ost_id)
{
  char *name_;
  int id_;
  char *first = strchr(osc_name, '-');
  if (first == NULL) {
    return 0;
  }
  char *second = strchr(first + 1, '-');
  if (second == NULL) {
    return 1;
  }
  char *third = strchr(second + 1, '-');
  if (third == NULL) {
    return 2;
  }
  // parse the fields
  int rc = sscanf(first + 1, "OST%d-", &id_);
  if (rc != 1) {
    return -2;
  }
  name_ = strndup(osc_name, first - osc_name);
  if (name_ == NULL) {
    return -1;
  }
  // return the results
  *fs_name = name_;
  *ost_id = id_;
  return 3;
}


/* Different versions of Lustre put files in different place.
   Returns a pointer to a path, or NULL if no directory found anywhere.
   FIXME: we need to make sure that not only the directory exists,
   but that it contains the file we need.
 */
int update_existing_path(const char **current_path, const char *const *path_options, size_t paths_len)
{
  struct stat sb;
  int i;
  if (*current_path && stat(*current_path, &sb) != -1 && S_ISDIR(sb.st_mode))
    return 1;

  for (i = 0; i < paths_len; i++) {
    if (stat(path_options[i], &sb) != -1 && S_ISDIR(sb.st_mode)) {
      *current_path = path_options[i];
      return 2;
    }
  }
  return 0;
}