#!/bin/sh
set -eu
set -o pipefail

# Do not leave a truncated include behind when the compiler or linker fails.
# A later incremental build could otherwise embed an empty shader library.
output=$(mktemp "$2.XXXXXX")
trap 'rm -f "$output"' EXIT
trap 'exit 1' HUP INT TERM

xcrun -sdk macosx metal -c "$1" -o - \
  ${MACOSX_DEPLOYMENT_TARGET:+-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET} |
xcrun -sdk macosx metallib - -o - |
xxd -i > "$output"

if [ ! -s "$output" ]; then
  echo 'error: Metal shader compiler produced an empty library' >&2
  exit 1
fi
mv -f "$output" "$2"
