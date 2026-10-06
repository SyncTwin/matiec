# PROGRESS — iec2c attribute hang

Ветка: `cloud/attr-hang` (от 6c70e7e, база 3a41303).

- [шаг 0] Старт. Ветка `cloud/attr-hang` создана локально от `origin/cloud/attr-hang`. Есть bison 3.8.2, g++, autoreconf; flex отсутствует — ставлю из apt.
- [шаг 1] Сборки вне дерева: `git worktree add /tmp/m_pin 3a41303`, `git worktree add /tmp/m_fph origin/synctwin/fix-parser-hang`; в каждой `autoreconf -i && ./configure && make -j8` — rc=0 обе (flex 2.6.4 поставлен через `apt-get update && apt-get install -y flex`).
- [шаг 2] Корпус `tests/corpus_attr/` (17 файлов: 2 зачина + 13 с метками + 2 базовых без меток) и прогонщик `tests/corpus_attr/run.sh <build-dir>`.
  Пин: 4 ЗАВИСАНИЯ (все SFC: метка перед STEP/INITIAL_STEP/TRANSITION/ACTION), 1 CRASH SIGSEGV (метка в VAR_OUTPUT), 7 синт. ошибок, 5 ok.
  fix-parser-hang: зависаний нет (SFC → синт. ошибка), CRASH в VAR_OUTPUT остаётся.
- [шаг 3] Причина (строки — по 3a41303):
  * `stage1_2/iec_flex.ll:629` — `{...}` любого вида = `pragma`; `iec_flex.ll:1008` отдаёт его в bison как `pragma_token` во всех inclusive-состояниях (`header_state`, `vardecl_state`, `st/il/sfc_state`, `iec_flex.ll:486-520`). `iec_flex.ll:1007` — в `body_state` прагма буферизуется и затем пересканируется в `sfc_state` → тот же `pragma_token`.
  * Грамматика принимает `pragma_token` только между POU (`iec_bison.yy:1679`) и в списках инструкций ST/IL (`iec_bison.yy:6724,6728,7886,7890`). Внутри VAR-блока, между заголовком и VAR, и в SFC — синтаксическая ошибка.
  * Зависание: `iec_bison.yy:5552-5553` `sfc_network: sfc_network error {...; yyerrok;}`. Правило не потребляет lookahead (`pragma_token`), `yyerrok` сбрасывает режим восстановления, тот же токен снова даёт ошибку → снова это правило → бесконечно (видно по бесконечной печати «unexpected token after SFC network»). Токен не отбрасывается, т.к. bison отбрасывает lookahead только при `yyerrstatus==3`, а `yyerrok` ставит 0.
  * Доп. находка — CRASH SIGSEGV: `iec_bison.yy:4037-4038` (`VAR_OUTPUT error ... END_VAR` → `$$ = NULL`) кладёт NULL в `var_declarations_list`, `absyntax_utils/add_en_eno_param_decl.cc:87` разыменовывает его (gdb bt). Любая прагма `{...}` в VAR_OUTPUT FB.
  * Доп. находка — ложный «ok»: `attr_in_body.st` на пине проходит, но текст метки `attribute 'label' := 'stmt'` дословно попадает в сгенерированный `POUS.c` (невалидный C).
  * Добавлены 2 файла с не-attribute прагмой (`pragma_plain_*`) — чтобы проверить (а) независимо от (б): пин HANG / CRASH, fix-parser-hang syntax_error / CRASH.
