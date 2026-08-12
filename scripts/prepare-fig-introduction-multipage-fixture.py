#!/usr/bin/env python3
"""Create the deterministic seven-page ORC 03ah introduction fixture."""
from __future__ import annotations
import argparse, hashlib, shutil
from pathlib import Path
EXPECTED = "c164eefb2c7fcc9040c93222a2c1c0bdf0644492dcf17275df809f615473c80d"
def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('game',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    try:
        if args.output.exists(): shutil.rmtree(args.output)
        args.output.mkdir(parents=True)
        for name in ('MAPZ.DA1','NAME1.DSK'): shutil.copy2(args.game/name,args.output/name)
        data=bytearray((args.game/'SAVE.DA1').read_bytes());data[0x4a0:0x4a2]=(0x03a).to_bytes(2,'little')
        actual=hashlib.sha256(data).hexdigest()
        if actual != EXPECTED: raise ValueError(f'FIG multipage introduction fixture differs: {actual}')
        (args.output/'SAVE.DA1').write_bytes(data);print(f'FIG multipage introduction fixture: {actual}');return 0
    except (OSError,ValueError) as error: parser.exit(1,f'FIG multipage introduction fixture: FAIL: {error}\n')
if __name__=='__main__':raise SystemExit(main())
