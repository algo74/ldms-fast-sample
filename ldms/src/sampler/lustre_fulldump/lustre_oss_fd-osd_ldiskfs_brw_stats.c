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

// #include "lustre_fd-general_stats.h"

// #include "lustre_fulldump.h"
#include "lustre_fulldump_general.h"
#include "lustre_fulldump_xxc_general.h"

#include "lustre_oss_fd-osd_ldiskfs_brw_stats.h"

// #define _GNU_SOURCE


#define MAXMETRICNAMESIZE 128
#define MAXLISTSIZE 256
#define MAXBINLABLESIZE 64
#define SUB_SAMP "osd-ldiskfs_brw_stats"
#define SCHEMA_NAME "lustre_oss_fulldump_" SUB_SAMP

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

static const char *root_path = "/proc/fs/lustre/osd-ldiskfs";

/* template for the metric record */
static struct ldms_metric_template_s schema_metric_record_templlate[] = {
    {"metric_name", 0, LDMS_V_CHAR_ARRAY, "", MAXMETRICNAMESIZE},
    {"bin", 0, LDMS_V_U64, "", 1},
    {"read_count", 0, LDMS_V_U64, "", 1},
    {"write_count", 0, LDMS_V_U64, "", 1},
    {0},
};
static int schema_metric_record_ids[ARRAY_LEN(schema_metric_record_templlate)];
enum {
  METRIC_NAME_ID,
  METRIC_BIN_ID,
  METRIC_READ_COUNT_ID,
  METRIC_WRITE_COUNT_ID,
};
/* template for the set schema */
static struct ldms_metric_template_s schema_templlate[] = {
    {"metric_record", 0, LDMS_V_RECORD_TYPE, "", /* set rec_def later */},
    {"fs_name", 2, LDMS_V_CHAR_ARRAY, "", MAXNAMESIZE},
    {"server_idx", 2, LDMS_V_U64, "", 1},
    {"snapshot_sec", 0, LDMS_V_U64, "", 1},
    {"snapshot_ns", 0, LDMS_V_U64, "", 1},
    {"metric_list", 0, LDMS_V_LIST, "", /* set heap_sz later */},
    // TODO: make a separate sampler for these
    // {"read_RPCs_in_flight", 0, LDMS_V_U64, "", 1},
    // {"write_RPCs_in_flight", 0, LDMS_V_U64, "", 1},
    // {"pending_write_pages", 0, LDMS_V_U64, "", 1},
    // {"pending_read_pages", 0, LDMS_V_U64, "", 1},
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


static int _schema_init(fulldump_sub_ctxt_p self)
{
  return fulldump_general_schema_init(self, SCHEMA_NAME, schema_templlate, schema_ids,
                                      schema_metric_record_templlate, schema_metric_record_ids,
                                      METRIC_RECORD_ID, METRIC_LIST_ID, MAXLISTSIZE);
}


inline static int _start_of_histogram(char *buf, size_t buf_size, FILE *sf)
{
  if (compare_strings_ignore_spaces(buf, "read | write") == 0) {
    return 1;
  }
  return 0;
}

/**
 * \brief find and parse a part of the file that corresponds to one histogram
 * \param[in/out] buf A buffer to read lines into (must already contain a line from the file)
 * \return 0 on success, -1 if the end of file, errno on other error
 */
inline static int _parse_histogram(char *buf, size_t buf_size, FILE *sf,
                                   ldms_set_t metric_set, ldms_mval_t handle, const char *source_path)
{
  int rc;
  uint64_t val1, val2, val3;
  char *ptr;
  // skip lines until the start of the histogram
  while(!_start_of_histogram(buf, buf_size, sf)) {
    if (!fgets(buf, buf_size, sf)) {
      return -1;  // end of file; histogram not found
    }
  }
  // read the metric name (it's the first line until % sign)
  // replace each area of one or more spaces with an underscore
  if (!fgets(buf, buf_size, sf)) {
    return -1;  // end of file; metric name not found
  }
  char metric_name[MAXMETRICNAMESIZE + 1];
  char *cur_metric_name_char = metric_name;
  char *cur_buf_char = buf;
  while (*cur_buf_char != '\0' && *cur_buf_char != '%' && cur_metric_name_char - metric_name < MAXMETRICNAMESIZE) {
    if (isspace(*cur_buf_char)) {
      // replace each area of one or more spaces with an underscore
      *cur_metric_name_char = '_';
      do {
        ++cur_buf_char;
      } while (isspace(*cur_buf_char));
    } else {
      *cur_metric_name_char = *cur_buf_char;
      ++cur_buf_char;
    }
    ++cur_metric_name_char;
  }
  *cur_metric_name_char = '\0';

  // read the lines of the histogram
  while (fgets(buf, buf_size, sf)) {
    if (empty_line(buf)) {
      return 0;  // end of histogram but not end of file
    }
    char bin_label[MAXBINLABLESIZE + 1];
    rc = sscanf(buf, "%lu%" TOSTRING(MAXBINLABLESIZE) "s %lu%*[^|]|%lu", &val1, bin_label, &val2, &val3);
    if (rc != 4) {
      log_fn(LDMSD_LWARNING, "%s %s%s: failed to parse line in %s (%s): %s\n", SAMP, SUB_SAMP,
              __func__, source_path, metric_name, buf);
      return ENOMSG;
    }
    switch (bin_label[0]) {
      case ':':
        break;
      case 'K':
        val1 *= 1024;
        break;
      case 'M':
        val1 *= 1024 * 1024;
        break;
      case 'G':
        val1 *= 1024 * 1024 * 1024;
        break;
      // case 'T':
      //   val1 *= 1024 * 1024 * 1024 * 1024;
      //   break;
      default:
        log_fn(LDMSD_LWARNING, "%s %s%s: failed to parse line in %s (%s): %s\n", SAMP, SUB_SAMP,
                __func__, source_path, metric_name, buf);
        return ENOMSG;
    }
    ldms_mval_t rec_inst = ldms_record_alloc(metric_set, schema_ids[METRIC_RECORD_ID]);
    if (!rec_inst) {
      return ENOTSUP;  // FIXME: implement resize
    }
    ldms_mval_t name_handle = ldms_record_metric_get(rec_inst, schema_metric_record_ids[METRIC_NAME_ID]);
    snprintf(name_handle->a_char, MAXMETRICNAMESIZE, metric_name);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_BIN_ID], val1);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_READ_COUNT_ID], val2);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_WRITE_COUNT_ID], val3);
    ldms_list_append_record(metric_set, handle, rec_inst);
  }
  return -1;  // end of file and end of histogram
}


static int _single_sample(fulldump_sub_ctxt_p self, struct source_data *source, void *virtual_args)
{
  FILE *sf;
  char buf[512];
  char str1[MAXNAMESIZE + 1];
  int err_code = 0;
  const char *stats_path = source->file_path;
  // struct fd_general_stats_sample_args *args = (struct fd_general_stats_sample_args *)virtual_args;
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
    log_fn(LDMSD_LDEBUG, "%s %s: calling %s schema init\n", SAMP, __func__, SCHEMA_NAME);
    if (_schema_init(self) < 0) {
      log_fn(LDMSD_LERROR, "%s_%s %s: general schema create failed\n", SAMP, SUB_SAMP, __func__);
      err_code = ENOMEM;
      goto out1;
    }
  }
  if (source->metric_set == NULL) {
    log_fn(LDMSD_LDEBUG, "%s %s: calling %s set create\n", SAMP, __func__, SCHEMA_NAME);
    source->metric_set = xxc_general_set_create(self, source);
    if (source->metric_set == NULL) {
      log_fn(LDMSD_LERROR, "%s %s: set create failed (schema: %s, source: %s) \n",
             SAMP, __func__, SCHEMA_NAME, source->name);
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
    rc = _parse_histogram(buf, sizeof(buf), sf, metric_set, handle, stats_path);
  } while (0 == rc);

out2:
  ldms_transaction_end(metric_set);
out1:
  fclose(sf);
  return err_code;
}

// static int _local_term(fulldump_sub_ctxt_p self)
// {
//   xxc_general_term(self);
// }


int lustre_oss_fd_ldiskfs_brw_stats_config(fulldump_sub_ctxt_p self)
{
  self->sample = (int (*)(void *self))xxc_general_sample;
  self->term = (int (*)(void *self))xxc_general_term;
  return xxc_extra_config(self, "brw_stats", root_path, NODE_TYPE_SERVER, _single_sample, NULL);
}
