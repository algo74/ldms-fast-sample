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

#ifndef __LUSTRE_FULLDUMP_XXC_HF_HIST_H
#define __LUSTRE_FULLDUMP_XXC_HF_HIST_H

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
#include "high_freq_sampler.h"

// #define _GNU_SOURCE

struct source_data;

struct hf_data_hanldler {
  void* (*create)(void);
  void (*destroy)(void *data);
  void (*on_sample)(struct source_data *server);
  void (*on_timer)(struct source_data *server);
};

struct xxc_extra {
  pthread_mutex_t mutex;
  struct rbt source_tree; /* red-black tree root for sources */
  char *source_category;  /* filename of the source */
  ldms_schema_t hf_schema;
  struct comp_id_data hf_cid;
  struct hf_data_hanldler *hf_data_handler;
  struct tsampler_timer hf_timer;
};

struct source_data {
  char *name;
  char *fs_name;
  int server_idx;
  char *server_id;
  char *dir_path;
  char *file_path;
  ldms_set_t metric_set; /* a pointer to hist metric set */
  ldms_set_t hf_metric_set; /* a pointer */
  void *hf_data;
  struct rbn tree_node;
};

int hf_hist_extra_config(fulldump_sub_ctxt_p self, char *source_category, struct hf_data_hanldler *handler);

int hf_schema_init(fulldump_sub_ctxt_p self, char *schema_name, struct ldms_metric_template_s *schema_template, int *schema_ids);

void hf_hist_term(fulldump_sub_ctxt_p self);

void hf_hist_servers_destroy(struct rbt *source_tree, fulldump_sub_ctxt_p self);

/** List subdirectories to get all metric files.
 * Create data structures for any file that we
 * have not seen, and delete any that we no longer see.
 */
int hf_hist_servers_refresh(struct rbt *source_tree, fulldump_sub_ctxt_p self, const char *path);

void hf_servers_sample(struct xxc_extra *extra);

void hist_servers_sample(struct xxc_extra *extra, int (*single_sample)(const char *, ldms_set_t));

#endif /* __LUSTRE_FULLDUMP_XXC_HF_HIST_H */