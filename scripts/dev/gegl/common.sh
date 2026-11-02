# Shared by the build-gegl-*.sh scripts (POSIX sh; sourced, not run, after setting
# PATCHY_GEGL_SCRIPTS to this folder, which holds sources.lock).

# The operations Patchy's GEGL is built without (docs/gegl-filters.md, "Legal review"):
# each is one source file in GEGL's operation build lists, removed from the list by its
# file name before the build (the files themselves are never opened). Patchy's offered set
# leaves the same names out (excluded_by_name in src/ui/gegl_filters.cpp).
PATCHY_GEGL_DROPPED="common/median-blur common-cxx/focus-blur common-gpl3+/red-eye-removal"

# patchy_gegl_drop_operations <gegl source dir>
patchy_gegl_drop_operations() {
  for entry in $PATCHY_GEGL_DROPPED; do
    list="$1/operations/${entry%%/*}/meson.build"
    file="'${entry#*/}.c'"
    # Exactly one line names the file, and it names nothing else, or the edit could drop
    # another operation.
    [ "$(grep -c -F "$file" "$list")" = 1 ] || { echo "$list: expected one line naming $file" >&2; exit 1; }
    [ "$(grep -F "$file" "$list" | grep -o "\.c'" | wc -l | tr -d ' ')" = 1 ] ||
      { echo "$list: the line naming $file names other files too" >&2; exit 1; }
    sed "/$(printf '%s' "$file" | sed 's/[.+]/\\&/g')/d" "$list" >"$list.patchy" && mv "$list.patchy" "$list"
  done
}

# patchy_gegl_locked <file> [<sha256>]: the file is in sources.lock (with that checksum
# when given).
patchy_gegl_locked() {
  awk -v f="$1" -v s="${2:-}" '!/^#/ && $1 == f && (s == "" || $2 == s) {found = 1} END {exit !found}' \
    "$PATCHY_GEGL_SCRIPTS/sources.lock"
}

# patchy_gegl_sources_note: the paragraph SOURCES.txt ends with.
patchy_gegl_sources_note() {
  cat <<EOF

Modified: GEGL is built without these operations, removed from its build lists by
scripts/dev/gegl/common.sh: $(echo $PATCHY_GEGL_DROPPED | tr '\n' ' ')

GPL-3 operations: included (lib/gegl-0.4/gegl-common-gpl3*, under GEGL's COPYING, the GNU
GPL version 3). The other GEGL and babl files are under the GNU LGPL version 3 or later.

Source code: the complete corresponding source of every library here (the archives listed
above, MSYS2's packaging of the Windows ones, and the scripts that built them) is
PatchyGeglSources.zip, an asset of every Patchy release on GitHub, next to the downloads:
https://github.com/SethRobinson/Patchy/releases (the release of this Patchy version,
https://github.com/SethRobinson/Patchy/releases/tag/v<version>). Patchy's own source, the
GEGL helper's included, is that release's tag in https://github.com/SethRobinson/Patchy.
EOF
}

# patchy_gegl_check_sources <SOURCES.txt>: the packagers' check that a bundle about to ship
# matches the PatchyGeglSources.zip published beside it: every checksum and MSYS2 source
# package it names is in sources.lock, and it carries the current source note (a bundle built
# before it named another place for the source). Prints the problem and returns 1.
patchy_gegl_check_sources() {
  [ -f "$1" ] || { echo "ERROR: $1 does not exist" >&2; return 1; }
  grep -q 'releases/tag/v<version>' "$1" ||
    { echo "ERROR: $1 predates the current source note; rebuild the GEGL bundle (scripts/dev/gegl/)" >&2; return 1; }
  for sha in $(grep -o 'sha256 [0-9a-f]\{64\}' "$1" | cut -d' ' -f2); do
    awk -v s="$sha" '!/^#/ && $2 == s {found = 1} END {exit !found}' "$PATCHY_GEGL_SCRIPTS/sources.lock" ||
      { echo "ERROR: $1 names sha256 $sha, which is not in sources.lock" >&2; return 1; }
  done
  for archive in $(grep -o '[^ ]*\.src\.tar\.zst' "$1"); do
    patchy_gegl_locked "$archive" || { echo "ERROR: $1 names $archive, which is not in sources.lock" >&2; return 1; }
  done
}

# patchy_gegl_license <source> <name>: copies one license text into $STAGE/licenses/<name>,
# failing the build when it is missing (a bundle never ships without its licenses).
patchy_gegl_license() {
  [ -f "$1" ] || { echo "license text not found: $1" >&2; exit 1; }
  cp "$1" "$STAGE/licenses/$2"
}
