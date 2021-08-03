## Process this file with automake to produce Makefile.in

ldmlibexecdir = $(libexecdir)/ldm
ldmlibexec_PROGRAMS = ldmgtkgreet
ldmgtkgreet_SOURCES = hostwin.c greeter.c langwin.c sesswin.c prefs.c prefwin.c \
					  ../src/ldminfo.c ../src/logging.c

ldmgtkgreet_LDADD = $(GTK_LIBS)
AM_CPPFLAGS = $(GTK_CFLAGS) \
              -DLOCALE_DIR=\""$(datadir)/locale"\" \
              -DLDM_THEME_DIR=\""$(datadir)/ldm/themes/"\" \
              -DRC_DIR=\""$(datadir)/ldm"\" \
			  -DLDM_EXEC_DIR=\""$(ldmlibexecdir)"\" \
			  -DLDM_PLUG_DIR=\""$(ldmlibexecdir)"\"
