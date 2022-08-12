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

# Fancy colors =)
RED="\033[0;31m"
GREEN="\033[0;32m"
NC="\033[0m"

# Paths
CURDIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
PROG=$(readlink -f "$CURDIR/../daem")
CLI=$(readlink -f "$CURDIR/../client")
TEST="$CURDIR/test"

not_pass() {
	printf "Tests: [%bNOT PASSED%b]\nReason: $1\n" $RED $NC >&2
	rm -rf "$CURDIR/.out_normal.txt"
	rm -rf "$CURDIR/.out_cli.txt"
	exit 1
}

pass() {
	printf "Tests: [%bPASSED%b]\n" $GREEN $NC
	rm -rf "$CURDIR/.out_normal.txt"
	rm -rf "$CURDIR/.out_cli.txt"
	exit 0
}

# Sanity checks
if [ ! -f "$PROG" ]; then
	echo "$PROG not found, failed!"
	exit 1
fi

if [ ! -f "$TEST" ]; then
	echo "Test program not found, failed!"
	exit 1
fi

# First: run test normally
echo "some input to test stdin" | $TEST 1 2 3 4 \
	&> "$CURDIR/.out_normal.txt"
out_n="$?"

# Second:
# 1) Launch preloader in daemon mode
$PROG $TEST -d

# 2) Run preloader client
echo "some input to test stdin" | $CLI $TEST 1 2 3 4 \
	&> "$CURDIR/.out_cli.txt"
out_c="$?"

# 3) Stop daemon
$PROG -s

## Compare return code and outputs
if [ "$out_n" -ne "$out_c" ]; then
	not_pass "Return code differ from expected!"
fi

#
# Compare both outputs:
# Since stdout might be 'reordered' with stderr, we need
# to sort both outputs.
#
if ! cmp -s <(sort "$CURDIR/.out_cli.txt") \
	<(sort "$CURDIR/.out_normal.txt"); then
	not_pass "Output differ from expected"
fi

pass
