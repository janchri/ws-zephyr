#!/usr/bin/env bash
# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Builds the MCUs of a twixt sample for native_sim (west build -DMCU=<mcu>),
# runs each as its own process on the sample's virtual I2C bus, and prints their
# logs as they come, every line after the name of its MCU. Ctrl-C stops them.
#
#   scripts/twixt_run.sh <sample> <mcu>... [-- <native_sim options>]
#
#   scripts/twixt_run.sh samples/twixt/hello a b
#   scripts/twixt_run.sh samples/twixt/hello a b -- -stop_at=3
#
#   BUILD_DIR=dir       where the builds, logs and flash files go
#                       (default: ./build-twixt-<sample>)
#   SKIP_BUILD=1        run what was built before
#   GDB=<mcu>           build that MCU for debugging (<BUILD_DIR>/<mcu>-gdb)
#                       and run it under gdbserver :2345, linked as
#                       <BUILD_DIR>/gdb.exe
#
# Every MCU has its own flash file, <BUILD_DIR>/<mcu>.flash.bin, if it has
# flash: settings and files are still there on the next run.
#
# Needs `west` and an initialised Zephyr environment (ZEPHYR_BASE).

set -u

[[ $# -ge 2 ]] || { sed -n 's/^#   //p' "$0" | head -3; exit 2; }

sample=$(realpath "$1")
shift
mcus=()
while [[ $# -gt 0 && $1 != -- ]]; do
	mcus+=("$1")
	shift
done
[[ ${1:-} == -- ]] && shift
out=$(realpath -m "${BUILD_DIR:-build-twixt-$(basename "$sample")}")
mkdir -p "$out"

dir() { [[ $1 == "${GDB:-}" ]] && echo "$out/$1-gdb" || echo "$out/$1"; }

# The images are independent: build them at the same time
if [[ -z ${SKIP_BUILD:-} ]]; then
	echo "building ${mcus[*]} ..."
	builds=()
	for m in "${mcus[@]}"; do
		opt=()
		[[ $m == "${GDB:-}" ]] && opt=(-DCONFIG_DEBUG_OPTIMIZATIONS=y)
		west build -b native_sim -d "$(dir "$m")" "$sample" -- -DMCU="$m" "${opt[@]}" \
			> "$out/$m.build.log" 2>&1 &
		builds+=("$!")
	done
	for i in "${!mcus[@]}"; do
		wait "${builds[i]}" || {
			echo "build of ${mcus[i]} failed, see $out/${mcus[i]}.build.log"
			exit 1
		}
	done
fi

pids=()
cleanup() {
	kill "${pids[@]}" 2>/dev/null
	[[ -n ${GDB:-} ]] && pkill -f "^$(dir "$GDB")/zephyr/zephyr.exe"
	wait 2>/dev/null
}
trap cleanup EXIT
trap exit INT TERM

for m in "${mcus[@]}"; do
	exe=$(dir "$m")/zephyr/zephyr.exe
	flash=()
	"$exe" --help 2>/dev/null | grep -q -- '-flash=' && flash=(--flash="$out/$m.flash.bin")
	gdb=()
	if [[ $m == "${GDB:-}" ]]; then
		gdb=(gdbserver :2345)
		ln -sf "$exe" "$out/gdb.exe"
	fi
	"${gdb[@]}" "$exe" "${flash[@]}" "$@" > >(sed -u "s/^/[$m] /") 2>&1 &
	pids+=("$!")
done
wait
