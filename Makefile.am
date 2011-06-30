ACLOCAL_AMFLAGS = -I m4 --install
SUBDIRS = lib src doc tests 

EXTRA_DIST = auxconfig/50-usb-plugdev.rules	\
		debian/changelog debian/compat debian/control\
		debian/copyright debian/docs		\
		debian/rules				\
		debian/libeegdev0-free.install		\
		debian/libeegdev0-free.symbols		\
		debian/libeegdev0-free.manpages		\
		debian/libeegdev0-free.udev		\
		debian/libeegdev0-dev.install		\
		debian/libeegdev0-dev.manpages		\
		debian/libeegdev0-gtec.install		\
		debian/libeegdev0-gtec.symbols		\
		debian/libeegdev0-gtec.manpages		\
		debian/libeegdev0-gtec.udev

REMOVE_DISTDIR = \
  { test ! -d "$(distdir)" \
    || { find "$(distdir)" -type d ! -perm -200 -exec chmod u+w {} ';' \
         && rm -fr "$(distdir)"; }; }

dist-deb: distdir
	am__cwd=`pwd` \
	&& cd $(distdir) \
	&& CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" FFLAGS="$(FFLAGS)" CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" dpkg-buildpackage \
	&& cd "$$am__cwd" 
	$(REMOVE_DISTDIR)
	
install-usb-plugdev:
	@echo "Installing 50-usb-plugdev.rules into /etc/udev/rules.d/ ..." \
	&& $(INSTALL) $(top_srcdir)/auxconfig/50-usb-plugdev.rules /etc/udev/rules.d \
	&& echo "done."

uninstall-usb-plugdev:
	@echo "Removing 50-usb-plugdev.rules from /etc/udev/rules.d/ ..." \
	&& rm -f /etc/udev/rules.d/50-usb-plugdev.rules && echo "done."
