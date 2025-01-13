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

/** @brief fulldump_ctxt is the context for the fulldump sampler */
typedef struct fulldump_ctxt {
  char producer_name[LDMS_PRODUCER_NAME_MAX]; ///< The name of the producer that will be used in all the sub-samplers
  struct comp_id_data cid; ///< The component ID data; see comp_id_helper.h 
  struct base_auth auth; ///< The base_auth struct; see sampler_base.h  
  int configured; ///< A boolean flag to indicate if the sampler has been configured 
} *fulldump_ctxt_p;

/**
 * @brief fulldump_sub_ctxt is the context for the sub-samplers
 * @param link The link to the next sub-sampler (used for the LIST macros)
 * @param sampl_ctxt_p The pointer to the fulldump_ctxt
 * @param cid The component ID data; for some reason (TODO: investigate) we have to have a separate one for each sub-sampler
 * @param schema The schema for the sub-sampler (works for most of sub-samplers as they have just one schema)
 * @param extra A pointer to extra data that the sub-sampler may need
 * @param sample A function pointer to the sample function for the sub-sampler
 * @param term A function pointer to the term function for the sub-sampler
 * 
 * The sub-samplers are stored in a linked list. 
 * The sample and term functions and extra are set at initialization.
 * The schema is managed by the sub-sampler.
 * sampl_ctxt_p, cid, and link are set at creation.
*/
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