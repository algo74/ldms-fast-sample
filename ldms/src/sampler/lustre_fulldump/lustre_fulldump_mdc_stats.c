/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "lustre_fulldump.h"
#include "lustre_fulldump_general.h"
#include "lustre_fulldump_xxc_general.h"

// #define _GNU_SOURCE

#define SUB_SAMP "mdc_stats"

#define MAXMETRICNAMESIZE 128
#define MAXLISTSIZE 256

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *current_path = "/sys/kernel/debug/lustre/mdc";

static struct ldms_metric_template_s schema_metric_record_templlate[] = {
    {"metric_name", 0, LDMS_V_CHAR_ARRAY, "", MAXMETRICNAMESIZE},
    {"count", 0, LDMS_V_U64, "", 1},
    // {"min", 0, LDMS_V_U64, "", 1},
    // {"max", 0, LDMS_V_U64, "", 1},
    {"sum", 0, LDMS_V_U64, "", 1},
    {"sum2", 0, LDMS_V_U64, "", 1},
    {0},
};
static int schema_metric_record_ids[ARRAY_LEN(schema_metric_record_templlate)];
enum {
  METRIC_NAME_ID,
  METRIC_COUNT_ID,
  METRIC_SUM_ID,
  METRIC_SUM2_ID,
};

/* metric templates for the set schema */
static struct ldms_metric_template_s schema_templlate[] = {
    {"metric_record", 0, LDMS_V_RECORD_TYPE, "", /* set rec_def later */},
    {"fs_name", 2, LDMS_V_CHAR_ARRAY, "", MAXNAMESIZE},
    {"server_idx", 2, LDMS_V_U64, "", 1},
    {"snapshot_sec", 0, LDMS_V_U64, "", 1},
    {"snapshot_ns", 0, LDMS_V_U64, "", 1},
    {"metric_list", 0, LDMS_V_LIST, "", /* set heap_sz later */},
    {0},
};
static int schema_ids[ARRAY_LEN(schema_templlate)];
enum {
  METRIC_RECORD_ID,
  FS_NAME_ID,
  INDEX_ID,
  SNAPSHOT_SEC_ID,
  SNAPSHOT_NS_ID,
  METRIC_LIST_ID
};


static int local_schema_init(fulldump_sub_ctxt_p self)
{
  return fulldump_general_schema_init(self, "lustre_fulldump_mdc_stats", schema_templlate, schema_ids,
                                      schema_metric_record_templlate, schema_metric_record_ids,
                                      METRIC_RECORD_ID, METRIC_LIST_ID, MAXLISTSIZE);
}


static int local_sample(const char *stats_path, ldms_set_t metric_set)
{
  FILE *sf;
  char buf[512];
  char str1[MAXNAMESIZE + 1];
  int err_code = 0;
  int rc;
  ldms_mval_t handle;
  uint64_t val1, val2, val3, val4;
  int index;

  log_fn(LDMSD_LDEBUG, SAMP " " SUB_SAMP " %s: file %s\n", __func__, stats_path);

  sf = fopen(stats_path, "r");
  if (sf == NULL) {
    log_fn(LDMSD_LWARNING, SAMP " " SUB_SAMP " %s: file %s not found\n",
           __func__, stats_path);
    return ENOENT;
  }
  // reading the first line (snapshot_time)
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, SAMP " " SUB_SAMP " %s: failed on read from %s\n",
           __func__, stats_path);
    err_code = ENOMSG;
    goto out1;
  }
  // log_fn(LDMSD_LDEBUG, SAMP ": llite_stats_sample: buf: %500s\n", buf);
  rc = sscanf(buf, "%64s %lu.%lu", str1, &val1, &val2);
  if (rc != 3 || strncmp(str1, "snapshot_time", MAXNAMESIZE) != 0) {
    log_fn(LDMSD_LWARNING, SAMP " " SUB_SAMP " %s: first line in %s is not \"snapshot_time\": %.512s\n",
           __func__, stats_path, buf);
    err_code = ENOMSG;
    goto out1;
  }
  ldms_transaction_begin(metric_set);
  jobid_helper_metric_update(metric_set);
  ldms_metric_set_u64(metric_set, schema_ids[SNAPSHOT_SEC_ID], val1);
  ldms_metric_set_u64(metric_set, schema_ids[SNAPSHOT_NS_ID], val2);
  handle = ldms_metric_get(metric_set, schema_ids[METRIC_LIST_ID]);
  ldms_list_purge(metric_set, handle);
  while (fgets(buf, sizeof(buf), sf)) {
    rc = sscanf(buf, "%64s %lu samples [%*[^]]] %*u %*u %lu %lu",
                str1, &val1, &val2, &val3);
    if (rc == 2) {
      val2 = 0;
      val3 = 0;
    } else if (rc == 3) {
      val3 = 0;
    } else if (rc != 4) {
      log_fn(LDMSD_LWARNING, SAMP " " SUB_SAMP " %s : failed to parse line in %s: %s\n",
             __func__, stats_path, buf);
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
  }

out2:
  ldms_transaction_end(metric_set);
out1:
  fclose(sf);
  return err_code;
}


static int sample(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, SAMP " " SUB_SAMP " %s() called\n", __func__);
  struct xxc_extra *extra = self->extra;
  if (self->schema == NULL) {
    log_fn(LDMSD_LDEBUG, SAMP " " SUB_SAMP " %s: calling schema init\n", __func__);
    if (local_schema_init(self) < 0) {
      log_fn(LDMSD_LERROR, SAMP " " SUB_SAMP " %s general schema create failed\n", __func__);
      return ENOMEM;

    }
  }
  // FIXME: make sure that the path contains the right files; until then, don't update the path
  // if (0 == update_existing_path(&current_path, paths, paths_len)) {
  //   log_fn(LDMSD_LWARNING, SAMP " %s: no path found\n", __func__);
  //   return 0;
  // };
  log_fn(LDMSD_LDEBUG, SAMP " " SUB_SAMP " %s calling refresh\n", __func__);
  int err = servers_refresh(&extra->source_tree, self, current_path);
  if (err) /* running out of set memory is an error */
    return err;

  servers_sample(extra, local_sample);
  return 0;
}


int lustre_fulldump_mdc_stats_config(fulldump_sub_ctxt_p self)
{
  self->sample = (int (*)(void *self)) sample;
  self->term = (int (*)(void *self)) server_general_term;
  return server_extra_config(self, "stats");
}
