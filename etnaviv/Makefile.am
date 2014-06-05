AM_CFLAGS = \
	$(WARN_CFLAGS) \
	-I$(top_srcdir) \
	$(PTHREADSTUBS_CFLAGS) \
	-I$(top_srcdir)/include/drm

libdrm_etnaviv_ladir = $(libdir)
libdrm_etnaviv_la_LTLIBRARIES = libdrm_etnaviv.la
libdrm_etnaviv_la_LDFLAGS = -version-number 0:0:0 -no-undefined
libdrm_etnaviv_la_LIBADD = ../libdrm.la @PTHREADSTUBS_LIBS@

libdrm_etnaviv_la_SOURCES = \
	etnaviv_device.c \
	etnaviv_bo.c \
	etnaviv_pipe.c \
	etnaviv_cmd_stream.c

libdrm_etnavivincludedir = ${includedir}/libdrm
libdrm_etnavivinclude_HEADERS = etnaviv_drmif.h

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = libdrm_etnaviv.pc
