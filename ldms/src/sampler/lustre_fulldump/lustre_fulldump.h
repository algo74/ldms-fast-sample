/* -*- c-basic-offset: 2 -*- */
/*
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_FULLDUMP_H
#define __LUSTRE_FULLDUMP_H

#include <sys/queue.h>
// #include "jobid_helper.h"
#include "comp_id_helper.h"
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#define SAMP "lustre_fulldump"

#define MAXNAMESIZE 64

extern ldmsd_msg_log_f log_fn;

typedef struct fulldump_ctxt {
  char producer_name[LDMS_PRODUCER_NAME_MAX];
  struct comp_id_data cid;
  struct base_auth auth;
  int configured; // bool
} *fulldump_ctxt_p;

typedef struct fulldump_sub_ctxt {
  LIST_ENTRY(fulldump_sub_ctxt) link;
  struct fulldump_ctxt *sampl_ctxt_p;
  struct comp_id_data cid;
  ldms_schema_t schema;
  void *extra;
  int (*sample)(void *self);
  int (*term)(void *self);
} *fulldump_sub_ctxt_p;

#endif /* __LUSTRE_FULLDUMP_H */