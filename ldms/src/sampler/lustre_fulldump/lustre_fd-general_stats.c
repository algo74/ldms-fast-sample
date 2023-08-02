/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
// #include <limits.h>
// #include <string.h>
// #include <unistd.h>

#include "lustre_fd-general_stats.h"

// #include "lustre_fulldump.h"
#include "lustre_fulldump_general.h"
#include "lustre_fulldump_xxc_general.h"

// #define _GNU_SOURCE

// #ifndef ARRAY_LEN
// #define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
// #endif


//static const char *current_path = "/sys/kernel/debug/lustre/osc";  // FIXME: this is a hack


struct ldms_metric_template_s fd_general_stats_schema_metric_record_templlate[METRIC_RECORD_LEN] = {
    {"metric_name", 0, LDMS_V_CHAR_ARRAY, "", MAXMETRICNAMESIZE},
    {"count", 0, LDMS_V_U64, "", 1},
    {"sum", 0, LDMS_V_U64, "", 1},
    {"sum2", 0, LDMS_V_U64, "", 1},
    {0},
};

/* metric templates for the set schema */
struct ldms_metric_template_s fd_general_stats_schema_templlate[] = {
    {"metric_record", 0, LDMS_V_RECORD_TYPE, "", /* set rec_def later */},
    {"fs_name", 2, LDMS_V_CHAR_ARRAY, "", MAXNAMESIZE},
    {"server_idx", 2, LDMS_V_U64, "", 1},
    {"snapshot_sec", 0, LDMS_V_U64, "", 1},
    {"snapshot_ns", 0, LDMS_V_U64, "", 1},
    {"metric_list", 0, LDMS_V_LIST, "", /* set heap_sz later */},
    {0},
};


int fd_general_stats_single_sample(fulldump_sub_ctxt_p self, struct source_data *source, void *virtual_args)
{
  FILE *sf;
  char buf[512];
  char str1[MAXNAMESIZE + 1];
  int err_code = 0;
  const char *stats_path = source->file_path;
  struct fd_general_stats_sample_args *args = (struct fd_general_stats_sample_args *)virtual_args;
  int *schema_ids = args->schema_ids;
  int *schema_metric_record_ids = args->schema_metric_record_ids;
  int rc;
  ldms_mval_t handle;
  uint64_t val1, val2, val3, val4;
  int index;

  log_fn(LDMSD_LDEBUG, "%s : %s: file %s\n", SAMP, __func__, stats_path);

  sf = fopen(stats_path, "r");
  if (sf == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s: file %s not found\n", SAMP,
           __func__, stats_path);
    return ENOENT;
  }
  // reading the first line (snapshot_time)
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s: failed on read from %s\n", SAMP,
           __func__, stats_path);
    err_code = ENOMSG;
    goto out1;
  }
  // log_fn(LDMSD_LDEBUG, "%s : llite_stats_sample: buf: %500s\n", SAMP, buf);
  rc = sscanf(buf, "%64s %lu.%lu", str1, &val1, &val2);
  if (rc != 3 || strncmp(str1, "snapshot_time", MAXNAMESIZE) != 0) {
    log_fn(LDMSD_LWARNING, "%s : first line in %s is not \", SAMPsnapshot_time\": %.512s\n",
           stats_path, buf);
    err_code = ENOMSG;
    goto out1;
  }
  // if it was the last line, return
  if (!fgets(buf, sizeof(buf), sf)) {
    log_fn(LDMSD_LDEBUG, "%s : %s: file %s contains only snapshot time\n", SAMP, __func__, stats_path);
    goto out1;
  }
  // else, make sure we instantiate the schema and the set and parse the rest of the file
  if (self->schema == NULL) {
    log_fn(LDMSD_LDEBUG, "%s %s: calling %s schema init\n", SAMP, __func__, args->schema_name);
    if (fulldump_general_schema_init(self,
                                     args->schema_name,
                                     fd_general_stats_schema_templlate,
                                     schema_ids,
                                     fd_general_stats_schema_metric_record_templlate,
                                     schema_metric_record_ids,
                                     METRIC_RECORD_ID, METRIC_LIST_ID, MAXLISTSIZE) < 0) {
      log_fn(LDMSD_LERROR, "%s %s: general schema create failed\n", SAMP, __func__);
      err_code = ENOMEM;
      goto out1;
    }
  }
  if (source->metric_set == NULL) {
    log_fn(LDMSD_LDEBUG, "%s %s: calling %s set create\n", SAMP, __func__, args->schema_name);
    source->metric_set = xxc_general_set_create(self, source);
    if (source->metric_set == NULL) {
      log_fn(LDMSD_LERROR, "%s %s: set create failed (schema: %s, source: %s) \n",
             SAMP, __func__, args->schema_name, source->name);
      err_code = ENOMEM;
      goto out1;
    }
  }
  ldms_set_t metric_set = source->metric_set;
  ldms_transaction_begin(metric_set);
  jobid_helper_metric_update(metric_set);
  ldms_metric_set_u64(metric_set, schema_ids[SNAPSHOT_SEC_ID], val1);
  ldms_metric_set_u64(metric_set, schema_ids[SNAPSHOT_NS_ID], val2);
  handle = ldms_metric_get(metric_set, schema_ids[METRIC_LIST_ID]);
  ldms_list_purge(metric_set, handle);
  do {
    rc = sscanf(buf, "%64s %lu samples [%*[^]]] %*u %*u %lu %lu",
                str1, &val1, &val2, &val3);
    if (rc == 2) {
      val2 = 0;
      val3 = 0;
    } else if (rc == 3) {
      val3 = 0;
    } else if (rc != 4) {
      log_fn(LDMSD_LWARNING, "%s : failed to parse line in %s: %s\n", SAMP,
             stats_path, buf);
      err_code = ENOMSG;
      goto out2;
    }
    ldms_mval_t rec_inst = ldms_record_alloc(metric_set, schema_ids[METRIC_RECORD_ID]);
    if (!rec_inst) {
      err_code = ENOTSUP;  // FIXME: implement resize
      goto out2;
    }
    ldms_mval_t name_handle = ldms_record_metric_get(rec_inst, schema_metric_record_ids[METRIC_NAME_ID]);
    snprintf(name_handle->a_char, MAXMETRICNAMESIZE, "%s", str1);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_COUNT_ID], val1);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_SUM_ID], val2);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_SUM2_ID], val3);
    ldms_list_append_record(metric_set, handle, rec_inst);
  } while (fgets(buf, sizeof(buf), sf));

out2:
  ldms_transaction_end(metric_set);
out1:
  fclose(sf);
  return err_code;
}


// int fd_general_stats_sample(fulldump_sub_ctxt_p self, enum node_type type)
// {
//   // NOTE: use lustre_fulldump_xxc_general.h:xxc_general_sample
// }


int fd_general_stats_term(fulldump_sub_ctxt_p self)
{
  struct xxc_extra *extra = self->extra;
  struct fd_general_stats_sample_args *args = (struct fd_general_stats_sample_args *)extra->virtual_args;
  free(args->schema_metric_record_ids);
  free(args->schema_ids);
  free(args);
  xxc_general_term(self);
}


int fd_general_stats_config(fulldump_sub_ctxt_p self, const char *path, enum node_type type,
                            const char *metric_name)
{
  int *schema_ids = calloc(METRIC_SCHEMA_LEN, sizeof(int));
  if (!schema_ids) {
    log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
    goto fd_general_stats_config_out1;
  }
  int *schema_metric_record_ids = calloc(METRIC_RECORD_LEN, sizeof(int));
  if (!schema_metric_record_ids) {
    log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
    goto fd_general_stats_config_out2;
  }
  struct fd_general_stats_sample_args *args = calloc(1, sizeof(struct fd_general_stats_sample_args));
  if (!args) {
    log_fn(LDMSD_LERROR, "%s %s: out of memory\n", SAMP, __func__);
    goto fd_general_stats_config_out3;
  }
  args->schema_name = metric_name;
  // args->set_name_suffix = set_name_suffix;
  args->schema_ids = schema_ids;
  args->schema_metric_record_ids = schema_metric_record_ids;
  self->sample = (int (*)(void *self))xxc_general_sample;
  self->term = (int (*)(void *self))fd_general_stats_term;
  return xxc_extra_config(self, "stats", path, type, fd_general_stats_single_sample, args);

  // free(args);
fd_general_stats_config_out3:
  free(schema_metric_record_ids);
fd_general_stats_config_out2:
  free(schema_ids);
fd_general_stats_config_out1:
  return ENOMEM;
}
