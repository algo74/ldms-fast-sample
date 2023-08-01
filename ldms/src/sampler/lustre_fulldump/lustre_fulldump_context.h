#ifndef _LUSTRE_FULLDUMP_CONTEXT_H
#define _LUSTRE_FULLDUMP_CONTEXT_H

#include <sys/queue.h>

#include "comp_id_helper.h"
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

typedef struct fulldump_ctxt {
  char producer_name[LDMS_PRODUCER_NAME_MAX];
  struct comp_id_data cid;
  struct base_auth auth;
  int configured;  // bool
} *fulldump_ctxt_p;

typedef struct fulldump_sub_ctxt {
  LIST_ENTRY(fulldump_sub_ctxt)
  link;
  struct fulldump_ctxt *sampl_ctxt_p;
  struct comp_id_data cid;
  ldms_schema_t schema;
  void *extra;
  int (*sample)(void *self);
  int (*term)(void *self);
} *fulldump_sub_ctxt_p;

#endif /* _LUSTRE_FULLDUMP_CONTEXT_H */