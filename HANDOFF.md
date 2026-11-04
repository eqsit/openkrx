# OpenKRX: текущая сборка после переноса OpenKRX-new — 2026-11-04

Версия OpenKRX-new стала основным деревом: содержимое перенесено из
`/home/a/openkrx-new` в `/home/a/openkrx`, отдельный клиент `dist/OpenKRX-new`,
ярлык `openkrx-new` и профиль `user-data` больше не используются (ярлык удалён,
профиль оставлен снимком в `user-data/`). Личный профиль
`~/.local/share/ddnet` не перезаписывался.

Работать в `/home/a/openkrx`; сборка — `/home/a/openkrx-build/build` (Linux) и
`/home/a/openkrx-build/build-win` (Windows cross-build). Прямые локальные правки,
без Web orchestration. Новая задача включает Basic и Legit.

Публикация: GitHub v0.1.9, исходный коммит `d964b9653a051875510b92c37596da8dce279ea2`.
Проверка переноса Codex: installed `dist/OpenKRX` SHA256
`edf9e9451c5fcc945f95f6b8a01f984a3c4eada8fa545702efeb64891a92797b`
совпадает со сборкой и, после strip, с клиентом Linux-релиза. Все 860 файлов
`src/` совпали с архивом итогового new-дерева. Обычный launcher проверен
в изолированном профиле/Xvfb: окно, настройки агентов/Pilot, выход 0;
личный конфиг не изменился. Beta binary/server и 28 зафиксированных исходных
файлов совпали с её deployment-записью, ярлыки по-прежнему ведут в beta.
Исправлены устаревшие AGENTS.md и локальный dist/BUILD.txt. Заголовок окна
в v0.1.9 пока `OpenKRX-new`; это косметический остаток, исходники и release
ради его исправления не менялись. Проверка: research/release-v019/codex-audit/.

Перенесены все пять агентов Avoid, новая Auto Edge, Perfect Silent, изменения
Dummy Fly/Auto Shotgun/Unfreeze, Blatant ESP, Fent, TAS Analyzer/Fake Aim,
Tile Editor и Pathfinding, Now Playing/Trail, новые настройки и benchmarks.
Сверка: `research/new-krx-20261103/COVERAGE.md` — 395 строк, статусы и ограничения.
Итог и проверки: `research/new-krx-20261103/RESULT.md`.

Pilot: исправлено ограничение переноса одним потоком при наличии лазеров.
Каждому работнику выделены свои collision/tuning/world; проверка и оценка
траектории объединены. В оригинале два прохода тоже есть. Наш лимит
`openkrx_pilot_budget_ms` по умолчанию 8 мс, 0 — весь исходный population.
Меньше кандидатов может снизить качество; это не жёсткий предел всей задержки
кадра и не найденное исправление оригинала. Его FPS в такой сцене не измеряли.
Случайные хуки и направления есть в оригинальном генераторе.

Blatant: актуальная запись death — 99999 и DeepFrozen. Ранняя трактовка поля
в preview1/2 была ошибочной; contact IsInFreeze обработчик не пишет.
См. `DEATH_FIELD_CORRECTION.md`. Реальные Track/Aimbot ESP перенесены.
Ручной прыжок, возврат оружия и поддержка helpers в чате/меню сохранены.

TAS: фактический клиент с dummy выявил перепутывание абсолютных слотов при
загрузке и запись неприменённого dummy input при Forward. Оба исправлены.
Повторный интеграционный прогон требует успешный Fake Aim и `Valid run`
после сохранения/загрузки. Analyzer завершился; подробности — TAS_INTEGRATION.md.

Нативные проверки проходят: Avoid 648 + 396 сравнений, Movement 39992,
Classic 184, Fent 213, rescue 31, TAS/editor 111, replay 2357101; реальные
попадания пяти оружий, физика death/tele, Rocket и возврат оружия.
Отдельный Rocket клиент/сервер: один спасённый ручной прыжок, один выстрел,
возврат к молотку, без заморозки в проверенной сцене. Настоящие меню и ESP
отрисованы в изолированных процессах; это не pixel A/B.

analysis/Ghidra Evidence по новому unpacked PE b3010f41… экспортированы, собственные
сессии закрыты. Полное совпадение packed EXE/hook, original runtime/pixel A/B,
все различия DDNet 19.5/20.3, тайминги и RNG не доказаны. Linux MPRIS,
графические и файловые адаптации явно описаны в отчётах. DDoS/flood исключён;
массовый спам и vote-kick не расширялись. Не выдавать эту сборку за доказанную
100% копию оригинального поведения.

---

# OpenKRX-new: отдельная тестовая сборка новой KRX — 2026-11-04

Рабочее дерево `/home/a/openkrx-new`, сборка `/home/a/openkrx-new-build/build`.
Текущий dist/OpenKRX-new SHA256
`023784e8e52fc0e6b0b4be353b6739c40763d843b15d050d8a15e9da43d02cde`.
Ярлык `openkrx-new` запускает scripts/openkrx-new.sh с отдельным
`XDG_DATA_HOME=/home/a/openkrx-new/user-data`. Открытый процесс не перезапускался.
Старые OpenKRX и beta не заменены; 846 исходных файлов/артефактов/ярлыков
проверены по сохранённому manifest без изменений. Личный профиль не перезаписывать.

Это промежуточный перенос, а не заявление о полном1:1. Текущая задача и границы:
research/new-krx-20261103/TASK.md. Доказательства — реальные analysis/Ghidra по новому
unpacked_krx.exe SHA b3010f41…; не приравнивать это к packedEXE/hook runtime A/B.
Новая просьба пользователя «вообще всё» включает Basic/Legit, несмотря на старое
исключение в историческом плане. DDoS/flood исключён; массовый спам не расширяется.

Перенесены Basic/Legit, Pilot, AutoEdge, Analyzer/TileEditor/multigoal Flow,
новый TAS FakeAim, NowPlaying/Trail/benchmark и подтверждённые изменения helpers.
Blatant: нулевой Anchor AutoDrag теперь отсутствует; death99999 сDeepFrozen (исправлено после уточнения поля321; IsInFreeze320 не пишется этим обработчиком).
Pilot: обнаружены реальные stalls в переносе; private collision/tuning perworker,
объединённая validate+score, budget default8ms (0 — полный исходный population).
Budget — адаптация OpenKRX, а не фикс из original. Original тоже делает separate
validate+score; originalruntime не замерялся. Random hook/aim есть в original,
поэтому отдельный empty-air hook сам по себе не доказывает ошибку переноса.

Preview2 прошёл native проверки Avoid648, movement39992, Classic184, TAS111,
replay2357101, real Aimbot hits5weapons, physics death/tele, rescue31 и bots;
Pilot scores reference-equal/source-unchanged на full/budget. Реальные6вкладок
отрисованы в owned private Xvfb/GDB child; не считать это проверкой кнопок или
pixel A/B. Первый preview-smoke показывал browser: его старые скриншоты не
доказывают меню. Достоверные текущие — preview-smoke-2/tab-*.png иpilot-menu.png.

Осталось: полный ledger старых и новых функций, deep Fent и оставшиеся
Rocket/physics/input/registry gates; проверить найденные реальные расхождения,
обновить финальные отчёты, закончить Evidence export и закрыть ownedREA.
Нельзя объявлять задачу завершённой только по успешным собственным тестам.

Ниже сохранена история исходного OpenKRX до отделения дерева.

# Локальная сборка: исправлен TAS Fake Aim — 2026-11-02

Fake Aim пересчитывает из m_pInitialWorld, а не текущего live world. Отказ
не меняет запись; пустая запись/нет стартового мира имеют отдельные сообщения.
Кнопка применяется к уже записанным/загруженным кадрам. Linux dist обновлён,
оба desktop hash323fb987…; Windows остался предыдущим. Открытый клиент надо
перезапустить. Личный config не меняли. Предыдущие chat/menu правки сохранены.
Native2357063checks/errors0; private live4modes/rewind/load exit0.

Rocket Save изучен без изменения алгоритма/настроек:10private UDP-delay runs.
В одной сцене стабильные0/60/150/300ms не сорвали сейв; с AntiPing0 при150/300
есть predicted freeze7/15frames, но authoritative freeze0. Burst+60ms при
RTT150/margin20:7late replies и5predicted freeze; margin80 в повторном
сценарии дал0/0. Это проверка конкретной сцены, не universal fix/успех-rate.
Полный отчёт и границы: research/fake-aim-ping-20261102/RESULT.md,
latency-summary.json, deployment.json. Rocket/личный margin не менялись.

# Локальная сборка поверх v0.1.8: чат и настройки — 2026-11-02

Пользователь запросил продолжение работы Avoid/Rocket Save и других helpers
при открытом чате/настройках; `krx_hidebubble` теперь default1. При1 SnapInput
явно ставит PLAYING и проходит обычный pipeline в чате и меню. При0 сохранены
CHATTING/IN_MENU и прежняя блокировка. Tooltip существующей галочки обновлён.
Сам алгоритм Avoid/Rocket не менялся. Это локальные незакоммиченные изменения;
GitHub v0.1.8 пока содержит прежнюю версию без этой правки.

analysis-agents6.8.0 установлен локально в `/home/a/.local/share/analysis-tools`, без
изменения MCP-регистраций. CLI валидировал122сохранённых Evidence и выбрал
original SnapInput `ev_5da54cf271dd388fb62412d110b1c91dcc29b3de338b2f3241129747eb3cbbb2`
для PE0477b21e…. Оригинал скрывает чат сохранением прежних флагов; обычное меню
выставляет IN_MENU и пропускает helpers. Поддержка меню и явный PLAYING вместо
stale flags — адаптация под запрос, не доказанная1:1функция оригинала.

Linux/Windows собраны отдельно из31cc03e+ровно3изменённыхsrcфайлов, обе сборки
успешны. Установлены dist/OpenKRX и dist/OpenKRX.exe, оба OpenKRX.desktop
обновлены; личный config не изменён, explicit krx_hidebubble override отсутствует.
Игровые/runtime-проверки не проводились по прямой просьбе пользователя:
он проверит самостоятельно. Backup и хеши — research/chat-menu-automation-20261102/deployment.json.

# Текущий релиз: OpenKRX v0.1.8 — 2026-11-02

Пользователь после аудита разрешил backup+исправления и затем явно запросил
push исходников и Linux/Windows GitHub Release. **v0.1.8 опубликован**:
https://github.com/eqsit/openkrx/releases/tag/v0.1.8
Исходники main и annotated tag отправлены; release commit
`efc1475e1fd96991c8a726888a4039e6f108f2b0`. GitHub API подтвердил5assets/digests/sizes,
latest=v0.1.8; public SHA256SUMS download200, bytes совпали.
Полный backup до правок:
`/home/a/backups/openkrx-before-parity-fix-20261102-143338/` (zstd проверен).

Исправлены все13групп из `research/codex/parity-audit-20261102/RESULT.md`:
IsInFreeze vs LiveFrozen и terminal flags, switch/game order, TAS forced
death/stopper, retained/pruned AutoDrag world, Super gate, Rocket normalization
и mouse radius, Avoid→aimbot→Rocket order, hooktele ray, death contact producer,
dynamic blast tuning, reload float order. Transient disable/freeze сохраняют
план; reconnect/cid/slot resets сохранены. Manual-jump rescue, confirmed weapon
restore и Fire-sync сохранены. Полная1:1 идентичность не заявляется.

Отчёт: `research/codex/parity-fix-20261102/{RESULT.md,verification.json}`.
13 native suites exit0: в том числе1381 policy checks,2357031 TAS/path/Fent,
13053 позиций старых original Fent записей без расхождений. Два private
live-прогона проверили rescue+выстрел+возврат, GDB подтвердил123цикла pipeline
и неизменность плана на3 gates. Linux client/server собраны локально,
установленный OpenKRX SHA25650ee629954fe42fe7b75c62db1c7c356382557211a5f3b65a8d62fce05059fd8.

Оба desktop-файла запускают текущий scripts/openkrx.sh. После скриншота
«x11 not available» подтверждён неработающий X11 :1 и рабочий native Wayland.
Launcher выбирает Wayland при доступном сокете и отсутствии явно заданного
SDL-драйвера. Настоящий запуск с пользовательскими graphics-настройками в
private XDG: Wayland/OpenGL/NVIDIA, exit0, личный конфиг неизменен.

Личный config не подменять пакетным/тестовым. Его внешний клиент сохранил
изменения в14:35:20 до наших private tests; эти изменения сохранены.
Пользователь требует оставить настройки пакетов как в v0.1.7: исходные defaults
(включая cl_antiping0), без копирования личного конфига. Подробнее о сборках,
упаковке и публикации: `research/release-v018/`.

Все сборки локальные, без ssh home. Windows MinGW cross-build exit0,
PE/DLL dependency closure проверен:20 PE/19DLL, missing0; нового Windows
runtime-прогона не было. Linux из финального пакета запущен через OpenKRX.sh
в native Wayland, OpenGL/NVIDIA, private profile, exit0. Конфиги каждого пакета
побайтно совпадают с v0.1.7, data/DLL/replays сохранены. Linux1049/Windows1067
файлов сверены по manifest. Git source archive точно соответствует тегу.
Артефакты: Linux7ae9b74f…0edc, Windowsd6345d72…52ae0,
source d17a52e3…ebb45, replaysacfada06…2865; полный JSON в
`research/release-v018/package-verification.json`. Старые v0.1.7 архивы не
менялись на GitHub; текущие dist platform archives теперь содержат v0.1.8.

# Предыдущее состояние рабочего пространства — 2026-11-02

Сессия теперь на машине сборки: `/home/a/openkrx`, локальные деревья
`/home/a/openkrx-build/{source,build,build-win}`. Проектный скилл и AGENTS.md
обновлены: собирать локально, старый build-remote.sh здесь не использовать.
Пользователь запретил дополнительные запуски/сравнения Windows ради различий
с Linux и использование ВМ. Текущий TAS принят пользователем; дальнейшая
полировка мелочей TAS сейчас не требуется.

Существующая диагностика `openkrx_avoid_debug` имеет default0 в коде. В личном
конфиге было1: выставлено0, очищен только путь logfile, ранее принудительно
добавленный нашим launcher. Launcher больше не создаёт client-logs/ и не
передаёт logfile без запроса. Другие настройки не менялись; резервная копия:
`/home/a/backups/openkrx-logs-build-20261102-142910/`.
Сборка v0.1.7 не менялась; desktop build hash исправлен на фактический0fd43a7a….

Завершена проверка уже снятых оригинальных Fent-записей: **13 053 позиции,
0 расхождений** (3 прогона AiP-Gores). Ошибка старой fixture: без DDNet extended
flags она выводила jetpack=true из ненулевого jetpack_strength. Исправлен
стенд, не production physics. Native fixture запущена локально, exit0.
Данные/хеши/границы: `research/fent-fidelity-20261102/ORIGINAL_AB_RESULT.md`,
`original-replay-verification.json`, `original-replay-physics-3.log`.
Прежние замеры Low на одном сервере: оригинал Wine31.55/33.64/38.26с,
OpenKRX Linux32.98/34.64/34.49с. Это ограниченное сравнение; не доказательство
идентичности всех карт/платформ. Новых запусков оригинала/Windows/ВМ не было.
На момент этой исторической записи правки аудита ещё не вносились;
текущее исправленное состояние описано выше.

# История: аудит Blatant / Rocket до разрешения исправлений — 2026-11-02

Повторная analysis-сверка выявила13групп расхождений. Критично: originalhazard
проверяетIsInFreeze318, а порт ошибочноLiveFrozen31a;Deep319. Отличаются
switch/gameorder, controllerAvoid/aimbotorder, hooktele-ray, dynamicblast
tuning и прочие детали. Полный отчёт и границы:
research/codex/parity-audit-20261102/{RESULT,COVERAGE}.md.

По прямому указанию пользователя исходники приложения этим аудитом НЕ
исправлялись;клиент/конфиг/установка неизменны. 4researchnativefixtures
наsshhome exit0;originalnativeA/Bнет. Старые утверждения о совпадении
timer/deep/live и оaimbot-before-Avoid ошибочны,новый отчёт приоритетен.

# Предыдущий релиз: OpenKRX v0.1.7, 2026-11-02

Исходник `11fe25ea16c8d4b941bf520a2412a827fa030e0c` отправлен origin/main,
annotated tag v0.1.7. Linux/Windows пакеты и архив исходников опубликованы:
https://github.com/eqsit/openkrx/releases/tag/v0.1.7

Содержимое v0.1.7 (поверх v0.1.6): конвертация legacy JSON→`.tas` вынесена в
отменяемый фоновый worker (`.tmp`+rename, результат из `OnRender`, меню TAS
больше не блокируется); мир TAS явно включает PredictTiles/PredictFreeze/
PredictWeapons/PredictDDRace/KrxPrediction; старт/финиш гонки через общий
`TouchesRaceTile` (PureMapIndex + пересечённый отрезок); fake aim из живого
мира; автор сохранённой записи из `player_name`; подписи Load/Clear и правки
раскладки/выравнивания меню.

В пакетах лежит папка `replays/` — 179 готовых TAS из оригинального KRX vault
(`krxclient/krx-replays`), плюс отдельный ассет `OpenKRX-tas-replays-v0.1.7.zip`.
Запись проигрывается на своей карте (карты в пакет не входят, клиент качает их с
сервера); те же файлы клиент скачивает сам через Replay Vault → Sync now.

Артефакты: `dist/OpenKRX` 0fd43a7ac7d9870cee9d139afe53f0ba581864938acb51e44eed2efd56594d41,
stripped `dist/OpenKRX-linux-x86_64` 2a5ee44555aa16518e6761d6422e518dbb8c5bc40c022761006a42e840bb7930,
`dist/OpenKRX.exe` 93e5598707d1a542fd12a99e0de54b4668f501bd552d05a10a5214a309f38c8e.
Архивы: Linux 00d351f8aa811023bc90a2fea0c162e396e3957323d96f8484f757a547284cb8 (1048 файлов),
Windows 1328cc7c72f086b9d0df31dc753f68c267c95870c0e6b0b0e03b473a33208d3d (1067 файлов).
Проверки, детали упаковки и ограничения — в конце JOURNAL.md
(`2026-11-02 — релиз v0.1.7`) и `research/release-v017/`.

# Предыдущая сборка: OpenKRX v0.1.6, 2026-11-02

Исходник `e60b7e4ef0cc24563b403cf04e9058dc3388892d` отправлен origin/main,
annotated tag v0.1.6. Windows/Linux пакеты опубликованы:
https://github.com/eqsit/openkrx/releases/tag/v0.1.6

Содержимое: аудит контроллера TAS — загрузка/валидация записи из текущего
источника, `replay_local`→активный dummy (`m_aReplayInputSlots`), Fire при
rewind из первого снятого снимка с синхронизацией DummyInput/HammerInput,
очистка реальных входов в neutral50, порядок playback/autoreplay, отказ
`LoadReplay` во время записи, отдельное LastReplayTimeTicks; Fent оценивает
кандидатов параллельно в изолированных мирах с отдельной коллизией каждого worker; попап
Save replay и обновлённые кнопки транспорта TAS. Матрица и ограничения
по-прежнему в TAS_PATH_FENT.md; отчёт аудита —
research/fent-fidelity-20261102/tas-controller-audit.md.

Сборки только ssh home. Linux: `scripts/build-remote.sh`. Windows: `build-win`,
target `DDNet.exe`, DISCORD=ON/DISCORD_DYNAMIC=OFF, pthread/ssp из текущего
`/usr/x86_64-w64-mingw32/bin`. 1920 tracked files совпали с home. Native
`tas_path_fent_test` 2 357 031 проверка/errors0, 179 файлов/1 010 553 кадра.
Linux-пакет: приватный Xvfb/сервер, Fent busy/отмена/завершение 88 кадров,
передача в TAS, playback, exit0. Windows-пакет: Wine с приватным префиксом,
страница TAS, exit0. Внутри архивов 868/886 файлов, SHA256SUMS сверены.

Хеши: Linux dist/OpenKRX 9572219482a7cf0bfea8f376ca12dda7810ed1cc305e7252afa761fa023eb5cb,
stripped cf980a42c9cd3d4a06c33ca89c2b961be7037869db58f503e603cb69cd1a5f6e;
Windows OpenKRX.exe 912a952f1e49282307d3a36c120fc30586a617a36693560f8a7992ebfbce1473.
Пакеты: linux tar.gz SHA256 f6f39699…049baf (51 302 370 байт), windows zip
4e7a8628…20fb05 (60 422 443 байта), SHA256SUMS a93c80a6…d11d6 (187 байт).
Загрузка local-only записи при активном dummy требует совпадения стартовой
позиции; иначе invalid run — это стартовая проверка оригинала. Полная 1:1
и pixel A/B не заявлены, Windows проверена только Wine.

# Предыдущая сборка: OpenKRX v0.1.5, 2026-11-02

TAS / Pathfinding / Fent (№34–74, 41 название) и Path ESP №27 перенесены.
Матрица реализации/ограничения/использование — TAS_PATH_FENT.md.
№39 сохраняет original flag/menu: в найденных refs нет consumer, reset
всегда prediction=true. Legit/Basic исключены. Не заявлять абсолютное1:1:
полное native original A/B и pixel A/B не проведены.

Исходник7470801c773f5db498a5c829f651948385891730 отправлен origin/main;
annotated tagv0.1.5 указывает на него. По явному запросу пользователя
опубликованы Windows/Linux и SHA256SUMS:
https://github.com/eqsit/openkrx/releases/tag/v0.1.5
Оба архива проверены через GitHub asset digest/size, внутри868Linux/886Windows
файлов. Windows19DLL, toolchain-current pthread/ssp,668imports/missing0.

C++ сборки Linux14 и Windows3 и native fixtures выполнялись только sshhome;
все1920tracked files исходного7470801 совпали с home. Linux/Windows packages
проверены: Linux private server/client/Xvfb, Windows private Wine/Xvfb (TAS menu,
exit0). Native4:2,357,031checks/errors0,179files/1,010,553frames. Extended dummy,
4fakeaim,autosave,tunnel,Vault/cache,cancel/disconnect и freeze-live5 прошли.
Последний stripped Linux package: Fent83frames,playback60,liveX362 против272,
exit0. Vault179files: max main heartbeat14.18ms/p9913.02ms.

Браузер TAS — popup с папками/.., Enter/doubleclick load. Vault/flow async,
установка Vault8files/2msbudget. AutoSync только при показе TAS page.
Show real aim default0 в оригинале: recorded взгляд только при включении.
Playback не меняет mouse/camera. Прерванный Fent не передаёт готовую запись;
это не ошибка GA. При ещё работающем Fent ранний Playback объясняет ожидание;
completed worker serviced перед playback; при успехе сообщение с числом кадров.

dist/OpenKRX SHA2560ee0d01d3fc4702969aff29b193040876370ea6c9370c4662f38674e3b8adc1e;
оба ярлыка обновлены на него. Уже открытый клиент требует перезапуска.
Личный config не переписывался тестами; пользовательские процессы не закрывали.
179готовых TAS оригинального Vault установлены локально в replays/ (gitignored,
manifestinstalled-replays.json); в goodcrack.zip готовых TAS не оказалось.
В пакетах готовые TAS доступны через Vault Sync; пользовательских записей нет.

Evidence/tests/build/package/release отчёты: research/tas-path-fent-20261101/.
RESULT.md,verification.json,package-verification.json,release.json.
REA167+12controller+18codec,18полных inline records. owned-evidence.json —
локальная подборка, не экспорт потерянного при лимите/выключении MCP ledger.
Финальная analysis binary_session open=false,cleanup_pending=false; own tests завершены.
Новый README отменён пользователем; отдельный README в репозиторий не добавлялся.
Существующая краткая инструкция внутри пакетов обновлена для v0.1.5.

Ниже сохранена история предыдущих релизов; текущая сводка выше приоритетна.

# Актуальное состояние: OpenKRX v0.1.4, 2026-11-01

Пункты 1–33 исходного FEATURE_ESTIMATES.md обработаны: от Teleport Prediction
Menu до Find Tunes включительно. Логика восстановлена через analysis,
исходники изменены напрямую. TAS и следующие пункты не входят в перенос.
Матрица поведения и ограничения опубликованы в PRE_TAS_PORT.md.

Актуальный план: REMAINING_FEATURES.md — сделанное, 41 оставшийся пункт
(№34–74: TAS 32, Pathfinding 8, Fent 1) и отдельные проверки уже перенесённого.
Пользователь 2026-11-01 исключил Avoid Legit №75 и Avoid Basic №76:
они не нужны вообще, не реализовывать и не возвращать в план как отложенные.
Path ESP №27 остаётся зависимостью Fent. Следующий блок по списку — основа TAS.

Новые компоненты: krx_misc_pre_tas, krx_movement_pre_tas,
krx_visuals_pre_tas. Конфигурация — krx_pre_tas_*_config.h. Данные генератора
имён — krx_name_data.h; исходные фразы — data/krx/killsays.json, включены в
CMake data manifest. Меню Misc, Visuals & HUD и Settings восстановлено по
оригинальным обработчикам, включая условия полей, размеры и режимы.

Сохранять особенности оригинала: дополнительный визуальный тик Fast Input
с откатом мира, порядок Dummy First, статические Ghost Move / Find Tunes,
MSVC RNG для слов и shuffle, mutex уведомлений и восстановление kill messages,
исходные ограничения в 64 игрока у новых обработчиков.

Linux и Windows собираются только на ssh home. Linux: scripts/build-remote.sh
с DISCORD=ON/DISCORD_DYNAMIC=ON; рядом с бинарником нужен discord_game_sdk.so.
Windows: build-win, target DDNet.exe, DISCORD=ON/DISCORD_DYNAMIC=OFF.
При упаковке Windows брать libwinpthread-1.dll и libssp-0.dll из текущего
/usr/x86_64-w64-mingw32/bin на home: старый комплектный pthread не экспортирует
clock_gettime64/nanosleep64, которые нужны новому компилятору.

Исходник релиза: 9d0aaa10f4bf1c271fce8dc579840f1194596939. Исходники
отправлены в origin/main. Оба локальных ярлыка запускают scripts/openkrx.sh;
итоговый dist/OpenKRX SHA256:
737d87b164dc8a9621254881c506434372094b944f471f8d845fc7751f2b8586.
Открытый клиент требует перезапуска. Пользовательский конфиг не переписывался.

Проверки: 34 native prediction, 20018 misc, movement, checksum packet dispatch
и три независимых hashlib; GDB — 602 наблюдения/шесть main/dummy/FastInput/
Others режимов без ошибок тика и снимков. Приватный FastInput+visuals+Rocket:
один сейв, один подтверждённый выстрел, возврат молота, без фриза, exit0.
Повторено на stripped Linux-релизе. Windows запущен под Wine с отдельным
профилем, рендер работает, exit0; проверены 668 импортов комплектных DLL.
RPC проверен через отдельную fixture SDK. Все 1724 source hashes сверены с home.

Не заявлять абсолютную 1:1 идентичность: native original A/B и pixel A/B
не получены. TrackPoint/Aimbot ESP 25/26 в исследованных прямых ссылках не
имеют renderer; Path ESP 27 принадлежит Fent вне объёма. SendAds 8 в оригинале
сбрасывает флаг; benchmark 18 — checksum metadata. Дополнительный Blatant
path renderer не придумывался. Manual jump, weapon restore и Fire-sync сохранены.

Локальные результаты: research/pre-tas-20261101/RESULT.md, PORT_STATUS.md,
verification.json, package-verification.json и release.json. Evidence по
веткам в misc/, movement/, visuals/, prediction/. Worker analysis-сессии закрыты,
root individual Evidence сохранены; новый MCP-сеанс в финале уже не имел
старого target/ledger. Тестовые процессы завершены; пользовательские VM и
открытый клиент не закрывались. Бэкап до работы:
/home/a/backups/openkrx-before-pre-tas-20261101-155937.

Пользователь явно запросил commit/push и публикацию Linux/Windows в Releases.
Релиз v0.1.4 опубликован: https://github.com/eqsit/openkrx/releases/tag/v0.1.4
Windows ZIP, Linux tar.gz и SHA256SUMS доступны; размеры и GitHub digests
совпали с локальными архивами. Тег указывает на исходник сборок 9d0aaa1.

Ниже история; при противоречии актуальна верхняя сводка.

# Актуальная сборка: аудит Blatant/Rocket/timing, 2026-11-01

Итог: `research/codex/full-parity-20261101/RESULT.md`. Таблицы функций и analysis
Evidence находятся в `avoid/`, `rocket/`, `timing/`. Проход и установка завершены.

Главная находка: в оригинале cl_prediction_margin default/min20/max300;
порт разрешал пользовательские8. Вернули20/20/300, загрузка8->20 и effective20
проверены в игре. Два промаха при10.5ms поиске воспроизводятся задержкой хука
на1тик, но отрицательного ack именно старого пакета нет. Новые input-send /
input-timing логи сохраняют все ответы и uncached время. Порядок SendInput ->
OnPredict совпал с оригиналом; дополнительный тик в прогноз не добавлен.

Исправлены source fallback, zero sentinels/anchors, придуманные Direction/Hook
фильтры; эквивалентная Assist-оптимизация проверена. В speculative KRX worlds
восстановлены death/tele policy, projectile disposal, weapon tele, telegun,
источник силы взрыва и длины пробного хука. krx_predictionteleports default0.

Сохранены ручной прыжок и возврат оружия. Fire-sync против NSIF replay старого
нажатия — явная поправка совместимости. EmergencyHook/KeepDirection/readiness
не возвращены. Перенесённые правила физики не включать в обычный prediction.

12 native suites ONLY sshhome passed: 648 boundaries, 108 controlled + 288 real
full/elided comparisons, 48 tile cases, projectile/Rocket/Fire/restore/trace.
Final live: 1 rescue/1 real shot/return hammer/no freeze/exit0, margin8->20,
91 timing replies без late. Это bounded проверка, не native original A/B.

dist/OpenKRX SHA256 `1e5c5f1284125defe2788a6dd9f2960b86fa137b1f5ad1821af8a449ca8faca9`.
34 source hashes и home binary совпали; BUILD/SOURCE-SHA256 актуальны.
Оба ярлыка обновлены и проверены. Нужен перезапуск через прежний ярлык.
Личный конфиг не переписан, krx-beta.desktop не тронут. Исходники этого прохода
подготовлены к commit/push в main по явному запросу пользователя; релиз не создаётся.
JOURNAL и openkrx-project skill обновлены; quick_validate passed.

Не объявлять идеальной1:1: native original A/B отсутствует, optional FastInput1,
TAS/other agents не портированы; полная cross-version physics не доказана.
Ниже исторические сводки, они не описывают текущую сборку.

# Текущее: исправлен staleFire, начат полный аудит (2026-11-01)

Новый запрос: полностью сверить Blatant/Avoid, prediction и Rocket/timing
с GoodCrack через analysis. Два дополнения — manualjump иweaponrestore — оставлены.
Сейчас dist/OpenKRX SHA256 5ed21d651e583ccc81b31fdb468dc77d123591478154356d4b09c41b4f3b3f95, сборкаONLYsshhome,
7native suites/8firecases/isolatedlive passed,31sourcehashes matched.
Новаяпоправка SynchronizeFireCounter передDecide предотвращаетNSIF replay
старогоFireпослеrelease; OnPlayerDeath сохраняетpendingrelease,отменяетrestore.
Этонамеренноеотличиеотoriginalfullinputcopy,невыдаватьза1:1.
Бэкап /home/a/backups/openkrx-before-fire-sync-20261101-143043.
Отчётresearch/codex/fire-sync-20261101/RESULT.md. Hookdelay1ещёнеисправлен:
дваслучая14:22/14:31reconstructedточно, причиналожногоtimingнеустановлена.
Текущийfullauditподзадачи/отчётыresearch/codex/full-parity-20261101/.
Нуженrestartчерезпрежнийярлыкдляновойсборки,конфигнетронут. Далееистория.

# Актуальное состояние: политика KRX и два дополнения (2026-11-01)

Пользователь попросил поведение Avoid/Rocket как в GoodCrack, затем явно
оставил возврат оружия и сохранение ручного прыжка. Оба дополнения сохранены.
Убраны runtime EmergencyHook, KeepDirection и pending-shot readiness/forecast.
Основной aimbot теперь перед Avoid; Rocket после Avoid до Send. Старые три
конфиг-переключателя удалены, их сохранённые1 больше не влияют.
Бэкап до изменений: /home/a/backups/openkrx-before-krx-parity-20261101-141313.
Отчёт: research/codex/krx-parity-20261101/RESULT.md; новые analysis ba150/b6760
Evidence сохранены, сессия закрыта. Native original runtime A/B всё ещё нет,
полную1:1копию не объявлять; два дополнения намеренно отличаются.
Сборка только sshhome, шесть native suites и isolated live passed;
1manualrescue/1shot/returnhammer/nofreeze/exit0; 31source hashes совпали.
dist/OpenKRX SHA256 4ebef395a7612dd933f13d71f8c02a220b4c67dc01e347a96ab42c5c57e87101.
Для пользователя — перезапуск прежним ярлыком. Конфиг не переписан,
коммит/релиз не сделаны. Ниже предыдущие сводки для контекста.

# Диагностика фриза 14:01:18 (2026-11-01)

См. research/codex/failure-analysis-20261101-1401/ANALYSIS.md.
EmergencyHook на13012948 снял рабочий зацеп по ConstantLife27 SAFE;
remote native probe: без хука freeze32, с хуком76 (горизонт80).
Далее промах повторного броска, NSIF26..1, no_safe_no_tail, freeze13012988.
Короткий NSIF/null подтверждён новым REA2d2080 как поведение оригинала;
фильтр отпускания — отдельная доработка. Полный causal originalAB отсутствует.
Это диагностика, production/config/dist не меняли. Текущие незакоммиченные
Rocket/controls правки относятся к другой работе и сохранены.

# OpenKRX — актуальное состояние на 2026-11-01, 14:03 МСК

Эта сводка описывает текущее дерево и установленный Linux-клиент. Старые
записи JOURNAL.md о релизах v0.1.0–v0.1.2 и удалении возврата оружия —
история; новый запрос пользователя снова разрешает эту функцию.
Предыдущий HANDOFF сохранён в
`research/rocket-restore-20261101/HANDOFF-before.md`.

## Рабочие пути и сборка

- Основной проект/реверс/результаты: `/home/a/openkrx` на текущем ПК.
- Компиляция C++ и сборочные тесты **только через `ssh home`**.
  `scripts/build-remote.sh` синхронизирует исходники в
  `home:/home/a/openkrx-build/source`, собирает в `.../build` и возвращает
  артефакты в локальный `dist/`. Hostname обоих ПК бывает `arch`.
- Текущий launcher: `scripts/openkrx.sh` → `dist/OpenKRX`.
  Логи запусков: `research/runtime/client-logs/`.
- Оба актуальных ярлыка: `~/.local/share/applications/openkrx.desktop` и
  `~/Рабочий стол/openkrx.desktop`. Обновлены, desktop-file-validate passed.
  `krx-beta.desktop` не меняли. Открытый клиент требует перезапуска.
- Личная конфигурация: `~/.local/share/ddnet/settings_ddnet.cfg`.
  Тесты используют отдельное хранилище; личный конфиг не переписывали.
- Проектный скилл: `/home/a/.codex/skills/openkrx-project/SKILL.md`.

Установленная сборка: `dist/BUILD.txt`, built_at_utc
`2026-11-01T11:03:35Z`, build_host=home, SHA256 OpenKRX
`3410b0bd82ae3d513ddef14128b0e9ea7d39ce06a2210fbb24ceab2596d7d569`.
Source_commit `006b384` — база, текущие правки пока не закоммичены.
31 файл `dist/SOURCE-SHA256.txt` совпал с home; хеш установленного бинарника
совпал с home build/DDNet (verification.json в папке исследования).
Linux/Windows release-файлы с другими именами в dist и старые GitHub-релизы
в этой задаче не пересобирали и не публиковали; их нельзя считать этой сборкой.

## Что сейчас реализовано

- Avoid Freeze/Blatant и Rocket Save; пользовательские дополнения:
  keep manual jump, only necessary automatic hook, keep direction when safe,
  согласованный прогноз pending rocket press и готовности выстрела.
- Простые функции: Moonwalk, Auto Fire, Auto Rehook, Hook Spam, Pixel Walk,
  Quick Stop/Ground Only, Hide Chat Bubble, AutoTeam/lock, AntiAFK,
  Kill on Freeze, Emote Spam, Random Timeout Seed.
- Средний пакет уже в коде: Aimbot, Auto Unfreeze/Shotgun/Aled, Dummy Fly,
  Hook Nearest, Balance, Auto Jump Save, Fake Aim, Hook/Jet Ride, ID Stealer;
  HUD/Watermark/Aim lines/Trail, цвета тии/хука/оружия/замороженных игроков,
  Rainbow, SmoothCam/SuperDynCam, Custom Menu.
- Автоматические хуки KRX/Avoid сохраняют своё направление: аимбот не
  переписывает их aim. AutoRehook сам по себе не является автонаведением.
- В меню шесть вкладок: Aimbot / Misc / Visuals & HUD / Avoid / TAS / Settings.
  Настройки пользователя сверх оригинала — KRX → Settings → OpenKRX.

Это перечень имеющегося кода, а не обещание полной 1:1 копии оригинала.
Исходники меню: `src/game/client/components/menus_settings_krx.cpp`.
Строки `Later(...)` остаются заглушками: в частности TAS, Trajectory,
Frozen HUD, Auto Edge, Discord RPC, Version Spoofer, ряд чат-функций,
Teleport prediction/Advanced settings. Сверять конкретную функцию с кодом,
не переносить устаревшие выводы из ранних секций журнала.

## Новый возврат оружия после Rocket Save

`openkrx_rocket_restore_weapon=1` по умолчанию; checkbox
`Restore weapon after Rocket Save` в KRX → Settings → OpenKRX.

Запоминается прежнее оружие при автоматическом изменении WantedWeapon на
гранату. Во время подготовки возврат не перебивает нужный для сейва выбор.
`Decision.m_Fired` означает запрос, а не реальный выстрел: завершение
распознаётся по новому authoritative AttackTick с активной гранатой.
Затем прежний слот запрашивается до подтверждённого переключения; реальный
reload/DoWeaponSwitch отрабатывает игровую задержку без фиксированных 4 тиков.

Исправлен новый баг: если граната досталась, но выстрел не состоялся,
подготовка больше не висит навсегда. После окончания собственного fire pulse
проверяется реальная траектория без нового выстрела на 22 шага. Если она
безопасна, возвращается прежнее оружие и состояние освобождается. При
проверке учитывается ещё удерживаемый подавленный ручной прыжок, чтобы не
сорвать взведение. Это ограниченный горизонт предиктора, не гарантия всей
будущей траектории. Клавиша оружия/колесо отменяют возврат; явный ручной
выбор гранаты сохраняется. Disconnect/death/dummy change/off/missing weapon
сбрасывают сохранённую цель.

Проверки текущей версии:
- C++ на home: реальные grenade projectiles и reload для hammer/gun/shotgun/
  laser → grenade → прежний слот, ожидание 26 шагов; неподтверждённый shot,
  pending release, новая попытка, ninja, безопасная отмена без выстрела.
- C++ pending Rocket/Avoid, ready guard 4320/4323, manual ground/air/gun→grenade
  regressions прошли. В новой fixture исправлен тестовый setup: FreezeDelay=3
  и доступные прыжки; нулевой freeze-delay не проверял опасный сценарий.
- Изолированный сервер/Xvfb, реальный SnapInput: обычный сейв = 1 выстрел,
  возврат молота; отмена до выстрела = 0 выстрелов, возврат молота; следующая
  попытка в той же сессии = 1 выстрел и возврат. Ручной выбор гранаты без
  сейва сохраняется. Freeze/лишних направлений нет, exit0.
- На первой сборке также проверены отключённый возврат и ручной выбор гранаты
  во время кулдауна. Все fixture-процессы этих запусков очищены.

Код: `src/game/client/avoid/rocket_save.{h,cpp}`, weapon callbacks в controls,
config_variables.h и меню. Доказательства/логи/фикстуры/analysis:
`research/rocket-restore-20261101/`; live-данные:
`research/codex/runtime/confirmed-restore-*`.
Бэкап до работы: `/home/a/backups/openkrx-before-confirmed-restore-20261101`.

## Эталон и оставшиеся неизвестные

GoodCrack/KRX — источник переносимого поведения. analysis обязателен для
анализа бинарника; используемые Evidence не подменять shell-догадками.
Эталон `research/analysis/krx-source-analysis.exe`, SHA256
`0477b21ed12ce04a3a657a26be2f75cfcfc9b7429d6ad07ff8ecd3359616dc3d`.
Свежий analysis/Ghidra handler `0x1402ba150` сохранён в
`research/rocket-restore-20261101/analysis-0.json`: выбор WantedWeapon=4,
порог/поиск, fire press и modulo release 1..5. Новый возврат — явное
дополнение пользователя, не заявлять его функцией оригинального KRX.
Своя analysis-сессия закрыта, checkpoint.snap сохранён.

Полного native live A/B GoodCrack нет. Telegun/weapon-teleport и часть
предсказания/сложных Later-функций не завершены; ранние исследования
`research/codex/`, `research/deep-audit-20261031/`,
`research/medium-port-20261031/` сохраняются. VM-состояние и прежние
неизвестные/cleanup описаны в `research/runtime/FINDINGS.md`; в этой задаче
VM не запускали и старую уборку не объявляли выполненной.
