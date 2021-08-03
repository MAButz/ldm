ldmlibexecdir = $(libexecdir)/ldm
sharedir = $(datadir)/ldm
share_DATA = ldm-script
EXTRA_DIST = $(share_DATA)

install-data-hook:
	if test ! -f $(DESTDIR)$(sharedir)/rc.d ; then \
		$(mkinstalldirs) $(DESTDIR)$(sharedir)/rc.d; \
	fi
