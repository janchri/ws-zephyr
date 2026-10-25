#!/usr/bin/env bash
# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Builds the four MCUs of the sample (boards a, b, c and the gateway gw), runs
# them as native_sim processes on one virtual I2C bus, and prints the PTY of
# each board's shell. Ctrl-C stops them.
#
#   scripts/run.sh [build-dir]        (default: ./build-mcubus-shell)
#   SKIP_BUILD=1 scripts/run.sh ...   run what was built before
#
# Needs `west` and an initialised Zephyr environment (ZEPHYR_BASE).

set -u

sample=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${1:-build-mcubus-shell}")
builds=()
nodes=(a b c gw)

# The images are independent: build them at the same time
if [[ -z "${SKIP_BUILD:-}" ]]; then
	echo "building MCUs ${nodes[*]} ..."
	for n in "${nodes[@]}"; do
		west build -b native_sim -d "$out/$n" "$sample" -- -DNODE="$n" \
			> "$out.$n.build.log" 2>&1 &
		builds+=("$!")
	done
	for i in "${!nodes[@]}"; do
		wait "${builds[i]}" || {
			echo "build of MCU ${nodes[i]} failed, see $out.${nodes[i]}.build.log"
			exit 1
		}
	done
fi

pids=()
cleanup() { kill "${pids[@]}" 2>/dev/null; wait 2>/dev/null; }
trap cleanup EXIT

for n in "${nodes[@]}"; do
	# Every board has its own flash, with its settings and its littlefs; the
	# gateway has none
	flash=()
	[[ $n != gw ]] && flash=(--flash="$out.$n.flash.bin")
	"$out/$n/zephyr/zephyr.exe" "${flash[@]}" > "$out.$n.log" 2>&1 &
	pids+=("$!")
done

# The gateway names the PTY of every board
for _ in $(seq 50); do
	(($(grep -c "^pty-. connected" "$out.gw.log") == 3)) && break
	sleep 0.1
done
sed -n 's/^pty-\(.\) connected to pseudotty: \(.*\)/board \1: \2/p' "$out.gw.log"
echo "open one with e.g.  tio <pty>  (or picocom, screen); logs: $out.<a,b,c,gw>.log"
echo "flash of each board (settings, littlefs): $out.<a,b,c>.flash.bin"
echo "Ctrl-C stops the MCUs"
wait
