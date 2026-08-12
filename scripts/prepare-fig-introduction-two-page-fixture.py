#!/usr/bin/env python3
"""Create the deterministic two-page ORC 042h introduction fixture."""
from __future__ import annotations
import argparse,hashlib,shutil
from pathlib import Path
EXPECTED='ca3468cc51582b414650f78c0442a228a7bb966f5ff4ac82fe4199432853d03a'
def main()->int:
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('game',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
 try:
  if a.output.exists():shutil.rmtree(a.output)
  a.output.mkdir(parents=True)
  for n in ('MAPZ.DA1','NAME1.DSK'):shutil.copy2(a.game/n,a.output/n)
  d=bytearray((a.game/'SAVE.DA1').read_bytes());d[0x4a0:0x4a2]=(0x042).to_bytes(2,'little');h=hashlib.sha256(d).hexdigest()
  if h!=EXPECTED:raise ValueError(f'FIG two-page introduction fixture differs: {h}')
  (a.output/'SAVE.DA1').write_bytes(d);print(f'FIG two-page introduction fixture: {h}');return 0
 except (OSError,ValueError) as e:p.exit(1,f'FIG two-page introduction fixture: FAIL: {e}\n')
if __name__=='__main__':raise SystemExit(main())
