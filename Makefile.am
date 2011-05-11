ACLOCAL_AMFLAGS = -I m4 --install
SUBDIRS = lib src doc tests 

EXTRA_DIST = debian/changelog debian/compat debian/control\
		debian/copyright debian/docs		\
		debian/postinst.ex debian/postrm.ex	\
		debian/preinst.ex debian/prerm.ex	\
		debian/rules				\
		debian/libeegdev0.install		\
		debian/libeegdev0-dev.install		\
		debian/libeegdev0-gtec.install		

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
