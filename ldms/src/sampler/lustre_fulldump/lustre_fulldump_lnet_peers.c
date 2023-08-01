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

#define SUB_SAMP "peers"

// #define _GNU_SOURCE

#define MAXMETRICNAMESIZE 128
#define MAXLISTSIZE 256

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef QUOTE
#define QUOTE(str) #str
#endif

#ifndef QUOTE_VALUE
#define QUOTE_VALUE(str) QUOTE(str)
#endif

ldmsd_msg_log_f log_fn;

static const char *current_path = "/sys/kernel/debug/lnet";

static struct ldms_metric_template_s schema_metric_record_templlate[] = {
    {"nid", 0, LDMS_V_CHAR_ARRAY, "", MAXMETRICNAMESIZE},
    {"refs", 0, LDMS_V_S64, "", 1},
    {"state", 0, LDMS_V_CHAR_ARRAY, "", MAXMETRICNAMESIZE},
    {"last", 0, LDMS_V_S64, "", 1},
    {"max", 0, LDMS_V_S64, "", 1},
    {"rtr", 0, LDMS_V_S64, "", 1},
    {"min_rtr", 0, LDMS_V_S64, "", 1},
    {"tx", 0, LDMS_V_S64, "", 1},
    {"min_tx", 0, LDMS_V_S64, "", 1},
    {"queue", 0, LDMS_V_S64, "", 1},
    {0},
};
static int schema_metric_record_ids[ARRAY_LEN(schema_metric_record_templlate)];
enum {
  METRIC_NID_ID,
  METRIC_REFS_ID,
  METRIC_STATE_ID,
  METRIC_LAST_ID,
  METRIC_MAX_ID,
  METRIC_RTR_ID,
  METRIC_MIN_RTR_ID,
  METRIC_TX_ID,
  METRIC_MIN_TX_ID,
  METRIC_QUEUE_ID,
};

/* metric templates for the set schema */
static struct ldms_metric_template_s schema_templlate[] = {
    {"metric_record", 0, LDMS_V_RECORD_TYPE, "", /* set rec_def later */},
    {"metric_list", 0, LDMS_V_LIST, "", /* set heap_sz later */},
    {0},
};
static int schema_ids[ARRAY_LEN(schema_templlate)];
enum {
  METRIC_RECORD_ID,
  METRIC_LIST_ID
};

struct lnet_extra {
  ldms_set_t metric_set;
};


static int local_schema_init(fulldump_sub_ctxt_p self)
{
  return fulldump_general_schema_init(self, "lustre_fulldump_lnet_" SUB_SAMP, schema_templlate, schema_ids,
                                      schema_metric_record_templlate, schema_metric_record_ids,
                                      METRIC_RECORD_ID, METRIC_LIST_ID, MAXLISTSIZE);
}


static ldms_set_t local_set_create(const char *producer_name,
                                   const struct base_auth *auth,
                                   const comp_id_t cid,
                                   const ldms_schema_t schema)
{
  ldms_set_t set;
  int index;
  char instance_name[LDMS_PRODUCER_NAME_MAX + 64];

  log_fn(LDMSD_LDEBUG, "%s %s: %s()\n", SAMP, SUB_SAMP, __func__);
  snprintf(instance_name, sizeof(instance_name), "%s/%s/%s",
           producer_name, SAMP, SUB_SAMP);
  set = fulldump_general_create_set(log_fn, producer_name, instance_name, auth, cid, schema);
  if (!set) {
    return NULL;
  }
  log_fn(LDMSD_LDEBUG, "%s %s: %s() exited normally\n", SAMP, SUB_SAMP, __func__);
  return set;
}


static int local_sample(fulldump_sub_ctxt_p self)
// FIXME: Make sure the error handling is correct
{
  // static int dir_once_log = 0;
  char source_path[PATH_MAX];
  FILE *sf;
  char buf[512];
  char nid[MAXNAMESIZE + 1], state[MAXNAMESIZE + 1];
  uint64_t refs, last, max, rtr, min_rtr, tx, min_tx, queue;
  int err_code = 0;
  int rc;
  ldms_mval_t handle;
  int index;

  sprintf(source_path, "%s/peers", current_path);
  log_fn(LDMSD_LDEBUG, "%s %s: %s: file %s\n", SAMP, SUB_SAMP, __func__, source_path);

  sf = fopen(source_path, "r");
  if (sf == NULL) {
    // TODO: log once
    log_fn(LDMSD_LWARNING, "%s %s %s: file %s not found\n", SAMP, SUB_SAMP,
           __func__, source_path);
    return ENOENT;
  }
  // reading the first line (header) and ignore
  if (fgets(buf, sizeof(buf), sf) == NULL) {
    log_fn(LDMSD_LWARNING, "%s %s %s: failed on read from %s\n", SAMP, SUB_SAMP,
           __func__, source_path);
    err_code = ENOMSG;
    goto out1;
  }
  struct lnet_extra *extra = self->extra;
  ldms_set_t metric_set = extra->metric_set;
  if (metric_set == NULL) {
    fulldump_ctxt_p ctxt = self->sampl_ctxt_p;
    metric_set = local_set_create(ctxt->producer_name, &ctxt->auth, &self->cid, self->schema);
    if (metric_set == NULL) {
      log_fn(LDMSD_LERROR, "%s %s %s: failed to create metric set\n", SAMP, SUB_SAMP,
             __func__);
      err_code = ENOMEM;
      goto out1;
  }
    extra->metric_set = metric_set;
  }
  ldms_transaction_begin(metric_set);
  jobid_helper_metric_update(metric_set);
  handle = ldms_metric_get(metric_set, schema_ids[METRIC_LIST_ID]);
  ldms_list_purge(metric_set, handle);
  while (fgets(buf, sizeof(buf), sf)) {
    // log_fn(LDMSD_LDEBUG, "%s %s %s: parsing line in %s: %s\n", SAMP, SUB_SAMP,
    //         __func__, source_path, buf);
    rc = sscanf(buf, "%" QUOTE_VALUE(MAXNAMESIZE) "s %ld %" QUOTE_VALUE(MAXNAMESIZE) "s %ld %ld %ld %ld %ld %ld %ld",
                nid, &refs, state, &last, &max, &rtr, &min_rtr, &tx, &min_tx, &queue);
    // log_fn(LDMSD_LDEBUG, "Result: %s %ld %s %ld %ld %ld %ld %ld %ld %ld",
    //        nid, refs, state, last, max, rtr, min_rtr, tx, min_tx, queue);
    if (rc != 10) {
      log_fn(LDMSD_LWARNING, "%s %s %s: failed to parse line in %s (rc: %d): %s\n", SAMP, SUB_SAMP,
             __func__, source_path, rc, buf);
      err_code = ENOMSG;
      goto out2;
    }
    ldms_mval_t rec_inst = ldms_record_alloc(metric_set, schema_ids[METRIC_RECORD_ID]);
    if (!rec_inst) {
      err_code = ENOTSUP;  // FIXME: implement resize
      goto out2;
    }
    ldms_mval_t str_handle = ldms_record_metric_get(rec_inst, schema_metric_record_ids[METRIC_NID_ID]);
    snprintf(str_handle->a_char, MAXMETRICNAMESIZE, "%s", nid);
    str_handle = ldms_record_metric_get(rec_inst, schema_metric_record_ids[METRIC_STATE_ID]);
    snprintf(str_handle->a_char, MAXMETRICNAMESIZE, "%s", state);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_REFS_ID], refs);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_LAST_ID], last);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_MAX_ID], max);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_RTR_ID], rtr);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_MIN_RTR_ID], min_rtr);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_TX_ID], tx);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_MIN_TX_ID], min_tx);
    ldms_record_set_u64(rec_inst, schema_metric_record_ids[METRIC_QUEUE_ID], queue);
    ldms_list_append_record(metric_set, handle, rec_inst);
  }

out2:
  ldms_transaction_end(metric_set);
out1:
  fclose(sf);
  return err_code;
}


static int config(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s :%s() called\n", SAMP, SUB_SAMP, __func__);
  struct lnet_extra *extra = malloc(sizeof(struct lnet_extra));
  if (extra == NULL) {
    log_fn(LDMSD_LERROR, "%s %s %s: out of memory\n", SAMP, SUB_SAMP, __func__);
    return ENOMEM;
  }
  extra->metric_set = NULL;
  self->extra = extra;
  log_fn(LDMSD_LDEBUG, "%s %s %s: exiting normally\n", SAMP, SUB_SAMP, __func__);
  return 0;
}


static int sample(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s %s :%s() called\n", SAMP, SUB_SAMP, __func__);
  struct lnet_extra *extra = self->extra;
  if (self->schema == NULL) {
    log_fn(LDMSD_LDEBUG, "%s %s %s: calling schema init\n", SAMP, SUB_SAMP, __func__);
    if (local_schema_init(self) < 0) {
      log_fn(LDMSD_LERROR, "%s %s %s: general schema create failed\n", SAMP, SUB_SAMP, __func__);
      return ENOMEM;
    }
  }
  return local_sample(self);
}


static void term(fulldump_sub_ctxt_p self)
{
  log_fn(LDMSD_LDEBUG, "%s  term() called\n", SAMP);
  struct lnet_extra *extra = self->extra;
  fulldump_general_destroy_set(extra->metric_set);
  fulldump_general_schema_fini(self);
}


int lustre_fulldump_lnet_peers_config(fulldump_sub_ctxt_p self)
{
  self->sample = (int (*)(void *self)) sample;
  self->term = (int (*)(void *self)) term;
  return config(self);
}
