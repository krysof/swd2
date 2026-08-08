# SWD2 completion rule

- Never claim that the rewrite is 100% complete merely because a build,
  regression suite, commit, or checkpoint deployment succeeded.
- `porting-status.json` is the completion source of truth. A 100% claim is
  allowed only after `./scripts/audit-completion.py --require-complete` exits
  successfully in the current worktree.
- A deployment before that point is a checkpoint, not completion. After a
  checkpoint, continue with the highest-priority non-verified gate that can be
  advanced with the available evidence.
- Keep each verified gate backed by repository evidence. Do not change a gate
  to `verified` to improve the count; satisfy every acceptance item first.

