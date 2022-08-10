#!/usr/bin/env bash

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

#
# Grab all the libraries opened via 'dlopen' and save to a given file
#

SCRIPT_NAME="$0"

getlibs() {
	OUTPUT_FILE="$1"
	shift # '$@' should contain only the program+args

	TMPFOLDER="../.dump_$(date +%s%N)"
	mkdir $TMPFOLDER

	BIN="$(basename $1)"

	strace -ff -o "$TMPFOLDER/dump_$BIN" "$@"

	#
	# This regex is an approximation of what a dlopen
	# might be: since dlopen is not a system call, we
	# can only try to 'guess' when a dlopen happens by
	# other syscalls.
	#
	# In *my* environments, it looks like this:
	# openat(AT_FDCWD, "/folder/foo.so", O_RDONLY|O_CLOEXEC) = positive_number
	#   or this:
	# open("/folder/foo.so", O_RDONLY|O_CLOEXEC) = positive_number
	#
	grep -Pr \
		"open(at)?\((AT_FDCWD, )?\".+\.so(\.[0-9]+)*\", O_RDONLY\|O_CLOEXEC\) = [^-]" "$TMPFOLDER" \
		| cut -d'"' -f2 \
		| sort -u > $OUTPUT_FILE

	rm -rf $TMPFOLDER
}

usage() {
	printf "Usage: $SCRIPT_NAME -o output.txt <program-name-or-path> "
	printf "<program parameters>\n"
	exit 1
}

if [ "$#" -lt 3 ]; then
	echo "You should pass at least 3 parameters!"
	usage
fi

# Validate if '-o' was passed
if [ "$1" != "-o" ]; then
	echo "Parameter ($1) is not output file (-o)!"
	usage
fi

getlibs "${@:2}"
