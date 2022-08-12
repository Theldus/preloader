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
INCLUDE = -I.
TESTS = $(CURDIR)/tests

# Flags
CC     ?= gcc
CFLAGS += -fPIC -O0 $(INCLUDE) -g -fvisibility=hidden
LDFLAGS = -shared
LDLIBS  = -ldl -pthread

OBJ = daem.o ipc.o util.o log.o load.o reaper.o

# Phone targets
.PHONY: tests clean

# Pretty print
Q := @
ifeq ($(V), 1)
	Q :=
endif

# Rules
all: daem.so client

# C Files
%.o: %.c
	@echo "  CC      $@"
	$(Q)$(CC) $< $(CFLAGS) -c -o $@

# Preloader
daem.so: $(OBJ)
	@echo "  LD      $@"
	$(Q)$(CC) $^ $(CFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

# Client program
client.o: client.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c
client: client.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -O2 -o $@

# Tests
tests: daem.so client $(TESTS)/test
	@bash "$(TESTS)/test.sh"
$(TESTS)/test.o: $(TESTS)/test.c
	@echo "  CC      $@"
	$(Q)$(CC) $^ -c -o $@
$(TESTS)/test: $(TESTS)/test.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@

clean:
	$(RM) $(OBJ)
	$(RM) client.o $(TESTS)/test.o
	$(RM) $(CURDIR)/daem.so
	$(RM) $(CURDIR)/client
	$(RM) $(TESTS)/test
