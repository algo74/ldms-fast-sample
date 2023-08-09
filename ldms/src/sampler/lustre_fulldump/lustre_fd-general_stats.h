/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#ifndef FULLDUMP_GENERAL_STATS_H
#define FULLDUMP_GENERAL_STATS_H

// #include <limits.h>
// #include <string.h>
// #include <unistd.h>

#include "consts.h"
#include "ldms.h"
#include "lustre_fulldump_context.h"
#include "lustre_fulldump_xxc_general.h"

// #include "lustre_fulldump_general.h"
// #include "lustre_fulldump_xxc_general.h"

// #define _GNU_SOURCE

#define MAXMETRICNAMESIZE 128
#define MAXLISTSIZE 256

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern char *SAMP;

enum {
  METRIC_NAME_ID,
  METRIC_COUNT_ID,
  METRIC_SUM_ID,
  METRIC_SUM2_ID,
};
#define METRIC_RECORD_LEN (METRIC_SUM2_ID+2)
extern struct ldms_metric_template_s fd_general_stats_schema_metric_record_templlate[];

/* metric templates for the set schema */
enum {
  METRIC_RECORD_ID,
  FS_NAME_ID,
  INDEX_ID,
  SNAPSHOT_SEC_ID,
  SNAPSHOT_NS_ID,
  METRIC_LIST_ID
};
#define METRIC_SCHEMA_LEN (METRIC_LIST_ID + 2)
extern struct ldms_metric_template_s fd_general_stats_schema_templlate[];

struct fd_general_stats_sample_args {
  const char *schema_name;
  // const char *set_name_suffix;
  int *schema_ids;
  int *schema_metric_record_ids;
};


int fd_general_stats_config(fulldump_sub_ctxt_p self, const char *path, enum node_type type,
                            const char *metric_name);

int fd_general_stats_config_flex(fulldump_sub_ctxt_p self, const char *path, enum node_type type,
                                 const char *metric_name, const char* stats_name);

#endif /* FULLDUMP_GENERAL_STATS_H */