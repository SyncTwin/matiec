# PROGRESS — iec2c attribute hang

Ветка: `cloud/attr-hang` (от 6c70e7e, база 3a41303).

- [шаг 0] Старт. Ветка `cloud/attr-hang` создана локально от `origin/cloud/attr-hang`. Есть bison 3.8.2, g++, autoreconf; flex отсутствует — ставлю из apt.
- [шаг 1] Сборки вне дерева: `git worktree add /tmp/m_pin 3a41303`, `git worktree add /tmp/m_fph origin/synctwin/fix-parser-hang`; в каждой `autoreconf -i && ./configure && make -j8` — rc=0 обе (flex 2.6.4 поставлен через `apt-get update && apt-get install -y flex`).
- [шаг 2] Корпус `tests/corpus_attr/` (17 файлов: 2 зачина + 13 с метками + 2 базовых без меток) и прогонщик `tests/corpus_attr/run.sh <build-dir>`.
  Пин: 4 ЗАВИСАНИЯ (все SFC: метка перед STEP/INITIAL_STEP/TRANSITION/ACTION), 1 CRASH SIGSEGV (метка в VAR_OUTPUT), 7 синт. ошибок, 5 ok.
  fix-parser-hang: зависаний нет (SFC → синт. ошибка), CRASH в VAR_OUTPUT остаётся.
