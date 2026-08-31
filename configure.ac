AC_INIT([src/ldm.c],[gtkgreet/ldmgtkgreeter.c])
AC_CONFIG_HEADER(config.h)
AC_CONFIG_MACRO_DIR([m4])

PACKAGE=ldm
VERSION=2.18.06
AUTHOR=sbalneav@ltsp.org

AM_INIT_AUTOMAKE([$PACKAGE],[$VERSION],[$AUTHOR])

AM_MAINTAINER_MODE([enable])

AC_PROG_CC
AM_PROG_CC_C_O()
AC_PROG_INSTALL
AC_STDC_HEADERS

# Security hardening flags. ldm runs as root and dlopen()s plugins, so this
# matters more than for most small projects. A real .deb build would get
# these for free from dpkg-buildflags/debhelper, but there's no debian/
# directory in this tree any more, so a plain ./configure && make gets none
# of it otherwise. -fPIE/-pie are deliberately left out here: they'd have to
# be applied per-executable-target (not blanket CFLAGS/LDFLAGS), since
# combining them with the -fPIC shared-library plugin builds risks breaking
# those.
CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=2 -fstack-protector-strong"
LDFLAGS="$LDFLAGS -Wl,-z,relro -Wl,-z,now"

AC_HEADER_STDC
AC_CHECK_HEADERS([syslog.h sys/wait.h fcntl.h],,AC_MSG_ERROR(Required header missing))

AC_CHECK_LIB([util],[main],[],AC_MSG_ERROR(ldm requires libutil))

PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.6.0)
PKG_CHECK_MODULES(GOBJECT, gobject-2.0 >= 2.6.0)
PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.22)

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GOBJECT_CFLAGS)
AC_SUBST(GOBJECT_LIBS)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

AM_PROG_LIBTOOL
IT_PROG_INTLTOOL([0.35.0])

AC_SUBST(ALL_LINGUAS)
AM_GLIB_GNU_GETTEXT
GETTEXT_PACKAGE=$PACKAGE
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])
AC_SUBST(GETTEXT_PACKAGE)

AC_OUTPUT([
Makefile
src/Makefile
src/plugins/ssh/Makefile
src/plugins/xfreerdp/Makefile
share/Makefile
man/Makefile
po/Makefile.in
gtkgreet/Makefile
wwm/Makefile
misc/Makefile
themes/Makefile
screen.d/Makefile
rc.d/Makefile
share/ldm-script
ltsp-cluster-info/Makefile
po/rc.d/Makefile.in
po/rc.d/Makefile
po/ltsp-cluster-info/Makefile.in
po/ltsp-cluster-info/Makefile
])
