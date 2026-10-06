#!/usr/bin/env bash
# Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Creates the FAT image used as SD card by the native_sim build.
#
#   mk_sdcard.sh [-o sdcard.img] [-n] [UID=DIR | DIR]...
#
#   DIR       copied to /Music/<name of DIR>/
#   UID=DIR   same, plus a playlist for tag UID with all mp3 files of DIR
#
# Without DIRs the image gets three playlists:
#   0a1b2c3d  Demo: generated test tones (needs ffmpeg)
#   a1b2c3d4  Six Little Ducklings: LibriVox story, chapters 1-4
#   b5c6d7e8  Goldberg Variations: Bach, Aria and variations 1-5 (Musopen)
# The last two are public domain recordings from archive.org, downloaded once
# into ~/.cache/wirbelwind (-n: skip them, e.g. when offline).
#
# Needs: mkfs.fat, mtools (mmd, mcopy), ffmpeg, curl.

set -euo pipefail

IMG=sdcard.img
SIZE_MIB=64 # must match the sd partition in boards/native_sim.overlay
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/wirbelwind
DOWNLOAD=1

while getopts "o:nh" opt; do
	case $opt in
	o) IMG=$OPTARG ;;
	n) DOWNLOAD=0 ;;
	*) sed -n '5,20p' "$0"; exit 1 ;;
	esac
done
shift $((OPTIND - 1))

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fetch() {
	local dst=$1 url=$2

	[ -s "$dst" ] && return 0
	mkdir -p "$(dirname "$dst")"
	curl -sfL -o "$dst.part" "$url" && mv "$dst.part" "$dst"
}

# Returns non-zero if a download failed
download_free() {
	local ia=https://archive.org/download
	local bach=$ia/MusopenCollectionAsFlac/Bach_GoldbergVariations/JohannSebastianBach
	local story="$CACHE/Six Little Ducklings" music="$CACHE/Goldberg Variations"
	local i

	echo "Fetching free recordings from archive.org (cached in $CACHE)"
	for i in 01 02 03 04; do
		fetch "$story/${i}_Six_Little_Ducklings.mp3" \
			"$ia/sixlittleducklings_1504_librivox/sixlittleducklings_${i}_pyle_64kb.mp3" || return 1
	done
	fetch "$music/01_Aria.mp3" "$bach-01-GoldbergVariationsBwv.988-Aria.mp3" || return 1
	for i in 1 2 4 5; do
		fetch "$music/0$((i + 1))_Variation_$i.mp3" \
			"$bach-0$((i + 1))-GoldbergVariationsBwv.988-Variation$i.mp3" || return 1
	done
	fetch "$music/04_Variation_3.mp3" \
		"$bach-04-GoldbergVariationsBwv.988-Variation3.CanonOnTheUnison.mp3" || return 1
}

make_demo() {
	local notes=(440 494 523 587 659) i n

	mkdir -p "$WORK/demo/Demo"
	for i in "${!notes[@]}"; do
		n=$(printf "%02d" $((i + 1)))
		ffmpeg -loglevel error -f lavfi -i "sine=frequency=${notes[$i]}:duration=20" \
			-ac 2 -ar 44100 -b:a 128k -metadata title="Track $n" \
			"$WORK/demo/Demo/${n}_tone_${notes[$i]}hz.mp3"
	done
}

if [ $# -eq 0 ]; then
	make_demo
	set -- "0a1b2c3d=$WORK/demo/Demo"
	if [ $DOWNLOAD -eq 1 ]; then
		if download_free; then
			set -- "$@" "a1b2c3d4=$CACHE/Six Little Ducklings" \
				"b5c6d7e8=$CACHE/Goldberg Variations"
		else
			echo "WARNING: download failed, only the demo playlist is created" >&2
		fi
	fi
fi

rm -f "$IMG"
truncate -s ${SIZE_MIB}M "$IMG"
mkfs.fat -n WIRBELWIND "$IMG" >/dev/null
mmd -i "$IMG" ::/Music ::/Music/.config

for arg in "$@"; do
	uid=
	dir=$arg
	if [[ $arg == *=* ]]; then
		uid=${arg%%=*}
		dir=${arg#*=}
	fi
	name=$(basename "$dir")
	mcopy -s -i "$IMG" "$dir" "::/Music/$name"
	echo "Copied $dir -> /Music/$name"

	if [ -n "$uid" ]; then
		uid=${uid,,}
		tracks=$(cd "$dir" && find . -maxdepth 1 -type f -iname '*.mp3' | sort |
			sed "s|^\./|/Music/$name/|; s|.*|\"&\"|" | paste -sd,)
		printf '{"uuid":"%s","name":"%s","tracks":[%s]}' "$uid" "$name" "$tracks" \
			>"$WORK/playlist_$uid"
		mcopy -i "$IMG" "$WORK/playlist_$uid" "::/Music/.config/playlist_$uid"
		echo "Playlist for tag $uid: $(echo "$tracks" | tr ',' '\n' | grep -c .) tracks"
	fi
done

echo "Created $IMG"
