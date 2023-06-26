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
#include "lustre_fulldump.h"
#include "lustre_fulldump_llite.h"
#include "lustre_fulldump_osc_stats.h"
// #include "jobid_helper.h"

#define _GNU_SOURCE

struct fulldump_ctxt samp_ctxt = {0};
LIST_HEAD(fulldump_sub_ctxt_list, fulldump_sub_ctxt);
struct fulldump_sub_ctxt_list sub_ctxt_list = {NULL};

// extern ldmsd_msg_log_f log_fn;

struct fulldump_sub_ctxt *create_sub_ctxt(struct fulldump_ctxt *samp_ctxt)
{
  struct fulldump_sub_ctxt *sub_ctxt_p = calloc(1, sizeof(struct fulldump_sub_ctxt));
  if (!sub_ctxt_p) {
    log_fn(LDMSD_LERROR, SAMP": create_sub_ctxt: calloc failed.\n");
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


static int add_sub_ctxt(int (*sub_ctxt_config)(struct fulldump_sub_ctxt *))
{
  struct fulldump_sub_ctxt *sub_ctxt_p = create_sub_ctxt(&samp_ctxt);
  if (!sub_ctxt_p) {
    log_fn(LDMSD_LERROR, SAMP ": config: create_sub_ctxt failed.\n");
    return EINVAL;
  }
  int err = sub_ctxt_config(sub_ctxt_p);
  if (err) {
    log_fn(LDMSD_LERROR, SAMP ": config: lustre_fulldump_llite_config failed.\n");
    return err;
  }
  LIST_INSERT_HEAD(&sub_ctxt_list, sub_ctxt_p, link);
  return 0;
}


static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
  log_fn(LDMSD_LDEBUG, SAMP" config() called\n");
  if (samp_ctxt.configured) {
    log_fn(LDMSD_LERROR, SAMP": config: sampler is already configured.\n");
    return EINVAL;

  }
	char *ival = av_value(avl, "producer");
	if (ival) {
    if (strlen(ival) < sizeof(samp_ctxt.producer_name)) {
      strncpy(samp_ctxt.producer_name, ival, sizeof(samp_ctxt.producer_name));
    } else {
      log_fn(LDMSD_LERROR, SAMP ": config: producer name too long.\n");
      return EINVAL;

    }
  }
	(void)base_auth_parse(avl, &samp_ctxt.auth, log_fn);
	int jc = jobid_helper_config(avl);
    if (jc) {
		  log_fn(LDMSD_LERROR, SAMP": set name for job_set"
			" is too long.\n");
		return jc;

	}
	int cc = comp_id_helper_config(avl, &samp_ctxt.cid);
    if (cc) {
		  log_fn(LDMSD_LERROR, SAMP": value of component_id"
			" is invalid.\n");
		return cc;

	}

  //TODO: configure sub-contexts
  int err = add_sub_ctxt(lustre_fulldump_llite_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_osc_stats_config);
  if (err) return err;

  log_fn(LDMSD_LDEBUG, SAMP" config() all done\n");
  samp_ctxt.configured = true;
  return 0;
}


static int sample(struct ldmsd_sampler *self)
{
  log_fn(LDMSD_LDEBUG, SAMP " sample() called\n");
  int err = 0;
  struct fulldump_sub_ctxt *sub_ctxt_p;
  LIST_FOREACH(sub_ctxt_p, &sub_ctxt_list, link) {
    log_fn(LDMSD_LDEBUG, SAMP " calling sub_ctxt sample()\n");
    err = sub_ctxt_p->sample(sub_ctxt_p);
    if (err)
      return err;
  }
  return err;
}


static void term(struct ldmsd_plugin *self)
{
  log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
  struct fulldump_sub_ctxt *sub_ctxt_p = LIST_FIRST(&sub_ctxt_list);
  while (sub_ctxt_p) {
    sub_ctxt_p->term(sub_ctxt_p);
    struct fulldump_sub_ctxt *next_sub_ctxt_p = LIST_NEXT(sub_ctxt_p, link);
    free(sub_ctxt_p);
    sub_ctxt_p = next_sub_ctxt_p;
  }
  LIST_INIT(&sub_ctxt_list);
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "TODO: config name=" SAMP;
}

static struct ldmsd_sampler llite_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
  log_fn = pf;
  log_fn(LDMSD_LDEBUG, SAMP" get_plugin() called\n");
  gethostname(samp_ctxt.producer_name, sizeof(samp_ctxt.producer_name));

  return &llite_plugin.base;
}