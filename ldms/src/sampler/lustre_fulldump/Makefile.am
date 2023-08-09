AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@

CORE_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/lib/src/ovis_util/libovis_util.la \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la \
	$(top_builddir)/ldms/src/sampler/libjobid_helper.la

#general functions for all plugins
liblustre_fulldump_general_la_SOURCES = \
				consts.h \
				high_freq_sampler.c \
				high_freq_sampler.h \
				ldms_missing.h \
				ldms_missing.c \
				lustre_fd-general_stats.c \
				lustre_fd-general_stats.h \
				lustre_fulldump_context.h \
				lustre_fulldump_general.c \
				lustre_fulldump_general.h \
				lustre_fulldump_globals.c \
				lustre_fulldump_xxc_general.c \
				lustre_fulldump_xxc_general.h \
				lustre_fulldump_xxc_hf_hist.c \
				lustre_fulldump_xxc_hf_hist.h \
				lustre_fulldump.h

liblustre_fulldump_general_la_LIBADD = $(CORE_LIBADD)
pkglib_LTLIBRARIES = liblustre_fulldump_general.la

# client sampler plugin
liblustre_fulldump_la_SOURCES = \
				consts.h \
				high_freq_sampler.h \
				lustre_fd-general_stats.h \
				lustre_fulldump_context.h \
				lustre_fulldump_general.h \
        lustre_fulldump_llite.c \
        lustre_fulldump_llite.h \
        lustre_fulldump_lnet_peers.c \
        lustre_fulldump_lnet_peers.h \
				lustre_fulldump_mdc_md_stats.c \
				lustre_fulldump_mdc_md_stats.h \
				lustre_fulldump_mdc_rpc_stats.c \
				lustre_fulldump_mdc_rpc_stats.h \
				lustre_fulldump_mdc_stats.c \
				lustre_fulldump_mdc_stats.h \
				lustre_fulldump_mdc_timeouts.c \
				lustre_fulldump_mdc_timeouts.h \
				lustre_fulldump_osc_rpc_stats.c \
				lustre_fulldump_osc_rpc_stats.h \
				lustre_fulldump_osc_stats.c \
				lustre_fulldump_osc_stats.h \
				lustre_fulldump_osc_timeouts.c \
				lustre_fulldump_osc_timeouts.h \
				lustre_fulldump_xxc_general.h \
				lustre_fulldump_xxc_hf_hist.h \
        lustre_fulldump.c \
        lustre_fulldump.h

liblustre_fulldump_la_LIBADD = \
	$(CORE_LIBADD) \
	liblustre_fulldump_general.la

liblustre_fulldump_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'get_plugin' \
        -version-info 1:0:0
# liblustre_fulldump_la_CPPFLAGS = \
# 	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES += liblustre_fulldump.la


# OSS sampler plugin
liblustre_oss_fulldump_la_SOURCES = \
				consts.h \
				lustre_fd-general_stats.h \
				lustre_fulldump_context.h \
				lustre_fulldump_general.h \
        lustre_fulldump.h\
				lustre_oss_fd-osd_ldiskfs_brw_stats.c \
				lustre_oss_fd-osd_ldiskfs_brw_stats.h \
				lustre_oss_fulldump.c

liblustre_oss_fulldump_la_LIBADD = \
	$(CORE_LIBADD) \
	liblustre_fulldump_general.la

liblustre_oss_fulldump_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'get_plugin' \
        -version-info 1:0:0
# liblustre_oss_fulldump_la_CPPFLAGS = \
# 	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES += liblustre_oss_fulldump.la

# MG/DS sampler plugin
liblustre_mdgs_fulldump_la_SOURCES = \
				consts.h \
				lustre_fd-general_stats.h \
				lustre_fulldump_context.h \
				lustre_fulldump_general.h \
        lustre_fulldump.h\
				lustre_mdgs_fulldump.c

liblustre_mdgs_fulldump_la_LIBADD = \
	$(CORE_LIBADD) \
	liblustre_fulldump_general.la

liblustre_mdgs_fulldump_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'get_plugin' \
        -version-info 1:0:0
# liblustre_mdgs_fulldump_la_CPPFLAGS = \
# 	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES += liblustre_mdgs_fulldump.la

dist_man7_MANS = Plugin_lustre_fulldump.man
