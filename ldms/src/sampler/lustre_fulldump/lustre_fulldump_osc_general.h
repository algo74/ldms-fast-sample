/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_FULLDUMP_OSC_GENERAL_H
#define __LUSTRE_FULLDUMP_OSC_GENERAL_H

#include <coll/rbt.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "comp_id_helper.h"
#include "sampler_base.h"
#include "config.h"
#include "jobid_helper.h"
// #include "ldms.h"
// #include "ldmsd.h"
#include "lustre_fulldump.h"
#include "lustre_fulldump_general.h"

// #define _GNU_SOURCE


struct osc_extra {
  struct rbt source_tree; /* red-black tree root for sources */
  char *source_category;  /* filename of the source */
};

struct osc_data {
  char *name;
  char *fs_name;
  int ost_id;
  char *dir_path;
  char *file_path;
  ldms_set_t metric_set; /* a pointer */
  struct rbn tree_node;
};

int osc_extra_config(fulldump_sub_ctxt_p self, char *source_category);

void osc_general_term(fulldump_sub_ctxt_p self);

void oscs_destroy(struct rbt *osc_tree);

/** List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 */
int oscs_refresh(struct rbt *osc_tree, fulldump_sub_ctxt_p self, const char *path);

void oscs_sample(struct osc_extra *osc_extra, int (*single_sample)(const char *, ldms_set_t));

#endif /* __LUSTRE_FULLDUMP_OSC_GENERAL_H */