## Process this file with automake to produce Makefile.in

bin_PROGRAMS = ldm-dialog
ldm_dialog_SOURCES = ldm-dialog.c

ldm_dialog_LDADD = $(GTK_LIBS)
AM_CPPFLAGS = $(GTK_CFLAGS) \
              -DLOCALE_DIR=\""$(datadir)/locale"\" \
              -DGETTEXT_PACKAGE=\""$(GETTEXT_PACKAGE)"\" \
              -DLDM_THEME_DIR=\""$(datadir)/ldm/themes/"\"
