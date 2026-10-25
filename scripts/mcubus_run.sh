#!/usr/bin/env bash
# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Builds the MCUs of an mcubus sample for native_sim (west build -DNODE=<node>),
# runs each as its own process on the sample's virtual I2C bus, and prints their
# logs as they come, every line after the name of its MCU. Ctrl-C stops them.
#
#   scripts/mcubus_run.sh <sample> <node>... [-- <native_sim options>]
#
#   scripts/mcubus_run.sh samples/mcubus/hello a b
#   scripts/mcubus_run.sh samples/mcubus/hello a b -- -stop_at=3
#
#   BUILD_DIR=dir       where the builds, logs and flash files go
#                       (default: ./build-mcubus-<sample>)
#   SKIP_BUILD=1        run what was built before
#
# Every MCU has its own flash file, <BUILD_DIR>/<node>.flash.bin, if it has
# flash: settings and files are still there on the next run.
#
# Needs `west` and an initialised Zephyr environment (ZEPHYR_BASE).

set -u

[[ $# -ge 2 ]] || { sed -n 's/^#   //p' "$0" | head -3; exit 2; }

sample=$(realpath "$1")
shift
nodes=()
while [[ $# -gt 0 && $1 != -- ]]; do
	nodes+=("$1")
	shift
done
[[ ${1:-} == -- ]] && shift
out=$(realpath -m "${BUILD_DIR:-build-mcubus-$(basename "$sample")}")
mkdir -p "$out"

# The images are independent: build them at the same time
if [[ -z ${SKIP_BUILD:-} ]]; then
	echo "building ${nodes[*]} ..."
	builds=()
	for n in "${nodes[@]}"; do
		west build -b native_sim -d "$out/$n" "$sample" -- -DNODE="$n" \
			> "$out/$n.build.log" 2>&1 &
		builds+=("$!")
	done
	for i in "${!nodes[@]}"; do
		wait "${builds[i]}" || {
			echo "build of ${nodes[i]} failed, see $out/${nodes[i]}.build.log"
			exit 1
		}
	done
fi

pids=()
cleanup() { kill "${pids[@]}" 2>/dev/null; wait 2>/dev/null; }
trap cleanup EXIT
trap exit INT TERM

for n in "${nodes[@]}"; do
	exe=$out/$n/zephyr/zephyr.exe
	flash=()
	"$exe" --help 2>/dev/null | grep -q -- '-flash=' && flash=(--flash="$out/$n.flash.bin")
	"$exe" "${flash[@]}" "$@" > >(sed -u "s/^/[$n] /") 2>&1 &
	pids+=("$!")
done
wait
