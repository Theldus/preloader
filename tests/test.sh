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
PROG=$(readlink -f "$CURDIR/../preloader")
CLI=$(readlink -f "$CURDIR/../preloader_cli")
TEST="$CURDIR/test"

announce() {
	printf "Test $1: "
}

not_pass() {
	$PROG -s
	printf "[%bNOT PASSED%b]\nReason: $2\n" $RED $NC >&2
	exit 1
}

pass() {
	$PROG -s
	printf "[%bPASSED%b]\n" $GREEN $NC
	rm -rf "$CURDIR/.out_normal.txt"
	rm -rf "$CURDIR/.out_cli.txt"
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

test1() {
	local flags="$1"
	local test_name="$2"

	announce "$2"

	# First: run test normally
	echo "some input to test stdin" | $TEST a b c d \
		&> "$CURDIR/.out_normal.txt"
	out_n="$?"

	# Second:
	# 1) Launch preloader in daemon mode
	$PROG $TEST -d "$flags"
	sleep 2s # Wait for daemon start

	# 2) Run preloader client
	echo "some input to test stdin" | $CLI $TEST a b c d \
		&> "$CURDIR/.out_cli.txt"
	out_c="$?"

	## Compare return code and outputs
	if [ "$out_n" -ne "$out_c" ]; then
		not_pass "$test_name" \
		"Return code differ from expected!, expected: $out_n, got: $out_c"
	fi

	#
	# Compare both outputs:
	# Since stdout might be 'reordered' with stderr, we need
	# to sort both outputs.
	#
	if ! cmp -s <(sort "$CURDIR/.out_cli.txt") \
		<(sort "$CURDIR/.out_normal.txt"); then
		not_pass "$test_name" "Output differ from expected"
	fi

	pass "$test_name"
}

test2() {
	local flags="$1"
	local test_name="$2"
	local arr=({a..z})
	local MAX=200

	announce "$2"

	# 1) Launch preloader in daemon mode
	$PROG $TEST -d "$flags"
	sleep 2s # Wait for daemon start

	# Loop through each amount of args
	for i in $(seq 1 $MAX)
	do
		# Build argument list
		# First  run (i = 1): a
		# Second run (i = 2): a b
		# Third  run (i = 3): a b c
		# n-th   run (i = n): a b c .. z a b c ...
		args=()
		for j in $(seq 0 $((i-1)))
		do
			idx=$((j%26))
			args+=(${arr[idx]})
		done

		# Run
		echo "input" | $CLI $TEST "${args[@]}" &> /dev/null
		out_c="$?"

		if [ "$out_c" -ne 42 ]; then
			not_pass "$test_name" "Failed with $i arguments!"
		fi
	done

	pass "$test_name"
}

test1 ""   "#1: normal run "
test1 "-b" "#2: run w/ bind"
test2 ""   "#3: range test (this may take a while)"
