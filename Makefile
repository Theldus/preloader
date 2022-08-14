# MIT License
#
# Copyright (c) 2022 Davidson Francis <davidsondfgl@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Paths
INCLUDE  = -I.
TESTS    = $(CURDIR)/tests
UTILS    = $(CURDIR)/utils
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
MANPAGES = $(CURDIR)/doc/

# Detect machine type
MACHINE = $(shell uname -m)
ifeq ($(MACHINE), x86_64)
	LIBDIR = $(PREFIX)/lib64
else
	LIBDIR = $(PREFIX)/lib
endif

# Flags
CC     ?= gcc
CFLAGS += -fPIC -O0 $(INCLUDE) -g -fvisibility=hidden
LDFLAGS = -shared
LDLIBS  = -ldl -pthread

OBJ = preloader.o ipc.o util.o log.o load.o reaper.o

# Phone targets
.PHONY: tests finder install uninstall clean

# Pretty print
Q := @
ifeq ($(V), 1)
	Q :=
endif

# Rules
all: libpreloader.so preloader_cli

# C Files
%.o: %.c
	@echo "  CC      $@"
	$(Q)$(CC) $< $(CFLAGS) -c -o $@

# Preloader
libpreloader.so: $(OBJ)
	@echo "  LD      $@"
	$(Q)$(CC) $^ $(CFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

# Client program
preloader_cli.o: preloader_cli.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -D "PRG_NAME=\"$(basename $@)\""
preloader_cli: preloader_cli.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -O2 -o $@

# Tests
tests: libpreloader.so preloader_cli $(TESTS)/test
	@bash "$(TESTS)/test.sh"
$(TESTS)/test.o: $(TESTS)/test.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -o $@
$(TESTS)/test: $(TESTS)/test.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@

# Finder
finder: $(UTILS)/finder
$(UTILS)/finder.o: $(UTILS)/finder.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -o $@ -O3
$(UTILS)/finder: $(UTILS)/finder.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ -lelf

# Install
install: libpreloader.so preloader_cli
	@echo "  INSTALL      $^"
	$(Q)install -d $(DESTDIR)$(LIBDIR)
	$(Q)install -m 755 $(CURDIR)/libpreloader.so $(DESTDIR)$(LIBDIR)
	$(Q)install -d $(DESTDIR)$(BINDIR)
	$(Q)install -m 755 preloader $(DESTDIR)$(BINDIR)
	$(Q)install -m 755 preloader_cli $(DESTDIR)$(BINDIR)

# Uninstall
uninstall:
	$(RM) $(DESTDIR)$(LIBDIR)/libpreloader.so
	$(RM) $(DESTDIR)$(BINDIR)/preloader
	$(RM) $(DESTDIR)$(BINDIR)/preloader_cli

# Clean
clean:
	$(RM) $(OBJ)
	$(RM) preloader_cli.o $(TESTS)/test.o $(UTILS)/finder.o
	$(RM) $(CURDIR)/libpreloader.so
	$(RM) $(CURDIR)/preloader_cli
	$(RM) $(TESTS)/test
	$(RM) $(UTILS)/finder
