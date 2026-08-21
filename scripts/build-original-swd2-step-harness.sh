#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -ne 1 ]]; then
    echo "usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi
if ! command -v nasm >/dev/null 2>&1; then
    echo "nasm is required to build the original SWD2 step harnesses" >&2
    exit 1
fi
output="$1"
mkdir -p "$output"
nasm -f bin -dRPG_STEP=1 "$root/scripts/original-swd2-step.asm" \
    -o "$output/RPGSTEP.COM"
nasm -f bin -dRPG_TITLE_STEP=1 "$root/scripts/original-swd2-step.asm" \
    -o "$output/RPGMT.COM"
nasm -f bin -dFIG_STEP=1 "$root/scripts/original-swd2-step.asm" \
    -o "$output/FIGSTEP.COM"

rpg="$(shasum -a 256 "$output/RPGSTEP.COM" | awk '{print $1}')"
rpg_mt="$(shasum -a 256 "$output/RPGMT.COM" | awk '{print $1}')"
fig="$(shasum -a 256 "$output/FIGSTEP.COM" | awk '{print $1}')"
expected_rpg="e169d08e8bf30e8a06597c62897ea453f9edbf6eca09f5f7c65f461767ba8bc1"
expected_rpg_mt="626132a2380464aad548b300384004f51d91ba963cf16689e0638d3709cfb6c7"
expected_fig="b5ef8838d97cfe81882d4498559c07e4496f0e7c71443bc99b1a9cfe9c49ac0f"
if [[ "$rpg" != "$expected_rpg" ]]; then
    echo "RPGSTEP.COM digest differs: $rpg" >&2
    exit 1
fi
if [[ "$rpg_mt" != "$expected_rpg_mt" ]]; then
    echo "RPGMT.COM digest differs: $rpg_mt" >&2
    exit 1
fi
if [[ "$fig" != "$expected_fig" ]]; then
    echo "FIGSTEP.COM digest differs: $fig" >&2
    exit 1
fi
echo "Original SWD2 RPG step harness: $output/RPGSTEP.COM ($rpg)"
echo "Original SWD2 RPG title-load harness: $output/RPGMT.COM ($rpg_mt)"
echo "Original SWD2 FIG step harness: $output/FIGSTEP.COM ($fig)"
