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

/**
 * @brief initialize the schema for a "general" sub-sampler
 * @param self The pointer to the sub-context structure
 * @param schema_name The name of the schema
 * @param schema_template The template for the schema
 * @param schema_ids The array of schema ids
 * @param metric_record_template The template for the metric record
 * @param metric_record_ids The array of metric record ids
 * @param record_idx The index of the record in the schema template
 * @param list_idx The index of the list in the schema template
 * @param maxlistsize The maximum size of the list
 * @return 0 on success
 * 
 * The "general" schema contains a list of metrics.
 * The initialization function creates the schema using a template of the schema and the template of its metric record.
 * The schema should have a position for the record and a position for the list of metrics.
 * The template of the schema is adjusted to include the size of the list of metrics.
 * The schema is stored in the sub-context structure.
 * 
 * NOTE: the schema is usually created during sample call if it does not exist.
*/
int fulldump_general_schema_init(fulldump_sub_ctxt_p self, const char *schema_name,
                                 struct ldms_metric_template_s *schema_template, int *schema_ids,
                                 struct ldms_metric_template_s *metric_record_template, int *metric_record_ids,
                                 int record_idx, int list_idx, size_t maxlistsize);

// FIXME: This function is not defined and probably not used in the code
// int fulldump_flat_schema_init(fulldump_sub_ctxt_p self, char *schema_name, comp_id_t cid,
//                               struct ldms_metric_template_s *schema_template, int *schema_ids);

/**
 * @brief finalize the schema for a "general" sub-sampler.
 *
 * The function destroys the schema stored in the sub-context structure.
 *
 * @param self The pointer to the sub-context structure
 */
void fulldump_general_schema_fini(fulldump_sub_ctxt_p self);

/**
 * @brief create a set for a "general" sub-sampler for the given schema
 * @param log_fn The log function
 * @param producer_name The name of the producer
 * @param instance_name The name of the instance
 * @param auth The authentication data
 * @param cid The component ID data
 * @param schema The schema for the set
 * @return The pointer to the created set
 *          NULL if the set creation fails (then the error code is in errno)
 * @details
 * NOTE: a schema can have several sets if needed.
 */
ldms_set_t fulldump_general_create_set(ldmsd_msg_log_f log_fn,
                                       const char *producer_name,
                                       const char *instance_name,
                                       const struct base_auth *auth,
                                       const comp_id_t cid,
                                       const ldms_schema_t schema);

void fulldump_general_destroy_set(ldms_set_t set);

/**
 * \brief Split the OSC name into the filesystem name and the OST id
 * \param[in] osc_name The full server name (eg. testfs-OST0000-osc-ffff9dec70092000)
 * \param[out] fs_name The filesystem name (eg. testfs)
 * \param[out] server_idx The index of the server (eg. 0 for OST0000)
 * \param[out] server_id The id of the server (eg. OST0000)
 * \return 3 on success TODO: refactor (it used to be number of dashes in the name)
 * \return or  0 if the name is not in the expected format
 * \return or -1 on allocation error
 * \return or -2 if the OSC name is not in the expected format
 */
int fulldump_split_server_name(char *osc_name, char **fs_name, int *server_idx, char **server_name);

/**
 * \brief make sure that the directory exists and attempt to update if needed.
 * 
 *  Different versions of Lustre put files in different place.
 * 
 * \param[in,out] current_path The current path to the directory which will be updated if needed
 * \param[in] path_options The possible paths to the directory
 * \return 1 if the path was not updated, 2 if it was updated, 0 on error
 */
int update_existing_path(const char **current_path, const char *const *path_options, size_t paths_len);

/**
 * \brief check if a line is empty
 * \param[in] line The line to check (must be null terminated)
 * \return 1 if the line is empty, 0 otherwise
*/
int empty_line(char *line);

/**
 * \brief compare two strings ignoring space areas differences
 * \param[in] s1 The first string
 * \param[in] s2 The second string
 * \return 1 if the strings are equal ignoring spaces, 0 otherwise
 */
int equal_strings_ignoring_spaces(const char *s1, const char *s2);

#endif /* __LUSTRE_FULLDUMP_GENERAL_H */
