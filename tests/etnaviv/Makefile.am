AM_CFLAGS = \
	-I $(top_srcdir)/include/drm \
	-I $(top_srcdir)/libkms/ \
	-I $(top_srcdir)/etnaviv \
	-I $(top_srcdir)

noinst_PROGRAMS = \
	etnaviv_2d_test \
	etnaviv_cmd_stream_test

etnaviv_2d_test_LDADD = \
	$(top_builddir)/libdrm.la \
	$(top_builddir)/libkms/libkms.la \
	$(top_builddir)/etnaviv/libdrm_etnaviv.la

etnaviv_2d_test_SOURCES = \
	etnaviv_2d_test.c \
	write_bmp.c

etnaviv_cmd_stream_test_LDADD = \
	$(top_builddir)/etnaviv/libdrm_etnaviv.la

etnaviv_cmd_stream_test_SOURCES = \
	etnaviv_cmd_stream_test.c
