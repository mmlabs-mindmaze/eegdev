ACLOCAL_AMFLAGS = -I m4 --install
SUBDIRS = gnulib-local lib src/core src/plugins doc tests
EXTRA_DIST = autogen.sh m4/gnulib-cache.m4
	
install-usb-plugdev:
	@echo "Installing 50-usb-plugdev.rules into /etc/udev/rules.d/ ..." \
	&& $(INSTALL) $(top_srcdir)/auxconfig/50-usb-plugdev.rules /etc/udev/rules.d \
	&& echo "done."

uninstall-usb-plugdev:
	@echo "Removing 50-usb-plugdev.rules from /etc/udev/rules.d/ ..." \
	&& rm -f /etc/udev/rules.d/50-usb-plugdev.rules && echo "done."
