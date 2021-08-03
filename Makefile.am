SUBDIRS = po po/rc.d po/ltsp-cluster-info src man share gtkgreet themes screen.d rc.d misc wwm ltsp-cluster-info
EXTRA_DIST = config.rpath autogen.sh \
	intltool-extract.in 	\
	intltool-merge.in   	\
	intltool-update.in

ACLOCAL_AMFLAGS = -I m4

ldmdir = $(pkgdatadir)
ldm_DATA = locales

locales: generate_locale_list.py
	./generate_locale_list.py locales
