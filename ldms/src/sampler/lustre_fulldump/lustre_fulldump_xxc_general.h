/* -*- c-basic-offset: 2 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

/**
 * @file lustre_fulldump_xxc_general.h
 * @brief Header file for lustre_fulldump_xxc_general.c
 * @details
 * This file contains the declarations for the functions that are used
 * by OSC sub-samplers and MDC samplers as well.
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

enum node_type {
  NODE_TYPE_SERVER,
  NODE_TYPE_FS,
  NODE_TYPE_SINGLE_SOURCE,
  NODE_TYPE_LEGACY,
};

// struct source_data;

struct source_data {
  char *name;        /* unique name of the source; freed when node destroyed */
  char *fs_name;     /* name of the filesystem; freed when node destroyed */
  int server_idx;
  char *server_id;   /* id of the server as string; freed when node destroyed */
  // char *dir_path;    /* path to the directory; freed when node destroyed TODO: get rid of it as it is unused */
  char *file_path;   /* path to the file; freed when node destroyed */
  ldms_set_t metric_set; /* a pointer; freed when node destroyed */
  struct rbn tree_node;
};

struct xxc_extra {
  union {
    struct rbt source_tree;             /* red-black tree root for sources */
    struct source_data single_source;   /* data if only one source */
  };
  const char *source_category;  /* filename of the source; borrowed, someone else should free after destroying */
  const char *source_root;      /* root directory of the source; borrowed, someone else should free after destroying */
  int dir_once_log;       /* flag to log missing directory once */
  /* extra fields used  by fd_general_stats */
  // TODO: implement to use uniformly
  enum node_type type;
  int (*single_sample)(fulldump_sub_ctxt_p self, struct source_data *source, void *virtual_args);
  void *virtual_args;     /* arguments for single_sample; borrowed, someone else should free after destroying */
};

int xxc_legacy_extra_config(fulldump_sub_ctxt_p self, const char *source_category, const char *source_root);

int xxc_extra_config(fulldump_sub_ctxt_p self,
                     const char *source_category,
                     const char *source_root,
                     enum node_type type,
                     int (*single_sample)(fulldump_sub_ctxt_p self, struct source_data *source, void *virtual_args),
                     void *virtual_args);

void xxc_general_term(fulldump_sub_ctxt_p self);
void xxc_general_multisource_term(fulldump_sub_ctxt_p self);
void multisource_destroy(struct rbt *source_tree);

ldms_set_t xxc_general_set_create(fulldump_sub_ctxt_p self, struct source_data *node);


/** 
 * @brief Refresh the servers "list".
 * 
 * List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 * 
 * NOTE: we use the red-black tree to store the servers. It may be an overkill.
 * 
 * @param[in,out] source_tree The red-black tree that contains the servers
 * @param[in] self The pointer to the sub-context structure
 * @param[in] path The path to the directory that contains the servers
 */
int multisource_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self, enum node_type type);
int xxc_legacy_servers_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self);

void xxc_general_sample(fulldump_sub_ctxt_p self);

/** 
 * @brief Sample the servers
 * 
 * Sample all the servers in the source tree.
 * 
 * @param[in] xxc_extra The extra data of the sub-sampler
 * @param[in] single_sample The function pointer to the "local" sample function that will be called for each server with the file path and the metric set to use
 */
void xxc_legacy_sample(struct xxc_extra *xxc_extra, int (*single_sample)(const char *, ldms_set_t));

#endif /* __LUSTRE_FULLDUMP_OSC_GENERAL_H */