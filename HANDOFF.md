# Актуальная сборка: аудит Blatant/Rocket/timing, 2026-11-01

Итог: `research/codex/full-parity-20261101/RESULT.md`. Таблицы функций и REA
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
с GoodCrack через REA. Два дополнения — manualjump иweaponrestore — оставлены.
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
Отчёт: research/codex/krx-parity-20261101/RESULT.md; новые REA ba150/b6760
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
config_variables.h и меню. Доказательства/логи/фикстуры/REA:
`research/rocket-restore-20261101/`; live-данные:
`research/codex/runtime/confirmed-restore-*`.
Бэкап до работы: `/home/a/backups/openkrx-before-confirmed-restore-20261101`.

## Эталон и оставшиеся неизвестные

GoodCrack/KRX — источник переносимого поведения. REA-agents обязателен для
анализа бинарника; используемые Evidence не подменять shell-догадками.
Эталон `research/rea/krx-source-analysis.exe`, SHA256
`0477b21ed12ce04a3a657a26be2f75cfcfc9b7429d6ad07ff8ecd3359616dc3d`.
Свежий REA/Ghidra handler `0x1402ba150` сохранён в
`research/rocket-restore-20261101/rea-0.json`: выбор WantedWeapon=4,
порог/поиск, fire press и modulo release 1..5. Новый возврат — явное
дополнение пользователя, не заявлять его функцией оригинального KRX.
Своя REA-сессия закрыта, checkpoint.snap сохранён.

Полного native live A/B GoodCrack нет. Telegun/weapon-teleport и часть
предсказания/сложных Later-функций не завершены; ранние исследования
`research/codex/`, `research/deep-audit-20261031/`,
`research/medium-port-20261031/` сохраняются. VM-состояние и прежние
неизвестные/cleanup описаны в `research/runtime/FINDINGS.md`; в этой задаче
VM не запускали и старую уборку не объявляли выполненной.
