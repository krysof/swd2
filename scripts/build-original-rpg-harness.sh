#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -ne 1 ]]; then
    echo "usage: $0 OUTPUT.COM" >&2
    exit 2
fi
if ! command -v nasm >/dev/null 2>&1; then
    echo "nasm is required to build the original RPG capture harness" >&2
    exit 1
fi
output="$1"
mkdir -p "$(dirname "$output")"
nasm -f bin "$root/scripts/original-rpg-oc.asm" -o "$output"
actual="$(shasum -a 256 "$output" | awk '{print $1}')"
expected="b884ff334ce081992422aa9a6b7e6a31685443c10869fa819d0fcb2a4e92b04b"
if [[ "$actual" != "$expected" ]]; then
    echo "RPGOC.COM digest differs: $actual" >&2
    rm -f "$output"
    exit 1
fi
echo "Original RPG capture harness: $output ($actual)"
