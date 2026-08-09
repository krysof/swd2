#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -ne 1 ]]; then
    echo "usage: $0 OUTPUT.COM" >&2
    exit 2
fi
if ! command -v nasm >/dev/null 2>&1; then
    echo "nasm is required to build the original FIG capture harness" >&2
    exit 1
fi
output="$1"
mkdir -p "$(dirname "$output")"
nasm -f bin "$root/scripts/original-fig-if.asm" -o "$output"
actual="$(shasum -a 256 "$output" | awk '{print $1}')"
expected="f6834ff65c61c2f647343f6f1e03b106a9625b729c5e39025aac86c81aaa0bba"
if [[ "$actual" != "$expected" ]]; then
    echo "FIGIF.COM digest differs: $actual" >&2
    rm -f "$output"
    exit 1
fi
echo "Original FIG capture harness: $output ($actual)"
