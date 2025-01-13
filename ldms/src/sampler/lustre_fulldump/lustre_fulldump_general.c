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
#include <ctype.h>

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


int fulldump_general_schema_init(fulldump_sub_ctxt_p self, const char *schema_name,
      struct ldms_metric_template_s *schema_template, int *schema_ids,
      struct ldms_metric_template_s *metric_record_template, int *metric_record_ids,
      int record_idx, int list_idx, size_t maxlistsize)
{
  // TODO: what to do with the metric ids?

  ldms_schema_t sch;
  int rc;
  log_fn(LDMSD_LDEBUG, "%s: %s()\n", SAMP, __func__);

  // Finish defining the schema template
  struct ldms_metric_template_s *rec_entry = &schema_template[record_idx];
  struct ldms_metric_template_s *list_entry = &schema_template[list_idx];
  ldms_record_t rec_def = ldms_record_from_template(rec_entry->name, metric_record_template, metric_record_ids);
  if (!rec_def) {
    log_fn(LDMSD_LERROR, "%s: fulldump_general_schema_init() failed to create record template\n", SAMP);
    goto err1;

  }
  rec_entry->rec_def = rec_def;
  size_t sz = ldms_record_heap_size_get(rec_def);
  list_entry->len = maxlistsize * sz;

  // Create the schema
  sch = ldms_schema_new(schema_name);
  if (sch == NULL) {
    log_fn(LDMSD_LERROR, "%s: fulldump_general_schema_init schema new failed"
           " (out of memory)\n", SAMP);
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
  log_fn(LDMSD_LERROR, "%s: lustre_llite_general schema creation failed to add %s. (%s)\n", SAMP,
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
  if (!set) {
    return;
  }
  ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
  ldms_set_unpublish(set);
  ldms_set_delete(set);
}


void fulldump_general_schema_fini(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s: fulldump_general_schema_fini()\n", SAMP);
  if (self->schema != NULL) {
    ldms_schema_delete(self->schema);
    self->schema = NULL;
  }
}



int fulldump_split_server_name(char *server_name, char **fs_name, int *server_inx, char **server_id)
{
  char *name_, *id_;
  int idx_;
  char *first = strchr(server_name, '-');
  if (first == NULL) {
    return 0;
  }
  char *second = strchr(first + 1, '-');
  if (second == NULL) {
    second = server_name + strlen(server_name);
  }
  // char *third = strchr(second + 1, '-');
  // if (third == NULL) {
  //   return 2;
  // }
  // parse the fields
  int rc = sscanf(first + 1, "%*3s%d-", &idx_);
  if (rc != 1) {
    return -2;
  }
  name_ = strndup(server_name, first - server_name);
  if (name_ == NULL) {
    return -1;
  }
  id_ = strndup(first + 1, second - first - 1);
  if (id_ == NULL) {
    free(name_);
    return -1;
  }
  // return the results
  *fs_name = name_;
  *server_id = id_;
  *server_inx = idx_;
  return 3;
}


/* FIXME: we need to make sure that not only the directory exists,
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

int empty_line(char *line)
{
  char *p = line;
  while (*p != '\0' && *p != '\n' && *p != '\r') {
    if (!isspace(*p)) {
      return 0;
    }
    p++;
  }
  return 1;
}


int equal_strings_ignoring_spaces(const char *s1, const char *s2)
{
  // skip spaces at the beginning
  while (isspace(*s1)) {
    s1++;
  }
  while (isspace(*s2)) {
    s2++;
  }
  while (*s1 != '\0' && *s2 != '\0') {
    if (isspace(*s1) || isspace(*s2)) {
      if (!(isspace(*s2) && isspace(*s1))) {
        return 0;
      }
      while (isspace(*s1)) {
        s1++;
      }
      while (isspace(*s2)) {
        s2++;
      }
    } else if (*s1 != *s2) {
      return 0;
    } else {
      s1++;
      s2++;
    }
  }
  // at least one string is finished
  // but another may have only spaces left
  while (isspace(*s1)) {
    s1++;
  }
  while (isspace(*s2)) {
    s2++;
  }
  if (*s1 == '\0' && *s2 == '\0') {
    return 1;
  }
  return 0;
}