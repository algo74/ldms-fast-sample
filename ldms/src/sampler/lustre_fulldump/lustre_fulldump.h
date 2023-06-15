/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_FULLDUMP_H
#define __LUSTRE_FULLDUMP_H

#include "ldms.h"
#include "ldmsd.h"

#define SAMP "lustre_fulldump"

#define MAXNAMESIZE 64

// extern ldmsd_msg_log_f log_fn;

typedef struct fulldump_ctxt {
  ldmsd_msg_log_f log_fn;
  char producer_name[LDMS_PRODUCER_NAME_MAX];
  struct comp_id_data cid;
  struct base_auth auth;
  bool configured;
} *fulldump_ctxt_p;

typedef struct fulldump_sub_ctxt {
  LIST_ENTRY(fulldump_sub_ctxt) link;
  struct fulldump_ctxt *sampl_ctxt_p;
  struct comp_id_data cid;
  ldms_schema_t schema;
  void *extra;
  int (*sample)(struct void *self);
  int (*term)(struct void *self);
} *fulldump_sub_ctxt_p;

#endif /* __LUSTRE_FULLDUMP_H */
