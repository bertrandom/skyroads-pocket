#!/bin/bash
# Copy the core onto the Pocket's SD card over USB, with mtools (no mounting).
#
#   ./push.sh                  make package, then build/package
#   ./push.sh path/to.zip      a release zip
#   ./push.sh path/to/dir      any folder laid out like the SD card
#
# The Pocket shows its card over USB (Tools > Developer > USB SD Card Access
# or similar) as a disk called "Pocket SD"; this finds it, says what it will
# copy and asks first (gum), then writes with sudo mcopy.  The card must not
# be mounted while mtools writes to it: if macOS has mounted it, it is
# unmounted first.  POCKET_DEVICE=/dev/diskNs1 (or a FAT image file) skips
# the search.
set -euo pipefail
cd "$(dirname "$0")"

for t in gum mcopy mmd mdir mdeltree diskutil; do
    command -v "$t" >/dev/null || { echo "needs $t (brew install gum mtools)"; exit 1; }
done
export MTOOLS_SKIP_CHECK=1
SUDO=sudo

# --- the package ------------------------------------------------------------
if [ $# -eq 0 ]; then
    # the local build, brought up to date (firmware, data pack, the JSON
    # files; the bitstream is whatever build/quartus last compiled)
    gum spin --title "make package" -- make package >/dev/null
    SRC=build/package
else
    SRC=$1
fi
TMP=
cleanup() { [ -n "$TMP" ] && rm -rf "$TMP"; return 0; }
trap cleanup EXIT
if [ -f "$SRC" ] && [[ "$SRC" == *.zip ]]; then
    TMP=$(mktemp -d)
    unzip -q "$SRC" -d "$TMP"
    PKG=$TMP
elif [ -d "$SRC" ]; then
    PKG=${SRC%/}
else
    gum log --level error "no package at $SRC (make package, or give a release zip)"
    exit 1
fi
CORE_DIR=$(cd "$PKG" && ls -d Cores/*.SkyRoads 2>/dev/null | head -1)
if [ -z "$CORE_DIR" ] || [ ! -f "$PKG/$CORE_DIR/skyroads.rbf_r" ]; then
    gum log --level error "$PKG has no Cores/*.SkyRoads/skyroads.rbf_r (compile the bitstream first)"
    exit 1
fi
VERSION=$(python3 -c 'import json,sys; m=json.load(open(sys.argv[1]))["core"]["metadata"]; print(m["version"], m["date_release"])' "$PKG/$CORE_DIR/core.json")

# --- the Pocket ---------------------------------------------------------------
if [ -n "${POCKET_DEVICE:-}" ]; then
    PART=$POCKET_DEVICE
    DISK=$PART
    [ -f "$PART" ] && SUDO=          # an image file: no root needed
else
    DISK=
    for d in $(diskutil list external physical | awk '/^\/dev\/disk/ {print $1}'); do
        if diskutil info "$d" | grep -q 'Media Name: *Pocket SD'; then DISK=$d; break; fi
    done
    if [ -z "$DISK" ]; then
        gum log --level error "no Pocket found over USB (turn on the Pocket's USB SD card access)"
        exit 1
    fi
    PART=${DISK}s1
    if ! diskutil info "$PART" | grep -q 'File System Personality: *MS-DOS FAT'; then
        gum log --level error "$PART is not a FAT volume"
        exit 1
    fi
fi
MOUNTED=no
if [ -b "$PART" ] && diskutil info "$PART" | grep -q 'Mounted: *Yes'; then MOUNTED=yes; fi
SIZE=$( [ -b "$PART" ] && diskutil info "$DISK" | sed -n 's/^ *Disk Size: *\([^(]*\).*/\1/p' | sed 's/ *$//' || echo "image")
NAME=$( [ -b "$PART" ] && diskutil info "$PART" | sed -n 's/^ *Volume Name: *//p' || basename "$PART")

# the old core id, from before the rename to bertrandom.SkyRoads
OLD=
[ "$SUDO" ] && $SUDO -v -p "sudo password (to read and write $PART): "
if $SUDO mdir -i "$PART" ::/Cores/skyroads.SkyRoads >/dev/null 2>&1 && [ "$CORE_DIR" != Cores/skyroads.SkyRoads ]; then
    OLD=Cores/skyroads.SkyRoads
fi

# --- say what will happen, and ask -------------------------------------------
FILES=$(cd "$PKG" && find Assets Cores Platforms -type f ! -name .DS_Store | sort)
{
    echo "**SkyRoads $VERSION** onto the Pocket"
    echo
    echo "- Pocket: \`$PART\` ($NAME, $SIZE)"
    echo "- from: \`$SRC\`"
    [ "$MOUNTED" = yes ] && echo "- unmount \`$PART\` first (macOS has it mounted)"
    [ -n "$OLD" ] && echo "- delete \`$OLD\` (the old name of this core)"
    echo "- copy these, overwriting what is there:"
} | gum format
while read -r f; do
    printf '      %-48s %9s bytes\n' "$f" "$(stat -f %z "$PKG/$f")"
done <<< "$FILES"
echo

gum confirm "Copy these to the Pocket?" || { echo "nothing copied"; exit 0; }

# --- do it ------------------------------------------------------------------
[ "$MOUNTED" = yes ] && diskutil unmount "$PART" >/dev/null
[ -n "$OLD" ] && $SUDO mdeltree -i "$PART" "::/$OLD"
# the folders, one level at a time (an existing one is fine)
(cd "$PKG" && find Assets Cores Platforms -type d | sort) | while read -r dir; do
    $SUDO mmd -i "$PART" -D s "::/$dir" 2>/dev/null || true
done
while read -r f; do
    echo "  $f"
    $SUDO mcopy -o -i "$PART" "$PKG/$f" "::/$f"
done <<< "$FILES"
sync
gum log --level info "done: SkyRoads $VERSION is on the Pocket (turn off USB SD card access on the Pocket to use it)"
