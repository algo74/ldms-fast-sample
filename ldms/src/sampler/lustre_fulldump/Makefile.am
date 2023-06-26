liblustre_fulldump_la_SOURCES = \
        lustre_fulldump.c \
				lustre_fulldump_general.c \
				lustre_fulldump_osc_general.c \
				lustre_fulldump_osc_stats.c \
        lustre_fulldump_llite.c \
        lustre_fulldump.h \
				lustre_fulldump_general.h \
				lustre_fulldump_osc_general.h \
				lustre_fulldump_osc_stats.h \
        lustre_fulldump_llite.h
liblustre_fulldump_la_LIBADD = \
	$(top_builddir)/ldms/src/core/libldms.la \
	$(top_builddir)/lib/src/coll/libcoll.la \
	$(top_builddir)/ldms/src/sampler/libsampler_base.la \
	$(top_builddir)/ldms/src/sampler/libldms_compid_helper.la \
	$(top_builddir)/ldms/src/sampler/libjobid_helper.la

liblustre_fulldump_la_LDFLAGS = \
	-no-undefined \
        -export-symbols-regex 'get_plugin' \
        -version-info 1:0:0
liblustre_fulldump_la_CPPFLAGS = \
	@OVIS_INCLUDE_ABS@

pkglib_LTLIBRARIES = liblustre_fulldump.la

dist_man7_MANS = Plugin_lustre_fulldump.man
