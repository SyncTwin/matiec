# SyncTwin fork of matiec

This is a fork of [beremiz/matiec](https://github.com/beremiz/matiec), the IEC 61131-3
compiler (ST/IL/SFC/FBD to ANSI C, `iec2c`). The default branch, `synctwin/main`, is
upstream `master` plus the fixes listed below. Each fix is one commit on top of upstream,
so it can be read, cherry-picked or reviewed on its own.

## What differs from upstream

| Problem | Commit | Upstream |
|---|---|---|
| SFC: actions with `P`/`P1`/`N` qualifiers on the initial step never ran in the first scan (the step-activity edge memory was overwritten before the transition test). | df42bcc | [beremiz/matiec#36](https://github.com/beremiz/matiec/pull/36) |
| SFC: an action body ran only while its `Q` was TRUE, so it never saw the step being deactivated (e.g. `CU := step.X` in an `N` action counted once for good). Now executed one final time after `Q` falls. | e54dcd9 | [beremiz/matiec#37](https://github.com/beremiz/matiec/pull/37) |
| SFC: two or more transitions between the same steps (legal in IEC 61131-3, e.g. "done" and "timeout") were all declared as `FROM_TO_TO`, so the generated C did not compile ("duplicate member"); a named transition (`TRANSITION name FROM ...`) ignored its name. Now a named transition uses its name, the n-th unnamed one between the same steps is `FROM_TO_TO__n` (n >= 2; `__` cannot occur in an IEC identifier). Programs without such transitions generate the same C byte for byte. | a0b17c1 | not yet proposed upstream |
| SFC: in a selection divergence (several transitions leaving one step) every transition whose condition was TRUE fired in the same scan, so two branches became active at once. Now the first transition in priority order (`PRIORITY`, else declaration order, i.e. left to right) clears the step and the others of that step are not crossed in that scan; a simultaneous divergence (one transition `TO (B, C)`) is unchanged. Charts without a selection divergence generate the same C byte for byte. | (this commit) | not yet proposed upstream |
| `iec2c`/`iec2iec` hang forever on some syntax errors (error-recovery loops in the parser and lexer). | 9503d70, 8969216 (regression tests) | not yet proposed upstream |

Compatibility: SFC code generation now follows IEC 61131-3 action control (initial step
active at initialization, action executed while `Q OR F_TRIG(Q)`). Programs that relied on
the old behaviour (no first-scan run of initial-step actions, no final scan of an
action) will behave differently. Programs that do not use these cases generate the same
semantics as before.

## Other branches

- `synctwin/iec2json` - extra stage 4 that prints a JSON model of the program.
- `synctwin/wasm` - WebAssembly (emscripten) build of `iec2json` and `iec2c`.
- `upstream/*` - branches that back the pull requests above; do not build on them.
- `master` - mirror of upstream `master`.

## Building

See [README.build](README.build). In short:

    autoreconf -i
    ./configure
    make

Licensing is unchanged from upstream (see `COPYING`).
