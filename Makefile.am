ACLOCAL_AMFLAGS = -I m4 --install
SUBDIRS = lib src/core src/plugins doc tests 

EXTRA_DIST = auxconfig/50-usb-plugdev.rules	\
		debian/changelog debian/compat debian/control\
		debian/copyright debian/docs		\
		debian/rules				\
		debian/source				\
		debian/eegdev-plugins-free.install 	\
		debian/eegdev-plugins-free.udev    	\
		debian/eegdev-plugins-gtec.install 	\
		debian/eegdev-plugins-gtec.udev    	\
		debian/libeegdev0.install          	\
		debian/libeegdev0.manpages         	\
		debian/libeegdev0.symbols          	\
		debian/libeegdev-dev.examples		\
		debian/libeegdev-dev.install       	\
		debian/libeegdev-dev.manpages

REMOVE_DISTDIR = \
  { test ! -d "$(distdir)" \
    || { find "$(distdir)" -type d ! -perm -200 -exec chmod u+w {} ';' \
         && rm -fr "$(distdir)"; }; }

dist-deb: distdir
	am__cwd=`pwd` \
	&& cd $(distdir) \
	&& dpkg-buildpackage $(DPKG_BUILDPACKAGE_FLAGS) \
	&& cd "$$am__cwd" 
	$(REMOVE_DISTDIR)
	
install-usb-plugdev:
	@echo "Installing 50-usb-plugdev.rules into /etc/udev/rules.d/ ..." \
	&& $(INSTALL) $(top_srcdir)/auxconfig/50-usb-plugdev.rules /etc/udev/rules.d \
	&& echo "done."

uninstall-usb-plugdev:
	@echo "Removing 50-usb-plugdev.rules from /etc/udev/rules.d/ ..." \
	&& rm -f /etc/udev/rules.d/50-usb-plugdev.rules && echo "done."

