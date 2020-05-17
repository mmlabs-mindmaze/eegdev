SUBDIRS = examples

MANPAGES = eegdev-open-options.5

EXTRA_DIST = $(MANPAGES) \
             eegdev-biosemi.5 eegdev-datafile.5 \
             eegdev-neurosky.5 eegdev-gtec.5 eegdev-tobiia.5
man_MANS =

if ACT2_SUPPORT
man_MANS += eegdev-biosemi.5
endif

if XDF_SUPPORT
man_MANS += eegdev-datafile.5
endif

if NSKY_SUPPORT
man_MANS += eegdev-neurosky.5
endif

if GTEC_SUPPORT
man_MANS += eegdev-gtec.5
endif

if TIA_SUPPORT
man_MANS += eegdev-tobiia.5
endif

DOC_SRCS = \
	index.rst \
	eegdev.rst \
	$(eol)

export srctree=$(SRCTREE)

#
# Sphinx documentation build targets
# Skipped if --enable-sphinxdoc=no
#
if BUILD_SPHINXDOC
SPHINXBUILD   = sphinx-build
SRCTREE       = $(top_srcdir)

sphinx_verbose = $(sphinx_verbose_@AM_V@)
sphinx_verbose_ = $(sphinx_verbose_@AM_DEFAULT_V@)
sphinx_verbose_0 = -q

ALLSPHINXOPTS = -d $(builddir)/.doctrees $(sphinx_verbose) $(srcdir)

# Due to a dependency bug in sphinx-build, we use the -M option instead of
# the usual -b one. This hidden option sums up to the same as -b with makefile
# dependency support ... except that it does not support -W option which turns
# warnings into errors !
# => do this with a simple grep line on the output
# Since html target depends on man target, this is only needed once
sphinx-build.log: $(wildcard $(SRCTREE)/src/*.[ch]) $(DOC_SRCS)
	@$(RM) -f manpages-buildstamp
	$(AM_V_GEN) $(SPHINXBUILD) -M kernel-doc-man $(ALLSPHINXOPTS) $(builddir)/man &> sphinx-build.log

# sphinx-build.log target does the doc generation
# This target checks that it was done right
manpages-buildstamp: sphinx-build.log
	@if [ "$(shell grep -c WARNING: sphinx-build.log)" != "0" ] ; then \
		grep WARNING: sphinx-build.log; \
		false; \
	fi
	@echo
	@echo "Build finished. The man pages are in $(builddir)/man."
	@touch $@

.PHONY: man-pages
man-pages: manpages-buildstamp
html-local: html-buildstamp

# always generate html after man: parallel sphinx invocation is not upported
html-buildstamp: $(wildcard $(SRCTREE)/src/*.[ch]) $(DOC_SRCS) manpages-buildstamp
	@$(RM) -f $@
	$(AM_V_GEN) $(SPHINXBUILD) -M html $(ALLSPHINXOPTS) $(builddir)/html
	@echo
	@echo "Build finished. The HTML pages are in $(builddir)/html."
	@touch $@

install-html: html-buildstamp
	@mkdir -p $(DESTDIR)$(docdir)/html/
	cd $(builddir)/html && \
		find . -type f -exec $(install_sh_DATA) '{}' $(DESTDIR)$(docdir)/html/'{}' \;

uninstall-html:
	$(RM) -r $(DESTDIR)$(docdir)/html/

install-man3: manpages-buildstamp
	@mkdir -p $(DESTDIR)$(mandir)/man3
	$(foreach manpage, $(notdir $(wildcard $(builddir)/man/*.3)), \
		$(install_sh_DATA) $(builddir)/man/$(manpage) $(DESTDIR)$(mandir)/man3/$(manpage);)

uninstall-man3: manpages-buildstamp
	$(foreach manpage, $(notdir $(wildcard $(builddir)/man/*.3)), \
		$(RM) $(DESTDIR)$(mandir)/man3/$(manpage);)
	@-rmdir $(mandir)/man3

all-local: manpages-buildstamp html-buildstamp
install-data-local: install-man3 install-html
uninstall-local: uninstall-man3 uninstall-html

endif # BUILD_SPHINXDOC

clean-local:
	$(RM) -rf html .doctrees man manpages-buildstamp html-buildstamp sphinx-build.log
