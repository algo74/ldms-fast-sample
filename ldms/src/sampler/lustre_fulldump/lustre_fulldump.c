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
#include "lustre_fulldump_lnet_peers.h"
#include "lustre_fulldump_mdc_md_stats.h"
#include "lustre_fulldump_mdc_rpc_stats.h"
#include "lustre_fulldump_mdc_stats.h"
#include "lustre_fulldump_mdc_timeouts.h"
#include "lustre_fulldump_osc_rpc_stats.h"
#include "lustre_fulldump_osc_stats.h"
#include "lustre_fulldump_osc_timeouts.h"

// #include "jobid_helper.h"

#define _GNU_SOURCE

/**
 * samp_ctxt is the global context for the fulldump sampler
*/
struct fulldump_ctxt samp_ctxt = {0};
/**
 * sub_ctxt_list is the global linked list of sub-contexts for the fulldump sampler
 */
LIST_HEAD(fulldump_sub_ctxt_list, fulldump_sub_ctxt);
struct fulldump_sub_ctxt_list sub_ctxt_list = {NULL};

// extern ldmsd_msg_log_f log_fn;

/**
 * @brief create_sub_ctxt creates a sub-context structure and initializes it with the given fulldump_ctxt
 * @param samp_ctxt The pointer to the fulldump_ctxt
 * @return The pointer to the created sub-context structure
 *         NULL if the memory allocation fails
 * The sub-context structure is allocated using calloc, so all the fields are initialized to 0
 * The sampl_ctxt_p field is set to the given fulldump_ctxt
 * The cid field gets a copy of the cid field of the given fulldump_ctxt
*/
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

/**
 * @brief add_sub_ctxt creates (using create_sub_ctxt) and adds a sub-context to the sub_ctxt_list
 * @param sub_ctxt_config The configuration function for the sub-context
 * @return 0 if the sub-context is successfully added
 *         error code otherwise
 */
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

/**
 * @brief config is a part of the plugin interface; it is called when the sampler is being configured
*/
static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
  log_fn(LDMSD_LDEBUG, SAMP" config() called\n");
  if (samp_ctxt.configured) {
    log_fn(LDMSD_LERROR, SAMP": config: sampler is already configured.\n");
    return EINVAL;

  }
  // get the producer name and check if it is not too long
  // store the producer name in the samp_ctxt
	char *ival = av_value(avl, "producer");
	if (ival) {
    if (strlen(ival) < sizeof(samp_ctxt.producer_name)) {
      strncpy(samp_ctxt.producer_name, ival, sizeof(samp_ctxt.producer_name));
    } else {
      log_fn(LDMSD_LERROR, SAMP ": config: producer name too long.\n");
      return EINVAL;

    }
  // NOTE: if the producer name is not provided, the hostname is used as the producer name (see get_plugin() function). So, this else block is not needed.
  // } else {
  //   log_fn(LDMSD_LERROR, SAMP ": config: could not get producer name.\n");
  //   return EINVAL;
  //
  }
  // parse the authorization information using base_auth_parse from sampler_base.h
	(void)base_auth_parse(avl, &samp_ctxt.auth, log_fn);
	int jc = jobid_helper_config(avl);
    if (jc) {
		  log_fn(LDMSD_LERROR, SAMP": set name for job_set"
			" is too long.\n");
		return jc;

	}
  // parse the component ID using comp_id_helper_config from comp_id_helper.h
	int cc = comp_id_helper_config(avl, &samp_ctxt.cid);
    if (cc) {
		  log_fn(LDMSD_LERROR, SAMP": value of component_id"
			" is invalid.\n");
		return cc;

	}

  //TODO: configure other sub-contexts
  int err = add_sub_ctxt(lustre_fulldump_llite_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_lnet_peers_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_mdc_md_stats_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_mdc_rpc_stats_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_mdc_stats_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_mdc_timeouts_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_osc_rpc_stats_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_osc_stats_config);
  if (err) return err;
  err = add_sub_ctxt(lustre_fulldump_osc_timeouts_config);
  if (err) return err;


  log_fn(LDMSD_LDEBUG, SAMP" config() all done\n");
  samp_ctxt.configured = true;
  return 0;
}


static int sample(struct ldmsd_sampler *self)
/*
  The logic of the sample() function is simple: it calls the sample() function of each sub-context in the sub_ctxt_list untill the end of the list is reached or an error occurs. If an error occurs, the function returns the error code. If no error occurs, the function returns 0.
*/
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
/*
  The term() function is called when the sampler is being terminated. It calls the term() function of each sub-context in the sub_ctxt_list and frees the memory allocated for the sub-context structure. Then it resets the sub_ctxt_list, which should free the list memory.
*/
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
// TODO: implement
{
        log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "TODO: config name=" SAMP;
}

// TODO: change the name of the struct to be consistent
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