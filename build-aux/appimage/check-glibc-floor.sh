#!/usr/bin/env bash
# Fails when an ELF file in the AppDir needs a newer glibc than the floor:
#   check-glibc-floor.sh <AppDir> [floor]   (floor defaults to 2.28)
# The floor is the oldest glibc the AppImage runs on (obs-rust/obs-studio#926).
# The C++ runtime of that floor's oldest distribution (AlmaLinux/RHEL 8's
# libstdc++ from GCC 8) is checked too, since an AppImage that does not bundle
# glibc does not bundle libstdc++ either: GLIBCXX_3.4.25 and CXXABI_1.3.11.
# Only the versions a file *requires* count (objdump -p, "Version References"),
# so the bundled glibc itself, which defines every GLIBC_2.x up to its own,
# does not trip the check.
set -euo pipefail

APPDIR="$(realpath "$1")"
FLOOR="${2:-2.28}"
floor_minor="${FLOOR#2.}"
GLIBCXX_FLOOR=25 # GLIBCXX_3.4.25
CXXABI_FLOOR=11  # CXXABI_1.3.11

over=0
total=0
while IFS= read -r -d '' f; do
    head -c 4 "$f" 2>/dev/null | grep -q $'^\x7fELF' || continue
    total=$((total + 1))
    refs="$(objdump -p "$f" 2>/dev/null | sed -n '/^Version References:/,/^$/p')"
    newest() { grep -oE "$1[0-9]+" <<<"$refs" | sed "s/$1//" | sort -n | tail -1 || true; }
    bad=""
    need="$(newest 'GLIBC_2\.')"
    [ -n "$need" ] && [ "$need" -gt "$floor_minor" ] && bad="$bad GLIBC_2.$need"
    need="$(newest 'GLIBCXX_3\.4\.')"
    [ -n "$need" ] && [ "$need" -gt "$GLIBCXX_FLOOR" ] && bad="$bad GLIBCXX_3.4.$need"
    need="$(newest 'CXXABI_1\.3\.')"
    [ -n "$need" ] && [ "$need" -gt "$CXXABI_FLOOR" ] && bad="$bad CXXABI_1.3.$need"
    if [ -n "$bad" ]; then
        echo "::error::${f#"$APPDIR"/} needs$bad (floor: glibc $FLOOR, GLIBCXX_3.4.$GLIBCXX_FLOOR, CXXABI_1.3.$CXXABI_FLOOR)" >&2
        over=$((over + 1))
    fi
done < <(find "$APPDIR" -type f -print0)

[ "$total" -gt 0 ] || {
    echo "::error::check-glibc-floor: no ELF files under $APPDIR" >&2
    exit 1
}
if [ "$over" -gt 0 ]; then
    echo "check-glibc-floor: FAILED, $over of $total ELF files need a newer glibc or C++ runtime than the floor" >&2
    exit 1
fi
echo "check-glibc-floor: all $total ELF files run on glibc $FLOOR with GLIBCXX_3.4.$GLIBCXX_FLOOR"
