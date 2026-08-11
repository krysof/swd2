#!/usr/bin/env python3
"""Stage FIG player attack-buff expiry after a direct item."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


EXPECTED_SAVE = "32c63d8fee0304b1f81899a0c402d7d693d69a7a44f0d73c80433d457f36755c"
EXPECTED_MISSING_MEDIUM_SAVE = \
    "967403714f7d445c8f067fe09a8920163a9c39b2f3b2263b45e80180646d4e85"
EXPECTED_STATUS_SAVE = \
    "e09999f59339bd32ec0272c768599ea3bd67ec4ea1853489731d3c4b231255e3"
EXPECTED_SUPPORT_SAVE = \
    "9fdac8fcd88f5fa5c095544e193e4564f193a69ce3132fe9dd88d19a23fea2be"
EXPECTED_COMPOSITE_SUPPORT_SAVE = \
    "57f00016204f347df49a4b116156ad6f577bfa606a5f208ccaea35095ca6c627"
EXPECTED_DISPEL_SAVE = \
    "dd114978016573e71141d827b2786bfc57da9291d61a5877dbfee42d385983f4"
EXPECTED_BARRIER_SAVE = \
    "98ae98e3b129b5ba19ffc3bbac126ade0ae50ab8c182b849341d1ec3b7d2e388"
EXPECTED_MEDIUM_SAVE = \
    "92853a673585baa5245fcfd5dab055a5e6d680fe5ee3de80126ab36ab16031cf"
EXPECTED_COMPOSITE_MEDIA_SAVE = \
    "dca08598ba229a53c57866eec93676f2628a5f69c030c65bc96e6f966919bbd0"
EXPECTED_COMPOSITE_DAMAGE_SAVE = \
    "5830d3dd67c76b9dd7f7d0f508e48d400b0ec1f552975c9fe25e6a9b6e5ec339"
EXPECTED_COMPOSITE_MISSING_MEDIA_SAVE = \
    "71918bc80a20815ec32427579f4755e15c9987a62760d1936102881a93bbbc61"
EXPECTED_AF_MEDIUM_SAVE = \
    "8d0feca88f2056ae83aa3c3ab266fed141e3efbedd870cab64a0225432736f60"
EXPECTED_B0_MEDIUM_SAVE = \
    "ebdbb4a94f345f23d17760d74b10fb731ee03d2d7b17054e85bb18484083e09f"
EXPECTED_EMPTY_MEDIUM_SAVE = \
    "5c9f3697a84462c87cd85bf027ccec8ce5730b14f2e9b3d3038d0188ffd54f7a"
EXPECTED_EMPTY_AF_MEDIUM_SAVE = \
    "c05b9f1c76e11327f59346edeb036b01fe6678ce2240053d1b44d05fdd2315d5"
EXPECTED_EMPTY_B0_MEDIUM_SAVE = \
    "e82af38e4d3f5559856ffd83fc03f40e66383ebdd61e27ea991388d109fb770b"
EXPECTED_DISMISS_MEDIUM_SAVE = \
    "e781ae6198d4fb02fe87f1132e5f229ce89877f38a937745b1e9d268f6a09a9f"
EXPECTED_DISMISS_AF_MEDIUM_SAVE = \
    "379f56d71ea18f3ac0c6b99111567e98ae03b3c9e4cf4f0dbe48dbe0725112d7"
EXPECTED_DISMISS_B0_MEDIUM_SAVE = \
    "5a2f054b9292e55a389e54b98acf1e88198d421b753e2a8c3c126a933cc9dda6"
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
        help="replace direct damage item 192 with item 226/effect 63h")
    finish.add_argument(
        "--status-item", action="store_true",
        help="replace direct damage item 192 with item 186/effect 69h")
    finish.add_argument(
        "--support-item", action="store_true",
        help="replace direct damage item 192 with item 190/effect 01h")
    finish.add_argument(
        "--composite-support-item", action="store_true",
        help="replace direct damage item 192 with item 219/effect 6bh")
    finish.add_argument(
        "--dispel-item", action="store_true",
        help="replace direct damage item 192 with item 211/effect 61h")
    finish.add_argument(
        "--barrier-item", action="store_true",
        help="replace direct damage item 192 with item 204/effect 62h")
    finish.add_argument(
        "--medium-item", action="store_true",
        help="replace direct damage item 192 with item 191/effect 31h")
    finish.add_argument(
        "--composite-media-item", action="store_true",
        help="replace direct damage item 192 with item 195/effect 6bh")
    finish.add_argument(
        "--composite-damage-item", action="store_true",
        help="replace direct damage item 192 with item 230/effect 6bh")
    finish.add_argument(
        "--composite-missing-media-item", action="store_true",
        help="replace direct damage item 192 with item 225/effect 6bh")
    finish.add_argument(
        "--medium-item-af", action="store_true",
        help="replace direct damage item 192 with item 206/effect 3bh")
    finish.add_argument(
        "--medium-item-b0", action="store_true",
        help="replace direct damage item 192 with item 222/effect 3ch")
    finish.add_argument(
        "--empty-medium-item", action="store_true",
        help="replace direct damage item 192 with item 193/effect 3dh")
    finish.add_argument(
        "--empty-medium-item-af", action="store_true",
        help="replace direct damage item 192 with item 207/effect 3eh")
    finish.add_argument(
        "--empty-medium-item-b0", action="store_true",
        help="replace direct damage item 192 with item 223/effect 3fh")
    finish.add_argument(
        "--dismiss-medium-item", action="store_true",
        help="install AE with item 191 before item 193 and buff expiry")
    finish.add_argument(
        "--dismiss-medium-item-af", action="store_true",
        help="install AF with item 206 before item 207 and buff expiry")
    finish.add_argument(
        "--dismiss-medium-item-b0", action="store_true",
        help="install B0 with item 222 before item 223 and buff expiry")
    args = parser.parse_args()
    try:
        if args.output.exists():
            shutil.rmtree(args.output)
        shutil.copytree(args.game, args.output)

        save_path = args.output / "SAVE.DA1"
        save = bytearray(save_path.read_bytes())
        actor = 0x106
        word(save, 0x10, 1)             # one active party member
        word(save, 0x49C,
             0x100C if (args.dismiss_medium_item or
                        args.dismiss_medium_item_af or
                        args.dismiss_medium_item_b0) else 0x1020)
        word(save, 0x49E, 20)           # stable physical/damage draws
        word(save, 0x4A0, 392)          # one-monster test formation

        # Ability 38 applies effect 67 (力量加強).  The selected random-buffer
        # window gives its minimum duration: two deliberately non-lethal
        # physical actions followed by direct damage item 192, or by the
        # optional item-226 missing-medium return. Both leave 1138's pose zero
        # live when 0c41 enters 0da7.
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
        word(save, actor + 0x6D, 38)
        word(save, actor + 0x6E, 1)
        if args.dismiss_medium_item or args.dismiss_medium_item_af or \
                args.dismiss_medium_item_b0:
            save[actor + 0x6D:actor + 0x6D + 50] = bytes(50)
            save[actor + 0x6D] = 38
        if args.support_item or args.composite_support_item:
            word(save, actor + 0x2D, 100)
            word(save, actor + 0x2F, 1000)
            word(save, actor + 0x35, 1000)
            word(save, actor + 0x37, 1000)
            word(save, actor + 0x55, 1000)
            word(save, actor + 0x57, 1000)
        for offset in range(0x382, 0x3E6, 2):
            word(save, offset, 0)
        final_item = (
            222 if args.dismiss_medium_item_b0 else
            206 if args.dismiss_medium_item_af else
            191 if args.dismiss_medium_item else
            223 if args.empty_medium_item_b0 else
            207 if args.empty_medium_item_af else
            193 if args.empty_medium_item else
            222 if args.medium_item_b0 else
            206 if args.medium_item_af else
            225 if args.composite_missing_media_item else
            230 if args.composite_damage_item else
            195 if args.composite_media_item else
            191 if args.medium_item else
            204 if args.barrier_item else
            211 if args.dispel_item else
            219 if args.composite_support_item else
            190 if args.support_item else
            186 if args.status_item else
            226 if args.missing_medium else 192)
        word(save, 0x382, final_item)
        if args.dismiss_medium_item or args.dismiss_medium_item_af or \
                args.dismiss_medium_item_b0:
            word(save, 0x384,
                 223 if args.dismiss_medium_item_b0 else
                 207 if args.dismiss_medium_item_af else 193)

        save_digest = sha256(save)
        mapz_digest = sha256((args.output / "MAPZ.DA1").read_bytes())
        name_digest = sha256((args.output / "NAME1.DSK").read_bytes())
        expected_save = (
            EXPECTED_DISMISS_B0_MEDIUM_SAVE if args.dismiss_medium_item_b0 else
            EXPECTED_DISMISS_AF_MEDIUM_SAVE if args.dismiss_medium_item_af else
            EXPECTED_DISMISS_MEDIUM_SAVE if args.dismiss_medium_item else
            EXPECTED_EMPTY_B0_MEDIUM_SAVE if args.empty_medium_item_b0 else
            EXPECTED_EMPTY_AF_MEDIUM_SAVE if args.empty_medium_item_af else
            EXPECTED_EMPTY_MEDIUM_SAVE if args.empty_medium_item else
            EXPECTED_B0_MEDIUM_SAVE if args.medium_item_b0 else
            EXPECTED_AF_MEDIUM_SAVE if args.medium_item_af else
            EXPECTED_COMPOSITE_DAMAGE_SAVE
            if args.composite_damage_item else
            EXPECTED_COMPOSITE_MISSING_MEDIA_SAVE
            if args.composite_missing_media_item else
            EXPECTED_COMPOSITE_MEDIA_SAVE if args.composite_media_item else
            EXPECTED_MEDIUM_SAVE if args.medium_item else
            EXPECTED_BARRIER_SAVE if args.barrier_item else
            EXPECTED_DISPEL_SAVE if args.dispel_item else
            EXPECTED_COMPOSITE_SUPPORT_SAVE
            if args.composite_support_item else
            EXPECTED_SUPPORT_SAVE if args.support_item else
            EXPECTED_STATUS_SAVE if args.status_item else
            EXPECTED_MISSING_MEDIUM_SAVE if args.missing_medium else
            EXPECTED_SAVE)
        if save_digest != expected_save:
            raise ValueError(
                f"FIG player-buff-expiry-item SAVE differs: {save_digest}")
        if mapz_digest != EXPECTED_MAPZ or name_digest != EXPECTED_NAME:
            raise ValueError(
                "FIG player-buff-expiry-item companion save files differ")
        save_path.write_bytes(save)
        print(
            "FIG player-buff-expiry-item fixture: "
            f"SAVE={save_digest} MAPZ={mapz_digest} NAME={name_digest}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(
            1, f"FIG player-buff-expiry-item fixture: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
