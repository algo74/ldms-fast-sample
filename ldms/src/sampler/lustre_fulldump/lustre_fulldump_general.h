/* -*- c-basic-offset: 8 -*- */
/*
 * Copyright 2023 University of Central Florida
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_FULLDUMP_GENERAL_H
#define __LUSTRE_FULLDUMP_GENERAL_H

// #include "ldms.h"
// #include "ldmsd.h"

#include "lustre_fulldump.h"

int fulldump_general_schema_init(fulldump_sub_ctxt_p self, char *schema_name,
                                 struct ldms_metric_template_s *schema_template, int *schema_ids,
                                 struct ldms_metric_template_s *metric_record_template, int *metric_record_ids,
                                 int record_idx, int list_idx, size_t maxlistsize);

void fulldump_general_schema_fini(fulldump_sub_ctxt_p self);

ldms_set_t fulldump_general_create_set(ldmsd_msg_log_f log_fn,
                                       const char *producer_name,
                                       const char *instance_name,
                                       const struct base_auth *auth,
                                       const comp_id_t cid,
                                       const ldms_schema_t schema);

void fulldump_general_destroy_set(ldms_set_t set);

/**
 * \brief Split the OSC name into the filesystem name and the OST id
 * \param[in] osc_name The full OSC name
 * \param[out] fs_name The filesystem name
 * \param[out] ost_id The OST id
 * \return number of dashes found in the OSC name (3 is success)
 * \return or -1 on allocation error
 * \return or -2 if the OSC name is not in the expected format
 */
int fulldump_split_osc_name(char *osc_name, char **fs_name, int *ost_id);

/**
 * \brief make sure that the directory exists and attempt to update if needed.
 *  Different versions of Lustre put files in different place.
 *  \return 1 if the path was not updated, 2 if it was updated, 0 on error
 */
int update_existing_path(const char **current_path, const char *const *path_options, size_t paths_len);

#endif /* __LUSTRE_FULLDUMP_GENERAL_H */
