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
MANPAGES = $(CURDIR)/doc/man1
LIBDIR   = $(PREFIX)/lib
MANDIR   = $(PREFIX)/man

# If TMPDIR exists, use it instead of /tmp
ifneq ($(TMPDIR),)
	CFLAGS += -DPID_PATH=\"$(TMPDIR)\"
endif

# Flags
CC       ?= gcc
CFLAGS   += -O2 -g -Wall -Wextra
PREFLAGS  = -fPIC $(INCLUDE) -fvisibility=hidden $(CFLAGS)
LDFLAGS   = -shared
LDLIBS    = -ldl -pthread

#
# Guess target architecture:
# I know... this is ugly, but there isn't an exactly
# beautiful way to do it via Make/GNU Make. At least
# this approach works automatically with cross-compiler
# and caches the arch for future invocations.
#
# One downside is that it doesn't work in TCC, so it's
# not exactly portable.
#
# Optionally, the user can configure the target
# architecture beforehand, to support TCC and other
# environments (which may not have sed, cut...):
#   $ ARCH=x86_64 make CC=tcc
#
ARCH ?= $(shell cat .cache 2>/dev/null || \
	echo | $(CC) -dM -E - | \
	grep -P "__i386__|__x86_64__|__arm__|__aarch64__|__riscv" | \
	cut -d' ' -f2 | sed 's/__//g' | tee .cache)

OBJ =  preloader.o ipc.o util.o log.o load.o reaper.o arch.o
OBJ += arch/arch_$(ARCH).o arch/$(ARCH).o
DEP = $(OBJ:.o=.d)

# Phone targets
.PHONY: tests finder ltime install uninstall clean

# Pretty print
Q := @
ifeq ($(V), 1)
	Q :=
endif

# Rules
all: libpreloader.so preloader_cli

# C Files
%.o: %.c Makefile
	@echo "  CC      $@"
	$(Q)$(CC) $< -MMD -MP $(PREFLAGS) -c -o $@

# ASM Files
%.o: %.S Makefile
	@echo "  CC      $@"
	$(Q)$(CC) $< $(PREFLAGS) -c -o $@

# Preloader
libpreloader.so: $(OBJ)
	@echo "  LD      $@"
	$(Q)$(CC) $^ $(PREFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

# Client program
preloader_cli.o: preloader_cli.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -D "PRG_NAME=\"$(basename $@)\"" $(CFLAGS)
preloader_cli: preloader_cli.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@

# Tests
tests: libpreloader.so preloader_cli $(TESTS)/test
	@bash "$(TESTS)/test.sh"
$(TESTS)/test.o: $(TESTS)/test.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -o $@ $(CFLAGS)
$(TESTS)/test: $(TESTS)/test.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@

# Finder
finder: $(UTILS)/finder
$(UTILS)/finder.o: $(UTILS)/finder.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -o $@ $(CFLAGS)
$(UTILS)/finder: $(UTILS)/finder.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ -lelf

# LTime
ltime: $(UTILS)/ltime libpreloader.so preloader_cli
$(UTILS)/ltime.o: $(UTILS)/ltime.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -o $@ $(CFLAGS)
$(UTILS)/ltime: $(UTILS)/ltime.o
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
	$(Q)install -d $(DESTDIR)$(MANDIR)/man1
	$(Q)install -m 644 $(MANPAGES)/*.1 $(DESTDIR)$(MANDIR)/man1

# Uninstall
uninstall:
	$(RM) $(DESTDIR)$(LIBDIR)/libpreloader.so
	$(RM) $(DESTDIR)$(BINDIR)/preloader
	$(RM) $(DESTDIR)$(BINDIR)/preloader_cli
	$(RM) $(DESTDIR)$(MANDIR)/man1/preloader.1
	$(RM) $(DESTDIR)$(MANDIR)/man1/preloader_cli.1

# Clean
clean:
	$(RM) .cache
	$(RM) $(OBJ)
	$(RM) $(DEP)
	$(RM) preloader_cli.o
	$(RM) $(TESTS)/test.o
	$(RM) $(UTILS)/finder.o
	$(RM) $(UTILS)/ltime.o
	$(RM) $(CURDIR)/libpreloader.so
	$(RM) $(CURDIR)/preloader_cli
	$(RM) $(TESTS)/test
	$(RM) $(UTILS)/finder
	$(RM) $(UTILS)/ltime

-include $(DEP)
