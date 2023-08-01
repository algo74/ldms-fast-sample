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
#include "lustre_fulldump_xxc_hf_hist.h"

#define SUB_SAMP "rpc_stats"

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

/* Histogram schemas */

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

/* metric templates for the set schema */
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


/* HF schema */

/* metric templates for the set schema */
static struct ldms_metric_template_s hf_schema_templlate[] = {
    {"fs_name", 2, LDMS_V_CHAR_ARRAY, "", MAXNAMESIZE},
    {"server_idx", 2, LDMS_V_U64, "", 1},
    {"read_RPCs_in_flight_n", 0, LDMS_V_U64, "", 1},
    {"read_RPCs_in_flight_sum", 0, LDMS_V_U64, "", 1},
    {"read_RPCs_in_flight_sum2", 0, LDMS_V_U64, "", 1},
    {"write_RPCs_in_flight_n", 0, LDMS_V_U64, "", 1},
    {"write_RPCs_in_flight_sum", 0, LDMS_V_U64, "", 1},
    {"write_RPCs_in_flight_sum2", 0, LDMS_V_U64, "", 1},
    {"penidng_write_pages_n", 0, LDMS_V_U64, "", 1},
    {"penidng_write_pages_sum", 0, LDMS_V_U64, "", 1},
    {"penidng_write_pages_sum2", 0, LDMS_V_U64, "", 1},
    {"penidng_read_pages_n", 0, LDMS_V_U64, "", 1},
    {"penidng_read_pages_sum", 0, LDMS_V_U64, "", 1},
    {"penidng_read_pages_sum2", 0, LDMS_V_U64, "", 1},
    {0},
};
static int hf_schema_ids[ARRAY_LEN(hf_schema_templlate)];
enum {
  HF_FS_NAME_ID,
  HF_INDEX_ID,
  READ_RPC_IN_FLIGHT_N_ID,
  READ_RPC_IN_FLIGHT_SUM_ID,
  READ_RPC_IN_FLIGHT_SUM2_ID,
  WRITE_RPC_IN_FLIGHT_N_ID,
  WRITE_RPC_IN_FLIGHT_SUM_ID,
  WRITE_RPC_IN_FLIGHT_SUM2_ID,
  PENDING_WRITE_PAGES_N_ID,
  PENDING_WRITE_PAGES_SUM_ID,
  PENDING_WRITE_PAGES_SUM2_ID,
  PENDING_READ_PAGES_N_ID,
  PENDING_READ_PAGES_SUM_ID,
  PENDING_READ_PAGES_SUM2_ID,
};

typedef struct hf_rpc_single_metric_data {
  uint64_t count;
  uint64_t sum;
  uint64_t sum2;
} hf_rpc_single_metric_data_t;

typedef struct hf_rpc_data {
  hf_rpc_single_metric_data_t read_RPCs_in_flight;
  hf_rpc_single_metric_data_t write_RPCs_in_flight;
  hf_rpc_single_metric_data_t pending_write_pages;
  hf_rpc_single_metric_data_t pending_read_pages;
} hf_rpc_data_t;

static int _read_one_metric_value_line(FILE *sf, const char *prefix, hf_rpc_single_metric_data_t *mdata, const char *source_path)
{
  char buf[512];
  uint64_t val;
  int rc;
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s%s: failed on read \"%s\" from %s\n", SAMP, SUB_SAMP,
           __func__, prefix, source_path);
    return -1;
  }
  size_t pref_len = strlen(prefix);
  if (strncmp(buf, prefix, pref_len) != 0) {
    log_fn(LDMSD_LWARNING, "%s %s%s: failed to parse line in %s (not %s): %s\n", SAMP, SUB_SAMP,
           __func__, source_path, prefix, buf);
    return -2;
  }
  rc = sscanf(buf+pref_len+1, "%lu", &val);
  if (rc != 1) {
    log_fn(LDMSD_LWARNING, "%s %s%s: failed to parse line \"%s\" in %s: %s\n", SAMP, SUB_SAMP,
           __func__, prefix, source_path, buf);
    return -3;
  }
  mdata->count += 1;
  mdata->sum += val;
  mdata->sum2 += val * val;
  return 0;
}

static hf_rpc_data_t *hf_rpc_data_create()
{
  hf_rpc_data_t *data = calloc(1, sizeof(hf_rpc_data_t));
  if (!data) {
    log_fn(LDMSD_LERROR, "%s %s %s: calloc failed\n", SAMP, SUB_SAMP, __func__);
    return NULL;
  }
  return data;
}

static void hf_rpc_data_destroy(hf_rpc_data_t *data)
{
  free(data);
}

static void hf_rpc_data_on_timer(struct source_data *server)
{
  hf_rpc_data_t *data = server->hf_data;
  log_fn(LDMSD_LDEBUG, "%s %s %s: %s\n", SAMP, SUB_SAMP, __func__, server->name);
  char *source_path = server->file_path;
  FILE *sf;
  char buf[512];
  int rc;
  uint64_t val;
  log_fn(LDMSD_LDEBUG, "%s %s: %s: file %s\n", SAMP, SUB_SAMP, __func__, source_path);

  sf = fopen(source_path, "r");
  if (sf == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s%s: file %s not found\n", SAMP, SUB_SAMP,
           __func__, source_path);
    return;
  }
  // reading the first line (snapshot_time)
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s%s: failed on read snapshot time from %s\n", SAMP, SUB_SAMP,
           __func__, source_path);
    goto out1;
  }
  // skip the snapshot line
  // read "read RPCs in flight"
  if (_read_one_metric_value_line(sf, "read RPCs in flight", &data->read_RPCs_in_flight, source_path) != 0) {
    goto out1;
  }
  // read "write RPCs in flight"
  if (_read_one_metric_value_line(sf, "write RPCs in flight", &data->write_RPCs_in_flight, source_path) != 0) {
    goto out1;
  }
  // read "pending write pages"
  if (_read_one_metric_value_line(sf, "pending write pages", &data->pending_write_pages, source_path) != 0) {
    goto out1;
  }
  // read "pending read pages"
  if (_read_one_metric_value_line(sf, "pending read pages", &data->pending_read_pages, source_path) != 0) {
    goto out1;
  }

out1:
  fclose(sf);
}

static void hf_rpc_data_on_sample(struct source_data *server)
{
  hf_rpc_data_t *data = server->hf_data;
  log_fn(LDMSD_LDEBUG, "%s %s %s: %s\n", SAMP, SUB_SAMP, __func__, server->name);
  ldms_set_t metric_set = server->hf_metric_set;
  ldms_transaction_begin(metric_set);
  jobid_helper_metric_update(metric_set);
  ldms_metric_set_u64(metric_set, hf_schema_ids[READ_RPC_IN_FLIGHT_N_ID], data->read_RPCs_in_flight.count);
  ldms_metric_set_u64(metric_set, hf_schema_ids[READ_RPC_IN_FLIGHT_SUM_ID], data->read_RPCs_in_flight.sum);
  ldms_metric_set_u64(metric_set, hf_schema_ids[READ_RPC_IN_FLIGHT_SUM2_ID], data->read_RPCs_in_flight.sum2);
  ldms_metric_set_u64(metric_set, hf_schema_ids[WRITE_RPC_IN_FLIGHT_N_ID], data->write_RPCs_in_flight.count);
  ldms_metric_set_u64(metric_set, hf_schema_ids[WRITE_RPC_IN_FLIGHT_SUM_ID], data->write_RPCs_in_flight.sum);
  ldms_metric_set_u64(metric_set, hf_schema_ids[WRITE_RPC_IN_FLIGHT_SUM2_ID], data->write_RPCs_in_flight.sum2);
  ldms_metric_set_u64(metric_set, hf_schema_ids[PENDING_WRITE_PAGES_N_ID], data->pending_write_pages.count);
  ldms_metric_set_u64(metric_set, hf_schema_ids[PENDING_WRITE_PAGES_SUM_ID], data->pending_write_pages.sum);
  ldms_metric_set_u64(metric_set, hf_schema_ids[PENDING_WRITE_PAGES_SUM2_ID], data->pending_write_pages.sum2);
  ldms_metric_set_u64(metric_set, hf_schema_ids[PENDING_READ_PAGES_N_ID], data->pending_read_pages.count);
  ldms_metric_set_u64(metric_set, hf_schema_ids[PENDING_READ_PAGES_SUM_ID], data->pending_read_pages.sum);
  ldms_metric_set_u64(metric_set, hf_schema_ids[PENDING_READ_PAGES_SUM2_ID], data->pending_read_pages.sum2);
  ldms_transaction_end(metric_set);
  // zero out the data
  memset(data, 0, sizeof(hf_rpc_data_t));
}

static struct hf_data_hanldler hf_rpc_data_handler = {
    .create = (void *(*)(void))hf_rpc_data_create,
    .destroy = (void(*)(void *)) hf_rpc_data_destroy,
    .on_timer = hf_rpc_data_on_timer,
    .on_sample = hf_rpc_data_on_sample,
};

static int local_schema_init(fulldump_sub_ctxt_p self)
{
  return fulldump_general_schema_init(self, "lustre_fulldump_osc_" SUB_SAMP, schema_templlate, schema_ids,
                                      schema_metric_record_templlate, schema_metric_record_ids,
                                      METRIC_RECORD_ID, METRIC_LIST_ID, MAXLISTSIZE);
}


/**
 * \brief find and parse a part of the file that corresponds to one histogram
 * \param[in] metric_name The name of the metric of the histogram
 * \return 0 on success, -1 if the end of file, errno on other error
*/
inline static int _parse_histogram(char *metric_name, char *buf, size_t buf_size, FILE *sf,
                           ldms_set_t metric_set, ldms_mval_t handle, const char *source_path)
{
  int rc;
  uint64_t val1, val2, val3;
  char *ptr;
  while (fgets(buf, buf_size, sf)) {
    // skip until line starts with "pages per rpc"
    if (strncmp(buf, metric_name, strlen(metric_name)) != 0) {
      continue;
    }
    while (fgets(buf, buf_size, sf)) {
      if (empty_line(buf)) {
        return 0; // end of histogram but not end of file
      }
      rc = sscanf(buf, "%lu:%lu%*[^|]|%lu", &val1, &val2, &val3);
      if (rc != 3) {
        log_fn(LDMSD_LWARNING, "%s %s%s: failed to parse line in %s (pages per rpc): %s\n", SAMP, SUB_SAMP,
               __func__, source_path, buf);
        return ENOMSG;
      }
      ldms_mval_t rec_inst = ldms_record_alloc(metric_set, schema_ids[METRIC_RECORD_ID]);
      if (!rec_inst) {
        return ENOTSUP;  // FIXME: implement resize
      }
      ldms_mval_t name_handle = ldms_record_metric_get(rec_inst, schema_metric_record_ids[METRIC_NAME_ID]);
      snprintf(name_handle->a_char, MAXMETRICNAMESIZE, metric_name);
      // replace spaces with underscores
      for (ptr = name_handle->a_char; *ptr != '\0'; ptr++) {
        if (*ptr == ' ') {
          *ptr = '_';
        }
      }
      ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_BIN_ID], val1);
      ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_READ_COUNT_ID], val2);
      ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_WRITE_COUNT_ID], val3);
      ldms_list_append_record(metric_set, handle, rec_inst);
    }
    return -1; // end of file and end of histogram
  }
  return -1; // end of file; histogram not found
}

static int local_sample(const char *source_path, ldms_set_t metric_set)
{
  FILE *sf;
  char buf[512];
  char str1[MAXNAMESIZE + 1];
  int err_code = 0;
  int rc;
  ldms_mval_t handle;
  uint64_t val1, val2, val3, val4;
  int index;

  log_fn(LDMSD_LDEBUG, "%s %s: %s: file %s\n", SAMP, SUB_SAMP, __func__, source_path);

  sf = fopen(source_path, "r");
  if (sf == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s%s: file %s not found\n", SAMP, SUB_SAMP,
           __func__, source_path);
    return ENOENT;
  }
  // reading the first line (snapshot_time)
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s%s: failed on read from %s\n", SAMP, SUB_SAMP,
           __func__, source_path);
    err_code = ENOMSG;
    goto out1;
  }
  rc = sscanf(buf, "%64s %lu.%lu", str1, &val1, &val2);
  if (rc != 3 || strncmp(str1, "snapshot_time:", MAXNAMESIZE) != 0) {
    log_fn(LDMSD_LWARNING, "%s %s: first line in %s is not \"snapshot_time\": %.512s\n", SAMP, SUB_SAMP,
           source_path, str1, buf);
    err_code = ENOMSG;
    goto out1;
  }
  ldms_transaction_begin(metric_set);
  jobid_helper_metric_update(metric_set);
  ldms_metric_set_u64(metric_set, schema_ids[SNAPSHOT_SEC_ID], val1);
  ldms_metric_set_u64(metric_set, schema_ids[SNAPSHOT_NS_ID], val2);
  handle = ldms_metric_get(metric_set, schema_ids[METRIC_LIST_ID]);
  ldms_list_purge(metric_set, handle);
  // process "pages per rpc"
  rc = _parse_histogram("pages per rpc", buf, sizeof(buf), sf, metric_set, handle, source_path);
  if (rc != 0) {
    err_code = rc == -1 ? 0 : rc;
    goto out2;
  }
  // process "rpcs in flight"
  rc = _parse_histogram("rpcs in flight", buf, sizeof(buf), sf, metric_set, handle, source_path);
  if (rc != 0) {
    err_code = rc == -1 ? 0 : rc;
    goto out2;
  }
  // process "offset"
  rc = _parse_histogram("offset", buf, sizeof(buf), sf, metric_set, handle, source_path);
  if (rc != 0) {
    err_code = rc == -1 ? 0 : rc;
    goto out2;
  }
  // done

out2:
  ldms_transaction_end(metric_set);
out1:
  fclose(sf);
  return err_code;
}


static int sample(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s %s() called\n", SAMP, SUB_SAMP, __func__);
  struct xxc_extra *extra = self->extra;
  if (self->schema == NULL) {
    log_fn(LDMSD_LDEBUG, "%s %s %s: calling schema init\n", SAMP, SUB_SAMP, __func__);
    if (local_schema_init(self) < 0) {
      log_fn(LDMSD_LERROR, "%s %s %s schema create failed\n", SAMP, SUB_SAMP, __func__);
      return ENOMEM;

    }
  }
  if (extra->hf_schema == NULL) {
    log_fn(LDMSD_LDEBUG, "%s %s %s: hf schema init\n", SAMP, SUB_SAMP, __func__);
    if (hf_schema_init(self, "lustre_fulldump_osc_" SUB_SAMP "_hf", hf_schema_templlate, hf_schema_ids) < 0) {
      log_fn(LDMSD_LERROR, "%s %s %s hf schema create failed\n", SAMP, SUB_SAMP, __func__);
      return ENOMEM;
    }
  }
  hf_servers_sample(extra);
  // FIXME: make sure that the path contains the right files; until then, don't update the path
  // if (0 == update_existing_path(&current_path, paths, paths_len)) {
  //   log_fn(LDMSD_LWARNING, "%s %s %s: no path found\n", SAMP, SUB_SAMP, __func__);
  //   return 0;
  // };
  log_fn(LDMSD_LDEBUG, "%s %s %s calling refresh\n", SAMP, SUB_SAMP, __func__);
  int err = hf_hist_servers_refresh(&extra->source_tree, self, current_path);
  if (err) /* running out of set memory is an error */
    return err;

  hist_servers_sample(extra, local_sample);
  return 0;
}


int lustre_fulldump_osc_rpc_stats_config(fulldump_sub_ctxt_p self)
{
  self->sample = (int (*)(void *self)) sample;
  self->term = (int (*)(void *self)) hf_hist_term;
  return hf_hist_extra_config(self, SUB_SAMP, &hf_rpc_data_handler);
}
