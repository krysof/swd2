# DBOPL vendored source

This directory contains the DOSBox DBOPL OPL2/OPL3 synthesizer used by
DOSBox-X 2026.07.02 when `oplemu=default` or `oplemu=fast`. The source was
copied from the official DOSBox-X tag `dosbox-x-v2026.07.02`; only the
DOSBox mixer/serialization wrapper was removed so the portable renderer can
drive `DBOPL::Chip` directly. The synthesis implementation is otherwise
unchanged.

Upstream source: <https://github.com/joncampbell123/dosbox-x/tree/dosbox-x-v2026.07.02/src/hardware>

Copyright (C) 2002-2021 The DOSBox Team, licensed under the GNU General
Public License version 2 or later, reproduced in `LICENSE`.
