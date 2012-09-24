include $(top_srcdir)/build/vars.build.mk

geanyplugins_LTLIBRARIES = snippetsmenu.la

snippetsmenu_la_SOURCES = \
	plugin.c

snippetsmenu_la_CFLAGS = $(AM_CFLAGS) $(LIBXML_CFLAGS) -DHAVE_GLIB -DHAVE_LIBXML
snippetsmenu_la_LIBADD = $(COMMONLIBS) $(LIBXML_LIBS)

include $(top_srcdir)/build/cppcheck.mk
