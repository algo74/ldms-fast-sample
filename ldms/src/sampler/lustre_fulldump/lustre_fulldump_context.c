/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <coll/rbt.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "config.h"
#include "ldms_missing.h"
#include "lustre_fulldump.h"
#include "lustre_fulldump_llite.h"
#include "lustre_fulldump_lnet_peers.h"
#include "lustre_fulldump_mdc_md_stats.h"
#include "lustre_fulldump_mdc_rpc_stats.h"
#include "lustre_fulldump_mdc_stats.h"
#include "lustre_fulldump_mdc_timeouts.h"
#include "lustre_fulldump_osc_rpc_stats.h"
#include "lustre_fulldump_osc_stats.h"
#include "lustre_fulldump_osc_timeouts.h"


static struct fulldump_sub_ctxt *create_sub_ctxt(struct fulldump_ctxt *samp_ctxt)
{
  struct fulldump_sub_ctxt *sub_ctxt_p = calloc(1, sizeof(struct fulldump_sub_ctxt));
  if (!sub_ctxt_p) {
    log_fn(LDMSD_LERROR, "%s: create_sub_ctxt: calloc failed.\n", SAMP);
    return NULL;
  }
  sub_ctxt_p->sampl_ctxt_p = samp_ctxt;
  sub_ctxt_p->cid = samp_ctxt->cid;
  // sub_ctxt_p->extra = NULL;
  // sub_ctxt_p->sample = NULL;
  // sub_ctxt_p->term = NULL;
  // sub_ctxt_p->schema = NULL;
  return sub_ctxt_p;
}


int add_sub_ctxt(fulldump_ctxt_p samp_ctxt, int (*sub_ctxt_config)(struct fulldump_sub_ctxt *))
{
  struct fulldump_sub_ctxt *sub_ctxt_p = create_sub_ctxt(samp_ctxt);
  if (!sub_ctxt_p) {
    log_fn(LDMSD_LERROR, "%s: config: create_sub_ctxt failed.\n", SAMP);
    return EINVAL;
  }
  int err = sub_ctxt_config(sub_ctxt_p);
  if (err) {
    log_fn(LDMSD_LERROR, "%s: config: lustre_fulldump_llite_config failed.\n", SAMP);
    return err;
  }
  LIST_INSERT_HEAD(&samp_ctxt->sub_ctxt_list, sub_ctxt_p, link);
  return 0;
}


int sample_contexts(struct ldmsd_sampler *self, fulldump_ctxt_p sampl_ctxt)
{
  log_fn(LDMSD_LDEBUG, "%s sample() called\n", SAMP);
  int err = 0;
  struct fulldump_sub_ctxt *sub_ctxt_p;
  LIST_FOREACH(sub_ctxt_p, &sampl_ctxt->sub_ctxt_list, link) {
    log_fn(LDMSD_LDEBUG, "%s calling sub_ctxt sample()\n", SAMP);
    err = sub_ctxt_p->sample(sub_ctxt_p);
    if (err)
      log_fn(LDMSD_LERROR, "%s: sample: sub_ctxt sample failed; err code %d.\n", SAMP, err);
  }
  return 0;
}


void term_contexts(struct ldmsd_plugin *self, fulldump_ctxt_p sampl_ctxt)
{
  log_fn(LDMSD_LDEBUG, "%s term() called\n", SAMP);
  struct fulldump_sub_ctxt *sub_ctxt_p = LIST_FIRST(&sampl_ctxt->sub_ctxt_list);
  while (sub_ctxt_p) {
    sub_ctxt_p->term(sub_ctxt_p);
    struct fulldump_sub_ctxt *next_sub_ctxt_p = LIST_NEXT(sub_ctxt_p, link);
    free(sub_ctxt_p);
    sub_ctxt_p = next_sub_ctxt_p;
  }
  LIST_INIT(&sampl_ctxt->sub_ctxt_list);
}