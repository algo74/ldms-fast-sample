/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
// #include <limits.h>
// #include <string.h>
// #include <dirent.h>
// #include <coll/rbt.h>
#include <sys/queue.h>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "config.h"
#include "lustre_fulldump.h"
#include "jobid_helper.h"

#include "lustre_fulldump_xxc_general.h"
#include "lustre_fd-general_stats.h"
#include "lustre_fulldump_lnet_peers.h"


#define _GNU_SOURCE

#define SAMPLER_NAME "lustre_mdgs_fulldump"

struct fulldump_ctxt samp_ctxt = {0};


static int _mdgs_fuldump_mds_mdt_stats(fulldump_sub_ctxt_p self)
{
  fd_general_stats_config(self, "/sys/kernel/debug/lustre/mds/MDS/mdt", NODE_TYPE_SINGLE_SOURCE, "lustre_mdgs_fuldump_msd_mdt_stats");
}


static int _mdgs_fuldump_mds_mdt_readpage_stats(fulldump_sub_ctxt_p self)
{
  fd_general_stats_config(self, "/sys/kernel/debug/lustre/mds/MDS/mdt_readpage", NODE_TYPE_SINGLE_SOURCE, "lustre_mdgs_fuldump_mds_mdt_readpage_stats");
}


static int _mdgs_fuldump_mds_mdt_md_stats(fulldump_sub_ctxt_p self)
{
  fd_general_stats_config_flex(self, "/proc/fs/lustre/mdt", NODE_TYPE_SERVER, "lustre_mdgs_fuldump_mds_mdt_md_stats", "md_stats");
}


static int _config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
  log_fn(LDMSD_LDEBUG, "%s config() called\n", SAMP);
  if (samp_ctxt.configured) {
    log_fn(LDMSD_LERROR, "%s: config: sampler is already configured.\n", SAMP);
    return EINVAL;

  }
	char *ival = av_value(avl, "producer");
	if (ival) {
    if (strlen(ival) < sizeof(samp_ctxt.producer_name)) {
      strncpy(samp_ctxt.producer_name, ival, sizeof(samp_ctxt.producer_name));
    } else {
      log_fn(LDMSD_LERROR, "%s: config: producer name too long.\n", SAMP);
      return EINVAL;

    }
  }
	(void)base_auth_parse(avl, &samp_ctxt.auth, log_fn);
	int jc = jobid_helper_config(avl);
    if (jc) {
    log_fn(LDMSD_LERROR,
           "%s: set name for job_set is too long.\n",
           SAMP);
    return jc;

	}
	int cc = comp_id_helper_config(avl, &samp_ctxt.cid);
    if (cc) {
		  log_fn(LDMSD_LERROR, "%s: value of component_id is invalid.\n");
		  return cc;
	  }

  //TODO: configure sub-contexts
  int err = add_sub_ctxt(&samp_ctxt, _mdgs_fuldump_mds_mdt_stats);
  if (err) return err;
  err = add_sub_ctxt(&samp_ctxt, _mdgs_fuldump_mds_mdt_readpage_stats);
  if (err) return err;
  err = add_sub_ctxt(&samp_ctxt, _mdgs_fuldump_mds_mdt_md_stats);
  if (err) return err;

  // err = add_sub_ctxt(&samp_ctxt, lustre_oss_fd_ldiskfs_brw_stats_config);
  // if (err) return err;

  err = add_sub_ctxt(&samp_ctxt, lustre_fulldump_lnet_peers_config);
  if (err) return err;

  log_fn(LDMSD_LDEBUG, "%s config() all done\n", SAMP);
  samp_ctxt.configured = true;
  return 0;
}


static int _sample(struct ldmsd_sampler *self)
{
  return sample_contexts(self, &samp_ctxt);
}


static void _term(struct ldmsd_plugin *self)
{
  return term_contexts(self, &samp_ctxt);
}

static ldms_set_t _get_set(struct ldmsd_sampler *self)
{
  return NULL;
}

static const char *_usage(struct ldmsd_plugin *self)
{
  log_fn(LDMSD_LDEBUG, "%s usage() called\n", SAMP);
  return "TODO: ";
}

static struct ldmsd_sampler llite_plugin = {
    .base = {
        .name = SAMPLER_NAME,
        .type = LDMSD_PLUGIN_SAMPLER,
        .term = _term,
        .config = _config,
        .usage = _usage,
    },
    .get_set = _get_set,
    .sample = _sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
  log_fn = pf;
  SAMP = SAMPLER_NAME;
  log_fn(LDMSD_LDEBUG, "%s get_plugin() called\n", SAMP);
  gethostname(samp_ctxt.producer_name, sizeof(samp_ctxt.producer_name));

  return &llite_plugin.base;
}