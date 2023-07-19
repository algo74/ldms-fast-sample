/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

/**
 * @file lustre_fulldump_xxc_general.h
 * @brief Header file for lustre_fulldump_osc_general.c
 * @details
 * This file contains the declarations for the functions that are used
 * by OSC sub-samplers and MDC samplers as well.
 * TODO: so the name may be misleading.
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


struct xxc_extra {
  struct rbt source_tree; /* red-black tree root for sources */
  char *source_category;  /* filename of the source */
};

struct server_data {
  char *name;
  char *fs_name;
  int server_idx;
  char *server_id;
  char *dir_path;
  char *file_path;
  ldms_set_t metric_set; /* a pointer */
  struct rbn tree_node;
};

int server_extra_config(fulldump_sub_ctxt_p self, char *source_category);

void server_general_term(fulldump_sub_ctxt_p self);

void servers_destroy(struct rbt *source_tree);

/** List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 */
int servers_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self, const char *path);

void servers_sample(struct xxc_extra *xxc_extra, int (*single_sample)(const char *, ldms_set_t));

#endif /* __LUSTRE_FULLDUMP_OSC_GENERAL_H */