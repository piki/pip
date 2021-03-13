SUBDIRS = libannotate dbfill expectations pathview
VERSION = 0.0-20071024

all:
	set -e ; for i in $(SUBDIRS); do $(MAKE) -C $$i all; done

distclean: clean
	rm -f Makefile dbfill/Makefile expectations/Makefile libannotate/Makefile pathview/Makefile
	rm -rf autom4te.cache config.log config.status

clean:
	set -e ; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done
	set -e ; $(MAKE) -C DEBIAN clean
	rm -f *.tar.gz

tar:
	rm -f pip-$(VERSION).tar.gz
	ln -s . pip-$(VERSION)
	sed -e s,^,pip-$(VERSION)/, .tarlist | tar cvz -T - -f pip-$(VERSION).tar.gz
	rm pip-$(VERSION)

	#cg-export pip-$(VERSION).tar.gz

deb: all
	set -e ; $(MAKE) -C DEBIAN all VERSION=$(VERSION)
