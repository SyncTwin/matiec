# iec2c: метки `{attribute ...}` — зависание и фикс

База: 3a41303. Ветка: `cloud/attr-hang`. Фикс: коммит `503b15a` («Ignore {attribute ...} pragmas in lexer; guard yyerrok…»), `git diff 3a41303 503b15a -- stage1_2 absyntax_utils` (3 файла, +77/−1).

## Что вешает (строки по 3a41303)
- `stage1_2/iec_flex.ll:629,1008` — любой `{...}` = прагма, отдаётся в bison как `pragma_token` во всех inclusive-состояниях лексера (header/vardecl/st/il/sfc). В `body_state` (`iec_flex.ll:1007`) прагма буферизуется и пересканируется уже в `sfc_state`.
- Грамматика принимает `pragma_token` только между POU (`iec_bison.yy:1679`) и в списках инструкций ST/IL (`iec_bison.yy:6724-6728,7886-7890`). В VAR-блоках, между заголовком и VAR и в SFC — синтаксическая ошибка.
- ЗАВИСАНИЕ: `stage1_2/iec_bison.yy:5552-5553` `sfc_network: sfc_network error {...; yyerrok;}` не потребляет lookahead; `yyerrok` (yyerrstatus=0) запрещает bison отбросить токен → та же ошибка на том же `pragma_token` → то же правило → бесконечный цикл с бесконечной печатью «unexpected token after SFC network».
- CRASH (SIGSEGV, доп. находка): `iec_bison.yy:4037-4038` при ошибке в `VAR_OUTPUT` кладёт NULL в список; `absyntax_utils/add_en_eno_param_decl.cc:87` его разыменовывает (gdb bt).
- Ложный «ok» на пине: метка в теле ST (`attr_in_body.st`) компилируется, но её текст дословно попадает в `POUS.c` → невалидный C.

## Фикс (минимальный, поверх 3a41303)
1. `iec_flex.ll`: новое определение `attribute_pragma` = `{` пробелы* `attribute` пробел `[^}]*` `}`; правило «игнорировать» перед общими правилами прагм, во всех состояниях кроме `body_state` (там текст буферизуется и игнорируется при пересканировании) и `comment_state`. → цель (б): метки компилируются. Номера строк в ошибках сохраняются (проверено вручную на метке в 2 строки).
2. `iec_bison.yy`: защита `yyerrok` от повторного срабатывания на том же lookahead (`%initial-action` + `yyerrok_made_progress()`), перенесена из `synctwin/fix-parser-hang` без остальных её правок. → цель (а) для любых прагм/токенов в SFC.
3. `add_en_eno_param_decl.cc:87`: пропуск NULL-элементов.

## Корпус (`tests/corpus_attr/run.sh <build-dir>`, timeout 30 c на файл)
| файл | пин 3a41303 | fix-parser-hang | фикс 503b15a |
|---|---|---|---|
| seed_attr_before_program | ok | ok | ok |
| attr_before_fb | ok | ok | ok |
| attr_before_var_block (между PROGRAM и VAR) | syntax_error | syntax_error | ok |
| seed_attr_in_var_input | syntax_error | syntax_error | ok |
| attr_in_var (VAR) | syntax_error | syntax_error | ok |
| attr_in_var_output | **CRASH(139)** | **CRASH(139)** | ok |
| attr_in_var_external | syntax_error | syntax_error | ok |
| attr_before_initial_step (SFC) | **HANG** | syntax_error | ok |
| attr_before_step (SFC) | **HANG** | syntax_error | ok |
| attr_before_transition (SFC) | **HANG** | syntax_error | ok |
| attr_before_action (SFC) | **HANG** | syntax_error | ok |
| attr_in_body (ST) | ok* | ok* | ok |
| attr_multi_field | syntax_error | syntax_error | ok |
| attr_in_comment | ok | ok | ok |
| attr_in_and_out_comment | syntax_error | syntax_error | ok |
| pragma_plain_before_step (`{x}` в SFC) | **HANG** | syntax_error | syntax_error |
| pragma_plain_in_var_output (`{x}`) | **CRASH(139)** | **CRASH(139)** | syntax_error |
| base_no_attr / base_sfc (контроль) | ok / ok | ok / ok | ok / ok |

\* ok, но текст метки попадает в сгенерированный C. Итог: пин 5 HANG + 2 CRASH; фикс 0 HANG, 0 CRASH, 17/17 ожидаемых результатов.

## Тесты matiec (запуск в каждой сборке: `cd <build>/tests/<dir> && ./runtests`)
| набор | пин | фикс |
|---|---|---|
| tests/initialization | 38/38 OK | 38/38 OK |
| tests/syntax/identifier | 0/308 OK | 0/308 OK |

`syntax/identifier` падает целиком и на пине: iec2iec отвергает сам `basic_code.test` семантическими ошибками («Data type mismatch for 'AND' operator» и т.п.) — тест устарел, к фиксу не относится. `make check` в Makefile.am нет; tests/*.xml, tests/syntax/{sfc,configuration,enumeration} — не исполняемые описания.
Доп.: `tests/hang/*.st` из fix-parser-hang (7 шт.): пин 7 HANG, фикс 4 HANG (action_bad_name, end_transition_in_function, transition_bad_destination, transition_hang), fix-parser-hang 0 HANG. Это зависания без меток, вне объёма задачи.

## Вопросы
1. Игнорировать только `{attribute ...}` или все неизвестные прагмы внутри VAR/SFC? Сейчас обычная `{x}` там — синтаксическая ошибка (без зависания), как и в стандарте matiec.
2. Метки теряются (в AST не попадают). Если они нужны дальше (напр. для экспорта) — нужен разбор в грамматике, это уже не малый фикс.
3. Переносить ли остальные правки fix-parser-hang (4 оставшихся зависания из tests/hang)?
