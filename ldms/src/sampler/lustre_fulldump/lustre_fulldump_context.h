#ifndef _LUSTRE_FULLDUMP_CONTEXT_H
#define _LUSTRE_FULLDUMP_CONTEXT_H

#include <sys/queue.h>

#include "comp_id_helper.h"
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

LIST_HEAD(fulldump_sub_ctxt_list, fulldump_sub_ctxt);

typedef struct fulldump_ctxt {
  char producer_name[LDMS_PRODUCER_NAME_MAX];
  struct comp_id_data cid;
  struct base_auth auth;
  int configured;  // bool
  struct fulldump_sub_ctxt_list sub_ctxt_list;
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

int add_sub_ctxt(fulldump_ctxt_p samp_ctxt, int (*sub_ctxt_config)(struct fulldump_sub_ctxt *));
int sample_contexts(struct ldmsd_sampler *self, fulldump_ctxt_p sampl_ctxt);
void term_contexts(struct ldmsd_plugin *self, fulldump_ctxt_p sampl_ctxt);

#endif /* _LUSTRE_FULLDUMP_CONTEXT_H */