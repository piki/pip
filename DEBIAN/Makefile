SUBDIRS = dev expectations pathview reconcile doc
ifeq ("@HAVE_JAVA@","1")
SUBDIRS += java
endif

all:
	ARCH=`uname -m | sed -e s/i.86/i386/` ; \
	set -e ; for i in $(SUBDIRS); do \
	rm -rf debtemp ; \
	mkdir -p debtemp/DEBIAN ; \
	while read FROM TO STRIP; do \
	mkdir -p debtemp/`echo $$TO | sed -e 's,/[^/]*$$,,'` ; \
	cp ../$$FROM debtemp/$$TO ; \
	if [ ! -z "$$STRIP" ]; then strip debtemp/$$TO; fi ; \
	done < $$i/files ; \
	sed -e 's/<VERSION>/$(VERSION)/g' -e 's/<SIZE>/'`du -ks debtemp | cut -d'	' -f1`/ < $$i/control > debtemp/DEBIAN/control ; \
	dpkg --build debtemp pip-$${i}_$(VERSION)_$$ARCH.deb ; \
	done ; \
	rm -rf debtemp ; \
	mkdir -p debtemp/DEBIAN ; \
	sed -e 's/<VERSION>/$(VERSION)/g' -e 's/<SIZE>/'`du -ks debtemp | cut -d'	' -f1`/ < pip/control > debtemp/DEBIAN/control ; \
	dpkg --build debtemp pip_$(VERSION)_$$ARCH.deb ; \
	rm -rf debtemp

clean:
	rm -rf pip*.deb debtemp
