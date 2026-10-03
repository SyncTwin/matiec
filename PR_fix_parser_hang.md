# Fix infinite loops in parser/lexer syntax error recovery

## Problem

`iec2c` and `iec2iec` can hang forever (100% CPU, no output, sometimes growing
memory) on certain erroneous inputs. Minimal example:

```
PROGRAM P
VAR a : BOOL; END_VAR
INITIAL_STEP S0: END_STEP
TRANSITION FROM S0 TO S0 := x.y; END_TRANSITION
END_PROGRAM
```

A mutation fuzzer over the files in `tests/` (random token deletion /
insertion / replacement) produced a hang in ~7% of the cases (106 of 1500).

## Root causes

Tracing the parser (`YYDEBUG`) showed three distinct kinds of non-terminating
error recovery, plus one lexer-only loop.

1. **Lexer state change right after `error`.** In

   ```
   TRANSITION ... TO error {cmd_goto_body_state();} transition_condition END_TRANSITION
   ACTION error {cmd_goto_body_state();} action_body END_ACTION
   ```

   bison discards the look-ahead, pops and re-shifts `error`, and re-runs the
   mid-rule action. flex then re-enters `body_state`, which unputs the text it
   scanned, so it returns the same `start_ST_body_token` / `start_IL_body_token`
   over and over. Bison discards it each time and the input never advances.

2. **`transition_condition: ASSIGN expression error { ...; yyerrok; }`** never
   consumes the offending token (the `.` in `x.y`). The same token immediately
   causes another error in the enclosing `transition`, which reaches the rule
   from cause 1. This is what hangs the minimal example above.

3. **`X: ... error { ...; yyerrok; }` with no progress.** Such rules leave the
   look-ahead in place. If that token is still invalid after the reduction, the
   same error rule is chosen again, `yyerrok` resets recovery again, and so on.
   For example, `sfc_network: sfc_network error` and
   `resource_declaration_list: resource_declaration_list error` loop forever and
   print the same message each time. The grammar has 399 `yyerrok` calls and 73
   rules that end in `error` with `yyerrok` but no `yyclearin`. These are
   difficult to audit one by one.

4. **Lexer loop on `END_TRANSITION` / `END_ACTION` inside a POU body.** The
   `<il_state,st_state>` rules always popped the state on these keywords. Inside
   a FUNCTION/FB/PROGRAM body this pops back to `vardecl_list_state`. That state
   pushes `body_state` on the same text, which returns `start_IL_body_token`,
   which pops again, and so on.

## Changes

* `stage1_2/iec_bison.yy`
  * The `TO error` transition rules now skip to `END_TRANSITION`.
    `ACTION error` now enters `body_state` only after the `:` has been
    shifted. A grammar comment records why a lexer-state mid-rule action must
    never directly follow `error`.
  * `transition_condition`: a missing `;` (`:= a END_TRANSITION`) is now
    detected by a plain production, without error recovery. This adds no
    conflicts because the follow set is only `END_TRANSITION`. Anything invalid
    after the expression is skipped up to and including the `;` with the
    existing message "invalid expression defined in ST condition of
    transition declaration.".
  * **Systemic guard for cause 3:** `yyerrok` is redefined in
    `%initial-action`. That code is emitted inside `yyparse()`, after bison's
    own `#define yyerrok`, so all 399 call sites are covered without touching
    them. The guarded `yyerrok` is skipped when the current look-ahead is the
    same token (same location `order`) as the last time `yyerrok` ran. Bison
    then stays in recovery mode and discards that token on the next error. This
    guarantees progress through the input whatever the shape of the error
    rule.
  * `print_err_msg()` no longer prints the exact same message twice in a row for
    the same token end location. This can happen once while the guard above
    takes effect.
* `stage1_2/iec_flex.ll`: `END_TRANSITION` / `END_ACTION` pop the state only when
  the previous state is `sfc_state`. Elsewhere they are returned to bison as
  ordinary tokens, so a proper syntax error is reported. This uses
  `yy_top_state()`, so `%option noyy_top_state` was removed.
* `tests/hang/`: 7 regression inputs (one for each loop above) and
  `tests/hang/run.sh`. The script checks that each input produces an error
  message and a non-zero exit code within a timeout.

## Result

```
$ ./iec2c -I lib tests/hang/transition_hang.st
tests/hang/transition_hang.st:4: error: invalid expression defined in ST condition of transition declaration.

1 error(s) found. Bailing out!
$ echo $?
1          # ~0.08 s; previously: hung forever
```

## Testing

* `tests/hang/run.sh`: all 7 inputs hang before this change (killed by
  timeout). After it, all 7 terminate with an error and exit code 1.
* All other `.st`/`.txt` files in `tests/` (42 files), with both `iec2c` and
  `iec2iec`, give identical results before and after the change: the same exit
  codes, byte-identical stdout/stderr and byte-identical generated output.
  This includes the files that are expected to fail.
* Valid SFC code with ST and IL transition conditions and actions produces
  byte-identical output.
* `bison` reports no conflicts, before or after.
* Fuzzing with 6000 mutated inputs (3 seeds): 0 hangs, down from 106 per 1500
  cases before the change. A few inputs still crash with SIGSEGV. They crash
  the same way without this change and are unrelated to error recovery (for
  example, `tests/syntax/configuration/configuration.txt` already crashes on
  `master`), so they are left for a separate fix.
