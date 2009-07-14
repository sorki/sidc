DESTDIR =
prefix = /usr/local
exec_prefix = ${prefix}
topdir = .
srcdir = .

bindir = $(DESTDIR)${exec_prefix}/bin
mandir = $(DESTDIR)${prefix}/man/man1

CC = gcc
CFLAGS = -g -O2 -Wall
LDFLAGS = 
DEFS = -DHAVE_CONFIG_H
LIBS = -lasound -lfftw3 -lm 

$(topdir)/sidd: $(srcdir)/sidd.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(DEFS) -o $@ $< $(LIBS)

install: $(topdir)/sidd
	install sidd -s $(bindir)

uninstall:
	rm -f $(bindir)/sidd

clean:
	rm -f $(topdir)/sidd 

distclean: clean
	rm -f $(topdir)/Makefile \
              $(topdir)/config.status \
              $(topdir)/config.log \
              $(topdir)/config.h \
              $(topdir)/config.cache

