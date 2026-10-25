#!/usr/bin/env bash
# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Builds the three MCUs of the sample (a, b, c), runs them as three native_sim
# processes on one virtual I2C bus, and prints their merged log.
#
#   scripts/run.sh [build-dir]        (default: ./build-mcubus-settings)
#   SKIP_BUILD=1 scripts/run.sh ...   run what was built before
#   SEED=7 scripts/run.sh ...         seed of the random choice of peers (default 1);
#                                     every MCU gets its own: SEED, SEED+1, ...
#
# Needs `west` and an initialised Zephyr environment (ZEPHYR_BASE).

set -u

sample=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${1:-build-mcubus-settings}")
builds=()
nodes=(a b c)
timeout_s=30

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

for i in "${!nodes[@]}"; do
	n=${nodes[i]}
	# Every MCU has its own flash, where its settings are
	"$out/$n/zephyr/zephyr.exe" --seed=$((${SEED:-1} + i)) --flash="$out.$n.flash.bin" \
		> "$out.$n.log" 2>&1 &
	pids+=("$!")
done

# Done when every MCU has printed its verdict
deadline=$((SECONDS + timeout_s))
while ((SECONDS < deadline)); do
	finished=0
	for n in "${nodes[@]}"; do
		grep -q "all settings verified\|settings check failed" "$out.$n.log" && finished=$((finished + 1))
	done
	((finished == ${#nodes[@]})) && break
	sleep 0.2
done
cleanup
pids=()

# Merged log: the time stamp first, then the MCU
for n in "${nodes[@]}"; do
	grep -v "^\*\*\* Booting\|^$\|^WARNING: Using a test" "$out.$n.log" | sed "s/^\(\[[0-9:.,]*\]\) /\1 [$n] /"
done | sort -s -k1,1

failed=0
sent=0; served=0
for n in "${nodes[@]}"; do
	grep -q "all settings verified" "$out.$n.log" || { echo "MCU $n: FAILED"; failed=1; }
	sent=$((sent + $(sed -n 's/.* sent \([0-9]*\) requests.*/\1/p' "$out.$n.log")))
	served=$((served + $(sed -n 's/.*served \([0-9]*\) requests.*/\1/p' "$out.$n.log")))
done
echo "$sent requests sent, $served served"
((sent == served)) || { echo "sent and served requests do not match"; failed=1; }
((failed == 0)) && echo "all MCUs verified the settings"
exit "$failed"
