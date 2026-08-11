#!/usr/bin/env python3
"""Stage FIG player attack-buff expiry after a learned ability."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "1ac72124e46138c5a42b89882a008d87863e6a3c3c805ff993a828a1152c52dc"
EXPECTED_MISSING_MEDIUM_SAVE = \
    "f6bef40bce5a59c8706dad949ba8caf19c93096b405b1ace638feae80caf8edd"
EXPECTED_RESISTED_SAVE = \
    "776db17ac748099e6347b01c93bb1b4457427ec49a0450d1951edac270cec7f2"
EXPECTED_DISMISS_MEDIUM_SAVE = \
    "417d52d03978270e06ccd681bc18576186fa28edd79e625665dc8cba4b41b117"
EXPECTED_DISMISS_AF_MEDIUM_SAVE = \
    "7f5a22dbcf50a5fcc8e5ce6e4f625833da2714360dba1cefffd849a70c0d18c7"
EXPECTED_DISMISS_B0_MEDIUM_SAVE = \
    "960c0a3fd50bc43efb7a31982406d1b9f755b723f1a630451f0ba6680eb8784f"
EXPECTED_EMPTY_MEDIUM_SAVE = \
    "be254bfb138affa44a1df431c1d5c1ecf0c8005fed6b51290ce2cc19f95858c8"
EXPECTED_EMPTY_AF_MEDIUM_SAVE = \
    "0fd37f3269b2ace38a5db275b1ccdbc163622d241683a1033e602c250996092e"
EXPECTED_EMPTY_B0_MEDIUM_SAVE = \
    "e922c7778ae0240e2b04b065c9bf2e9d01d2b34eb32b0fb1bf5f3eaae8ef3d2f"
EXPECTED_MEDIUM_SAVE = \
    "64dcf92c38b163351584ba80bf3849568025f080bfa6c1d1005f2b0a3f75d5d1"
EXPECTED_AF_MEDIUM_SAVE = \
    "2ee0c9bb775ce93d9e40dd1d1b237fbdd6bc0da51074fd56ff814faeda27c764"
EXPECTED_B0_MEDIUM_SAVE = \
    "56e59407db059afcc377855a3abb75c6b7cc00dcf89db09abbd965e95898ee9e"
EXPECTED_MAPZ = "b9e31ff2d3dac2efbd314b6dfe7426eab88c7757315a1ae10695aad961eea917"
EXPECTED_NAME = "98bed0fc2855bdd752f914a9dffcf5b799a66e2501ac7a5b19cd7989dd69b0ba"


def word(data: bytearray, offset: int, value: int) -> None:
    data[offset:offset + 2] = value.to_bytes(2, "little")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    finish = parser.add_mutually_exclusive_group()
    finish.add_argument(
        "--missing-medium", action="store_true",
        help="replace the finishing ability with ability 86/effect 63h")
    finish.add_argument(
        "--resisted", action="store_true",
        help="replace the finishing ability with ability 6/effect 5eh")
    finish.add_argument(
        "--dismiss-medium", action="store_true",
        help="install medium AE with ability 51, then expire on ability 53")
    finish.add_argument(
        "--dismiss-medium-af", action="store_true",
        help="install medium AF with ability 66, then expire on ability 67")
    finish.add_argument(
        "--dismiss-medium-b0", action="store_true",
        help="install medium B0 with ability 82, then expire on ability 83")
    finish.add_argument(
        "--empty-medium", action="store_true",
        help="expire on ability 53 while medium AE is absent")
    finish.add_argument(
        "--empty-medium-af", action="store_true",
        help="expire on ability 67 while medium AF is absent")
    finish.add_argument(
        "--empty-medium-b0", action="store_true",
        help="expire on ability 83 while medium B0 is absent")
    finish.add_argument(
        "--medium", action="store_true",
        help="expire on ability 51 while installing medium AE")
    finish.add_argument(
        "--medium-af", action="store_true",
        help="expire on ability 66 while installing medium AF")
    finish.add_argument(
        "--medium-b0", action="store_true",
        help="expire on ability 82 while installing medium B0")
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        save_path = args.output / "SAVE.DA1"
        save = bytearray(save_path.read_bytes())
        actor = 0x106
        word(save, 0x10, 1)             # one active party member
        word(save, 0x49C, 0x1020)       # minimum attack-buff duration draw
        word(save, 0x49E, 20)           # stable physical/damage draws
        word(save, 0x4A0, 392)          # one-monster test formation

        # Ability 38 applies effect 67 (力量加強).  The selected random-buffer
        # window gives its minimum duration: two deliberately non-lethal
        # physical actions followed by learned ability 1.  That final ability
        # defeats the monster on the same turn that 0c41 enters 0da7.
        word(save, actor + 0x0C, 1)
        word(save, actor + 0x0E, 60000)
        word(save, actor + 0x2D, 60000)
        word(save, actor + 0x2F, 60000)
        word(save, actor + 0x31, 1)
        word(save, actor + 0x33, 60000)
        word(save, actor + 0x35, 60000)
        word(save, actor + 0x37, 60000)
        word(save, actor + 0x41, 1)
        word(save, actor + 0x43, 60000)
        word(save, actor + 0x55, 60000)
        word(save, actor + 0x57, 60000)
        word(save, actor + 0x5D, 60000)
        word(save, actor + 0x5F, 60000)
        if args.dismiss_medium or args.dismiss_medium_af or \
                args.dismiss_medium_b0:
            save[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
            save[actor + 0x6D:actor + 0x70] = bytes(
                (38, 82, 83) if args.dismiss_medium_b0 else
                (38, 66, 67) if args.dismiss_medium_af else (38, 51, 53))
        else:
            word(save, actor + 0x6D, 38)
            word(save, actor + 0x6E,
                 82 if args.medium_b0 else
                 66 if args.medium_af else
                 51 if args.medium else
                 83 if args.empty_medium_b0 else
                 67 if args.empty_medium_af else
                 53 if args.empty_medium else
                 6 if args.resisted else 86 if args.missing_medium else 1)

        save_digest = sha256(save)
        mapz_digest = sha256((args.output / "MAPZ.DA1").read_bytes())
        name_digest = sha256((args.output / "NAME1.DSK").read_bytes())
        expected_save = (
            EXPECTED_B0_MEDIUM_SAVE if args.medium_b0 else
            EXPECTED_AF_MEDIUM_SAVE if args.medium_af else
            EXPECTED_MEDIUM_SAVE if args.medium else
            EXPECTED_DISMISS_B0_MEDIUM_SAVE if args.dismiss_medium_b0 else
            EXPECTED_DISMISS_AF_MEDIUM_SAVE if args.dismiss_medium_af else
            EXPECTED_DISMISS_MEDIUM_SAVE if args.dismiss_medium else
            EXPECTED_EMPTY_B0_MEDIUM_SAVE if args.empty_medium_b0 else
            EXPECTED_EMPTY_AF_MEDIUM_SAVE if args.empty_medium_af else
            EXPECTED_EMPTY_MEDIUM_SAVE if args.empty_medium else
            EXPECTED_RESISTED_SAVE if args.resisted else
            EXPECTED_MISSING_MEDIUM_SAVE if args.missing_medium else
            EXPECTED_SAVE)
        if save_digest != expected_save:
            raise ValueError(
                f"FIG player-buff-expiry-ability SAVE differs: {save_digest}")
        if mapz_digest != EXPECTED_MAPZ or name_digest != EXPECTED_NAME:
            raise ValueError(
                "FIG player-buff-expiry-ability companion save files differ")
        save_path.write_bytes(save)
        print(
            "FIG player-buff-expiry-ability fixture: "
            f"SAVE={save_digest} MAPZ={mapz_digest} NAME={name_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG player-buff-expiry-ability fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
