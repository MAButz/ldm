EXTRA_DIST=ldm

install-data-hook: ldm
	if test '!' -f $(DESTDIR)$(datadir)/ltsp/screen.d ; then \
		$(mkinstalldirs) $(DESTDIR)$(datadir)/ltsp/screen.d; \
	fi
	$(INSTALL_SCRIPT) ldm $(DESTDIR)$(datadir)/ltsp/screen.d
