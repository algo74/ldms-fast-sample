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

#define MAXMETRICNAMESIZE 128
#define MAXLISTSIZE 256

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


// static const char *const paths[] = {
//     "/sys/kernel/debug/lustre/osc" /* lustre 2.12 and later */
// };
// static const int paths_len = sizeof(paths) / sizeof(paths[0]);
static const char *current_path = "/proc/fs/lustre/osc";  // FIXME: this is a hack


static struct ldms_metric_template_s schema_metric_record_templlate[] = {
    {"portal", 0, LDMS_V_S64, "", 1},
    {"current", 0, LDMS_V_U64, "", 1},
    {"last", 0, LDMS_V_U64, "", 1},
    {0},
};
static int schema_metric_record_ids[ARRAY_LEN(schema_metric_record_templlate)];
enum {
  PORTAL_ID,
  CURRENT_ID,
  LAST_ID,
};

/* metric templates for the set schema */
static struct ldms_metric_template_s schema_templlate[] = {
    {"metric_record", 0, LDMS_V_RECORD_TYPE, "", /* set rec_def later */},
    {"fs_name", 2, LDMS_V_CHAR_ARRAY, "", MAXNAMESIZE},
    {"server_idx", 2, LDMS_V_U64, "", 1},
    {"snapshot_sec", 0, LDMS_V_U64, "", 1},
    // {"snapshot_ns", 0, LDMS_V_U64, "", 1},
    {"metric_list", 0, LDMS_V_LIST, "", /* set heap_sz later */},
    {0},
};
static int schema_ids[ARRAY_LEN(schema_templlate)];
enum {
  METRIC_RECORD_ID,
  FS_NAME_ID,
  INDEX_ID,
  SNAPSHOT_SEC_ID,
  // SNAPSHOT_NS_ID,
  METRIC_LIST_ID
};


static int osc_timeouts_schema_init(fulldump_sub_ctxt_p self)
{
  return fulldump_general_schema_init(self, "lustre_fulldump_osc_timeouts", schema_templlate, schema_ids,
                                      schema_metric_record_templlate, schema_metric_record_ids,
                                      METRIC_RECORD_ID, METRIC_LIST_ID, MAXLISTSIZE);
}


static int osc_timeouts_sample(const char *source_path, ldms_set_t metric_set)
{
  FILE *sf;
  char buf[512];
  char str1[MAXMETRICNAMESIZE + 1];
  int err_code = 0;
  int rc;
  ldms_mval_t list_handle;
  uint64_t val0, val1, val2;
  int index;

  log_fn(LDMSD_LDEBUG, "%s: %s: file %s\n", SAMP, __func__, source_path);

  sf = fopen(source_path, "r");
  if (sf == NULL) {
    log_fn(LDMSD_LWARNING, "%s%s: file %s not found\n", SAMP,
           __func__, source_path);
    return ENOENT;
  }
  // reading the first line (snapshot time)
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, "%s%s: failed on read from %s\n", SAMP,
           __func__, source_path);
    err_code = ENOMSG;
    goto out1;
  }
  // log_fn(LDMSD_LDEBUG, "%s: llite_stats_sample: buf: %500s\n", SAMP, buf);
  rc = sscanf(buf, "%64[^:]:%lu", str1, &val1);
  // log_fn(LDMSD_LDEBUG, "%s: first line in %s results: (\"%s\", %d)\n", SAMP,
  //        source_path, str1, val1);
  if (rc != 2 || strncmp(str1, "last reply ", MAXMETRICNAMESIZE) != 0) {
    log_fn(LDMSD_LWARNING, "%s: first line in %s is not \"last reply\" (return code: %d): %.512s\n", SAMP,
           source_path, rc, buf);
    err_code = ENOMSG;
    goto out1;
  }
  ldms_transaction_begin(metric_set);
  jobid_helper_metric_update(metric_set);
  ldms_metric_set_u64(metric_set, schema_ids[SNAPSHOT_SEC_ID], val1);
  list_handle = ldms_metric_get(metric_set, schema_ids[METRIC_LIST_ID]);
  ldms_list_purge(metric_set, list_handle);
  while (fgets(buf, sizeof(buf), sf)) {
    // geting the portal name
    char *column = strchr(buf, ':');
    if (column == NULL) {
      log_fn(LDMSD_LWARNING, "%s%s: failed to parse line in %s (no column): %s\n", SAMP,
             __func__, source_path, buf);
      err_code = ENOMSG;
      goto out2;
    }
    // skip trailing spaces
    char *metric_end = column - 1;
    while (metric_end > buf && isspace(*metric_end)) --metric_end;
    if (metric_end == buf) {
      log_fn(LDMSD_LWARNING, "%s%s: failed to parse line in %s (no metric name): %s\n", SAMP,
             __func__, source_path, buf);
      err_code = ENOMSG;
      goto out2;
    }
    size_t len = metric_end - buf + 1;
    len = len < MAXMETRICNAMESIZE ? len : MAXMETRICNAMESIZE;
    strncpy(str1, buf, len); str1[len] = '\0';
    // log_fn(LDMSD_LDEBUG, "%s %s: metric is \"%s\"\n", SAMP, __func__, str1);
    if (strncmp(str1, "network", len) == 0) {
      val0 = 0;
    } else {
      rc = sscanf(str1, "portal %lu", &val0);
      if (rc != 1) {
        log_fn(LDMSD_LWARNING, "%s%s: failed to parse portal name in %s (set to -1): %s\n", SAMP,
               __func__, source_path, buf);
        val0 = -1;
      }
    }
    // read current timlimit
    rc = sscanf(column, ": cur %lu %*[^)]) %*s %*s %*s %lu", &val1, &val2);
    if (rc != 2) {
      log_fn(LDMSD_LWARNING, "%s%s: failed to parse line in %s (return code %d): %s\n", SAMP,
             __func__, source_path, rc, buf);
      err_code = ENOMSG;
      goto out2;
    }
    // log_fn(LDMSD_LDEBUG, "%s %s: cur timelimit is \"%d\"\n", SAMP, __func__, val1);
    // log_fn(LDMSD_LDEBUG, "%s %s: last time is \"%d\"\n", SAMP, __func__, val2);
    ldms_mval_t rec_inst = ldms_record_alloc(metric_set, schema_ids[METRIC_RECORD_ID]);
    if (!rec_inst) {
      err_code = ENOTSUP;  // FIXME: implement resize
      goto out2;
    }
    // ldms_mval_t name_handle = ldms_record_metric_get(rec_inst, schema_metric_record_ids[PORTAL_ID]);
    // snprintf(name_handle->a_char, MAXMETRICNAMESIZE, "%s", str1);
    ldms_record_set_s64(rec_inst, schema_metric_record_ids[PORTAL_ID], val0);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[CURRENT_ID], val1);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[LAST_ID], val2);
    ldms_list_append_record(metric_set, list_handle, rec_inst);
  }

out2:
  ldms_transaction_end(metric_set);
out1:
  fclose(sf);
  return err_code;
}


static int sample(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s() called\n", SAMP, __func__);
  struct xxc_extra *extra = self->extra;
  if (self->schema == NULL) {
    log_fn(LDMSD_LDEBUG, "%s %s: calling schema init\n", SAMP, __func__);
    if (osc_timeouts_schema_init(self) < 0) {
      log_fn(LDMSD_LERROR, "%s %s general schema create failed\n", SAMP, __func__);
      return ENOMEM;

    }
  }
  // FIXME: make sure that the path contains the right files; until then, don't update the path
  // if (0 == update_existing_path(&current_path, paths, paths_len)) {
  //   log_fn(LDMSD_LWARNING, "%s %s: no path found\n", SAMP, __func__);
  //   return 0;
  // };
  log_fn(LDMSD_LDEBUG, "%s %s calling refresh\n", SAMP, __func__);
  int err = xxc_legacy_servers_refresh(&extra->source_tree, self);
  if (err) /* running out of set memory is an error */
    return err;

  xxc_legacy_sample(extra, osc_timeouts_sample);
  return 0;
}


int lustre_fulldump_osc_timeouts_config(fulldump_sub_ctxt_p self)
{
  self->sample = (int (*)(void *self)) sample;
  self->term = (int (*)(void *self)) xxc_general_multisource_term;
  return xxc_legacy_extra_config(self, "timeouts", current_path);
}
