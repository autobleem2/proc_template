#!/usr/bin/env bash
# The launcher's tools/proc_check.py over this processor, the way an author runs it before publishing: the
# native build (build_linux/, from `ci/build.sh native`) laid out as a processor folder for this machine's
# key, checked against a generated sample Games tree. The checker comes from the launcher's develop branch
# (PROC_CHECK_URL, or a local copy in PROC_CHECK).
#
# Replace the sample-tree generation below with something that actually exercises your processor (real
# Match= extension, a big-enough file to catch a stop-and-restart mid-write, a file it should leave alone).
set -euo pipefail
cd "$(dirname "$0")/.."

BIN=proc_template
URL="${PROC_CHECK_URL:-https://raw.githubusercontent.com/autobleem2/autobleem/develop/tools/proc_check.py}"
WORK="build_linux/proc_check"
rm -rf "$WORK"
mkdir -p "$WORK"
if [ -n "${PROC_CHECK:-}" ]; then
    cp "$PROC_CHECK" "$WORK/proc_check.py"
else
    curl -fsSL "$URL" -o "$WORK/proc_check.py"
fi

key="linux-$(uname -m | sed 's/x86_64/x86_64/; s/aarch64/arm64/; s/i.86/i386/')"
mkdir -p "$WORK/$BIN/bin/$key"
cp package/processor.ini "$WORK/$BIN/"
cp "build_linux/$BIN" "$WORK/$BIN/bin/$key/"

mkdir -p "$WORK/Games"
printf 'sample input' > "$WORK/Games/Sample.example"

python3 "$WORK/proc_check.py" "$WORK/$BIN" --games "$WORK/Games" --key "$key" -v
