# Последнее обновление — разрешённая правка связи Avoid/Rocket (2026-10-29)

НОВОЕ РЕШЕНИЕ ПОЛЬЗОВАТЕЛЯ: на вопрос о точечном исправлении прогноза
ожидающего выстрела он ответил «Исправить эту несогласованность».
Это разрешённое исключение к строгому KRX, а не новая общая политика Rocket.

Свежий реальный отчёт18:34:15 уже содержит pipeline_us новой сборки:
полный pipeline максимум7245us при configured margin8ms. Authoritative
upper-freeze на4706; уже нет оснований переносить прежнюю гипотезу14ms
на этот конкретный эпизод. Новый отчёт сохранён в
research/codex/failure-analysis-20261029-1834/.

Точный native replay bounded scene: на4703 выбранный Hook0 с heldFire243
получаетGreedySAFE благодаря выстрелу послеreload1. При известном отпускании
244 на4704 тот же план замораживается через2 шага, AttackTick остаётся4679.
На4701 важный нюанс: старый выбранный Hook1 тоже имеет safe-план без ракеты,
но ложное held-fire предположение ухудшает ИСХОДНЫЙ прогноз6/8 и заставляет
включитьHook1. Без ожидания выстрела исходный GreedySAFE и Hook0 сохраняется.

Реализовано `openkrx_avoid_rocket_pulse_prediction=1` по умолчанию:
Rocket помнит только выданный им press/cid/slot; Avoid включает ограничение
лишь для этого ещё ожидающего odd-counter press. В simulation он не обещает
будущее удержание Fire/aim через reload: подавляет новые будущие выстрелы,
сохраняя все уже существующие projectiles. Один немедленный выстрел можно
учесть только при готовой гранате в auth И pred и отсутствии текущего
modulo-release. Остальные future shots считаются негарантированными.

Это консервативный прогноз ожидающей команды, НЕ точный симулятор будущих
сетевых packet timings/physical mouse changes. Input/fire реально не
отменяется этой функцией, обычный Rocket Search/углы/threshold/release не
менялись. Ручной новый fire с другим counter не помечается как ожидающий
нашего Rocket. `0` возвращает прежний прогноз KRX. UI не расширяли.
Clone переносит policy и его шаг, CopyFrom сбрасывает; вложенные Constant/
Greedy/NSIF пользуются той же моделью. Для подавления берётся актуальный
counter мира, а не stale candidate counter: разные even counters сами могут
создавать presses при wrap. Логи добавили pending_automatic_fire,
allow_immediate_auto_shot и settings.rocket_pulse_prediction.

Проверки ONLY sshhome для C++:
- native avoid_pending_rocket_test: original/immediate-release/known-release/
  production-policy modes, точные сцены4701/4703, counter/Clone/CopyFrom/
  ready-immediate shot; errors0. Ранний исправленный22-input план при
  известном реальном release проходит безfreeze. Это не полный sessionreplay.
- original planner и400tracing tests OK; manual ground/double jump/gun->grenade
  fixture OK. Rocketmanual algorithm не меняли.
- isolated live manual press:1rescued, nofreeze, no unwanted direction,
  press recognised by Avoid pending policy; exit0. Wall/menu/trace checks OK.

Бэкап: /home/a/backups/openkrx-before-pending-rocket-20261029-184020.
Новая сборкаdist SHA256 9da468fa5fe3ae3fc835283beb148a7d88b243cca6067e555038641e153677e4; сборка толькоhome.
Запущенный клиент надо перезапустить с обычного ярлыка.
Нельзя обещать отсутствие всех других промахов/полное KRX runtime parity.

# Последнее обновление — два реальных freeze, задержка хука и ускорение поиска (2026-10-29)

Разобраны реальные отчёты16:22:49 и16:24:09, копии и выводы сохранены в
research/codex/failure-analysis-20261029-1624/ANALYSIS.md. REA: повторно
использованы существующие exports, не заявлять новый live A/B KRX.
Основные проверенные ветки Blatant соответствуют оригиналу; полного
поведенческого совпадения всех функций пока не доказано.

Контролируемая реконструкция хука по реальным map/core/input/tuning/reload
показала чувствительность обоих эпизодов к одному тику применения ввода:
854 immediate1599,495 -> delayed1599,509 (authoritative anchor);
4865 immediate9339,2171 -> delayed9321,2175, life9 (совпадает со следующим
реальным состоянием4867). Это не доказательство конкретной сетевой причины.
В реальных данных planner до14.228ms, saved prediction margin8ms; личную
настройку не меняли. Старые логи не содержат полного времени pipeline.

Rocket: точно воспроизведён baseline6/best14/aim(-39,398) кадра4873.
Постоянное Fire в probe стреляет после reload6 и даётlife14; каждый
реальный original-style modulo1..5 pulse отпускается раньше и не стреляет,
life7. Эта несовместимость probe/pulse есть и в сохранённой логике KRX.
Обычный Rocket policy НЕ меняли: пользователь ранее выбрал строгий KRX.
Первый эпизод отличается: предыдущая обычная граната ещё перезаряжалась,
нового Rocket-issued press в финальном окне нет.

Исправлена лишняя работа порта: AimCandidates может завершиться, когда ВСЕ
включённые selectors ужеSAFE9999 и строгие сравнения больше не могут
изменить выход. Порядок и выбираемые inputs сохранены. Unknown adapters
по умолчанию не разрешают elision. Реальный adapter разрешает только
isolated/nullPRNG мир; проверены отсутствие внешних mutation/random-effects.
Это serial optimization; прежний conservative parallel guard не меняли.

Дополнены диагностики: full Avoid+Rocket pipeline_us, requested_input_tick,
prediction time/configured margin, attack_tick/reload/num_inputs/queued weapon,
previous/latest/current inputs; DiagnosticSavedInput теперь действительно
возвращает m_SavedInput (раньше в этом поле записывалиm_Input). Rocket debug
явно содержит issued_press/reload/attack_tick; старый fired сохранён для
совместимости и по-прежнему означает выдачу нажатия, а не факт выстрела.

Все C++ сборки/проверки толькоsshhome:
- 108 controlled +288 real-map comparisons полного и сокращённого поиска:
  output40bytes, full stored plans, candidate records совпадают; source/grid
  не мутируют. 94real cases используют сокращение, aggregate time -45%.
- На ранних сценах обоих реальных промахов:854 10507->4627us,
  4865 7529->2964us, inputs/plans совпадают. Это измерение на build host,
  не гарантия такого же времени на ноутбуке или устранения промаха.
- Native original planner+flight recorder400 comparisons OK; Rocket reload
  reproduction после сборки остаётся тем же. Hook reconstruction assertions0.
- Isolated live trace:3reports/766frames/192candidateframes, новые поля,
  menu retention/no-send/one controlled freeze/exit0 OK.
- source manifest28 совпал с home; gitdiff--check OK. GUI не меняли.

Сборка вdist, SHA256 9c06b89b1af2bf33ebddcc104e247ee6e38babcd9ea8e6258b4576f5261dfddd.
Бэкап до изменений: /home/a/backups/openkrx-before-latency-audit-20261029-182356.
Ярлык тот же; старый запущенный процесс сам не обновится.
Открытые вопросы: полное live KRX A/B, source-time corrections/input delay,
18.9.1vs20.3 physics/tune-zone/telegun, AutoDrag DragAim и непроверенные
consumers настроек. Не считать их доказанной причиной этих падений.

# Последнее обновление — подробный flight recorder Avoid (2026-10-29)

По последнему запросу пользователя сначала усилили ЛОГИ. Поведение спасения
не меняли. `openkrx_avoid_debug 1` пишет каждый SnapInput в ограниченный буфер:
до 6 секунд / 384 кадров; при серверной/authoritative заморозке сразу сохраняет
JSONL и ещё до 1 секунды / 128 кадров после. Frozen prediction сам по себе
не запускает отчёт. Повторные тики одной заморозки не плодят файлы.

Путь: `~/.local/share/ddnet/dumps/openkrx-avoid/`; абсолютное имя печатается
в launcher log. `openkrx_avoid_dump` вручную сохраняет последние кадры.
Настройка уже была сохранена у пользователя как1; личный конфиг не меняли.
История сохраняется при меню/чате и переключении Avoid; disconnect/slot/cid
смена сбрасывает контекст. Запись выключается через openkrx_avoid_debug0.

Три стадии input + planner proposal; reasons/gates, все результаты кандидатов
с явным cap128+selected, NSIF horizon/tail, stored plan prefix64; physics tuning,
auth/pred hook/jump/freeze/ammo/reload/input, nearby tees/projectiles, 7x7 tiles,
ticks/latency/settings. Подробности и ограничения: research/codex/AVOID-TRACE.md.
`life9999` по NSIF относится только к длине хвоста; latency_snapshot_ms не
доказывает моментальный ping, emitted input не доказывает доставку UDP.

Сборка и C++ проверки только sshhome. Native: прежние boundary tests OK,
400 сравнений tracing on/off без изменения output, cap/age/freeze/manual/reset
и независимый JSON parse OK. Live private server: 3 отчёта,
767 записей, 192 кадров с кандидатами,
одна контрольная freeze (Avoid намеренно выключен), меню/история/no-send OK,
exit0. Это доказывает логирование, а не исправление последних промахов.

Новая сборка dist, client SHA256: df3d485ed17205589986c44a4f3c4b8fb3960b362cad09a7df9e1913a33a00db.
28 файлов source manifest совпали с build host; ярлыки по-прежнему указывают
на scripts/openkrx.sh и этот dist. Уже запущенный клиент надо перезапустить.
Исходники до этой правки и последний тогдашний real log сохранены в
research/codex/avoid-trace-backup-20261029-154809/.

# Последнее обновление — убрать снос влево при прыжках (2026-10-29)

Реальныйлог14:44 подтвердил before_dir0 -> dir-1 при отмене Jump, Rocketfired0.
Причина — таблицы кандидатов исключают текущую Direction+Hook пару, но при
Jump1 такая пара с Jump0 была бы другим действием. Базовую таблицу не меняли.
Пользовательская корректировка `openkrx_avoid_keep_direction=1` сохраняет
запрошенное направление, если ИДЕНТИЧНЫЙ остальной input безопасен по всему
CheckTicks+1. Необходимое уклонение/торможение сохраняется при unsafe.
0отключает корректировку. UI: KRX -> Misc -> Keep direction when safe.
Это намеренное дополнение по жалобе, не восстановленная часть оригинала KRX.

Сборка/тесты толькоhome, всё прошло. Реальный Space+Rocket: 1rescuedpress,
0лишнихбоковыхкоманд/0freeze/exit0. Реальные A/D freeze-стены:639/128 безfreeze.
Nativeрегрессия показывает исходный-1 и исправленный0, сохраняет необходимый-1
и brake. UI toggle проверен. Бэкап:
`/home/a/backups/openkrx-before-direction-fix-20261029-145137`.
Текущий client SHA: 537564369f8599cf17ecee162c2dcf834009f39687f821529c94076ef46ea5cf. Подробности — конец JOURNAL.md.

# Последнее обновление — ручной прыжок, 2026-10-29

Новый явный запрос пользователя: Rocket Save не должен отменять его РУЧНОЙ
прыжок/даблджамп, если ракета спасает. Автоматические прыжки не нужны.
Реализовано и проверено. `openkrx_rocket_preserve_jump=1` по умолчанию,
`KRX -> Misc -> Keep manual jump with Rocket Save`;0отключает эту доработку.
Только новый физический press, одно восстановление на press, release/reset
очищают запрос. Кандидат Jump1+grenade проверяется физикой; учтены подготовка
оружия и уже летящая собственная граната. Исчерпанные air jumps не восстанавливаются.
Это явно разрешённое исключение к прежнему требованию строгого Rocket KRX;
обычный Search вне ручной ветки остаётся прежним. Общий no-shot safety filter
для всех ракет пользователь НЕ разрешал, его по-прежнему нет.

Тесты на home: `research/codex/manual-jump-tests.log` OK, реальные ground/air и
weapon preparation. Live localhost+Space: `runtime/manual-jump-final/result.json`
keptpress1/frozen0/exit0; опция0: `runtime/manual-jump-disabled/result.json`
keptpress0/frozen0/exit0. UI и strict Rocket regressions такжеOK.
Новая сборка уже в dist, старый запущенный процесс требует обычного перезапуска.
Текущий SHA: d80c42ab2ac5fbecf29853749ab56047f5d5548d79709abf25d0b53a63886d9a. Бэкап до правок:
`/home/a/backups/openkrx-before-manual-jump-20261029-133043`.
Подробности и сохранённый свежий реальный промах — в конце JOURNAL.md.

# OpenKRX — актуальный checkpoint, 2026-10-29

Каноническая папка `/home/a/openkrx`; обе записи OpenKRX (рабочий стол и
`~/.local/share/applications`) запускают `scripts/openkrx.sh` отсюда.
Компиляция и все C++ тесты **только `ssh home`**, `scripts/build-remote.sh`.
На ноутбуке — изменения, REA и запуск готовых бинарников. Не коммитить/не чистить
untracked research. Не использовать ChatGPT Web/ZIP orchestration.

## Актуальный запрос и предпочтения пользователя

Пользователь требует максимально точную копию Avoid/Blatant и Rocket Save KRX
(goodcrack), сообщает реальные промахи с верхним/нижним freeze и в проходе высотой
один тайл с freeze сверху/снизу. Подозревает взаимодействие ракеты с хуком.

**Последнее явное решение:** Rocket Save оставить СТРОГО как KRX. Пользователь
отказался от предлагаемого дополнительного фильтра «стрелять только если лучше
варианта без выстрела». Такого фильтра в коде нет, не включать его молча.
Ранее пользователь отдельно попросил менее навязчивый автоматический хук:
`openkrx_avoid_emergency_hook=1` — отдельная намеренная доработка. Значение0
возвращает восстановленный путь без этого фильтра. Ручной Hook не фильтруется.

Меню: общий пункт **KRX**, первым внутренним разделом **Avoid**, затем **Misc**.
Rocket Save находится в Misc (`Auto grenade save`), убран из General.

## Установленная проверенная сборка

`dist/OpenKRX`, `dist/DDNet-Server`, время/хеши в `dist/BUILD.txt`.
25 исходных/сборочных файлов из `dist/SOURCE-SHA256.txt` сверены с удалённой
сборкой, все OK. Текущий хеш клиента: b8747c142d64b5a5a5fd1b21a4601ea91a57fe825317293520932ec6b7cfffd0.
Сводка всех проверок: `research/codex/RELEASE-2026-10-29.json`.

Важно: бинарник до этой сессии был собран29окт05:02UTC. Логи09:34/11:29MSK
относятся именно к старому бинарнику, БЕЗ исправлений углублённого аудита.
Новая сборка получена29окт после12:49MSK; повторный запуск ярлыка нужен, чтобы
уже запущенный процесс перешёл на новые байты. Не убивать игровой клиент пользователя.

## Подтверждённые исправления

1. Ранее исправлена точка ввода: KRX custom SnapInput23e670, поставленный через
   controls vtable4a9278+90, вызывает Avoid ПОСЛЕ подготовки direction/aim и ДО
   send/last-input checks. Stock16b420 был ложным следом DeepSeek. Его вывод
   «KRX не управляет направлением» опровергнут. `openkrx_avoid_apply` legacy,
   оба значения теперь один правильный путь.
2. m_IsLocal (`character+98`) подтверждён свежим REA Read2195c0. Оригинал сохраняет
   обе локальные тии во всех фильтрах cd0c0 и в danger-world Rocketba150.
   Порт ошибочно удалял dummy. Теперь локальные сохраняются. Регрессия Rocket
  108состояний: до исправления0различий, после95, во всех95 жизнь с опорой лучше.
3. Frozen gate b4450 читает **authoritative m_GameWorld**, а поиск — predicted
   m_PredictedWorld. Прежний root gate по predicted был ошибкой. Теперь единый
   IsKrxCharacterFrozen для Avoid/Rocket с точными FNG/name conventions:
   FNG ищется в GameType или worldflag; DDNet — в server NAME; соответственно
   timer/deep/live либо timer/snapshot-weaponNinja. Не заменять общим IsDDRace.
4. Независимые Greedy кандидаты оцениваются через existing engine job pool,
   все результаты ждутся и выбираются по исходному индексу, как оригинал.
   Queued job можно атомарно забрать на main thread, чтобы HTTP очередь не
   блокировала ввод. Late worker после уничтожения snapshot не трогает его.
   Существующий laser либо inventory/pickup способный его породить -> sequential,
   поскольку CLaser временно меняет shared collision grid. Byte-for-byte тест
   последовательного/параллельного поиска и позднего выполнения прошёл.
5. Isolated copy теперь сохраняет m_DestroyTick/m_MarkedForDestroy, как обычный
   CopyWorld. Старое обнуление могло воскрешать удаляемые сущности. Изоляцию
   обеспечивают detached parent/child links; регрессия удаления/изоляции прошла.
6. Aim assist/auto действительно Hook1 (assembly115c/1172/11a4); центр от mouse,
   similarity от input aim. NSIF требует непустой CURRENT partial. ActionGroups
   5actions+3sentinels верны. Guard возвращает текущий ввод при safe continuation.
7. Player Prediction удаляет других тии, не выбирает время snapshot. Unfreeze
   читает raw game tile (GetCollisionAt отфильтровывает11). HookPoint квантует aim,
   трассирует hook_length-28, учитывает live player blocker и только SOLID.
8. Rocket Save перенесён по ba150/b6760: danger clone stripped +currentInput1tick,
   затем ShotInputFire+1 10+1steps, trigger<7; full snapshot shots21+1,
  65float-accumulated directions, bestlife>8, Wanted4 даже при отсутствии выстрела.
   MSVC bounded sort перенесён, GCC std::sort иначе выбирал ties. Release modulo
   uniform1..5, game-world tick divisor, перед enable/gates. RNG distribution,
   не битовая последовательность. Сохраняет Hook/Direction/Jump; пишет Aim/Fire.

## Проверки текущей версии

Все native fixtures скомпилированы и выполнены на `home`. Общий лог:
`research/codex/final-deep-validation.log`.
- Planner/policy boundary regressions OK.
- World isolation, destruction markers, dummy collisions, actual job pool,
  queued takeover, laser fallback OK.
- Rocket physics126states:85triggers,64acceptedshots,0unsafe first9steps,
 0source mutations. Это не обещание спасения на всей дальнейшей траектории.
- Upper freeze:22/26/33horizons, release1, miny182, frozen0.
- Frozen-head support retainedy338/frozen0; removedy369/frozen1.
- Corridor960states,384safe no-shot22steps,37unnecessaryshots,0harmful inside22;
 37safe no-shot100tickcases x5release moduli ->0harmful. **Именно пользовательский
 промах не воспроизведён. Не утверждать, что он исправлен.**
- Private live A/D walls: stopx639/right and128/left, frozen0, exit0.
 `research/codex/runtime/deep-audit-release/result.json`.
- UI screenshot/click smoke: KRX Avoid/Misc, master, F8 keyreader, conditional
 rows, UnfreezeTicks0 preserved, Rocket checkbox. Last menu-smoke run OK.
- Both desktop files validated, source manifest verified25/25, git diff--check OK.

## Что выяснил новый REA по узкому проходу

Свежие доказательства: `research/codex/corridor-rocket/`.
Original219340/217760 действительно создают гранату из ShotInput в danger probe;
никакого специального запрета выстрела в безопасном проходе нет. Это может
провоцировать поиск/лишний выстрел и в восстановленном алгоритме. Пользователь
выбрал сохранить это поведение.

OriginalCProjectile vtable4aac78 Tick+10 -> wrapper21c6a0 -> **custom24dc80**.
Обычные radius135/inner48/force*2 и ClampVel совпали в зоне0. Дополнительные
telegun/weapon-teleport ветки KRX отсутствуют в текущем DDNet20.3 predictor —
реальное оставшееся расхождение, пока не перенесено. В исследованном обычном
коридоре оно не доказано причиной. 9999 покрывает только22шага, life>8 только
первые9шагов; это не гарантия всей жизни гранаты или прохождения маршрута.

## Оставшиеся ограничения

- Полного live KRX A/B одинаковых сохранённых ситуаций ещё нет.
- Telegun/weapon-teleport prediction, tuning-zone точность, whole18.9.1vs20.3
 physics, точные Hook/Direction toggle consumers требуют продолжения.
- AutoDrag DragAim stub, AFK behavior, ESP drawing, агенты0/1/3 не закончены.
- Original wider pipeline содержит отдельные AutoJumpSave/laser-unfreeze/etc;
 их не добавляли в пространство Blatant Jump, чтобы не выдумывать другой алгоритм.
- Runtime VM checkpoint `research/runtime/FINDINGS.md` (исключение Defender,
 media cleanup) не меняли. Не объявлять старые cleanup-действия выполненными.

## Логи, бэкапы и продолжение

Launcher сохраняет новые реальные сессии в `research/runtime/client-logs/`.
Новые строки Avoid включают life(-1/9999/partial), tick, velocity, hook state/pos,
 входной/выходной aim, check/kick. Rocket: trigger/baseline/best/fired, aim/fire,
 weapon/wanted/hook/time. Это нужно для точного разбора следующих промахов.

Бэкапы: `/home/a/backups/openkrx-before-hook-change-20261028-210535` (полный
код+бинарник+настройки+ярлык, до пользовательской правки хука) и
`/home/a/backups/openkrx-pre-corridor-20261029-124916` (старый установленный
бинарник+текущие на тот момент ещё не собранные исходники Avoid).
Оригинальные данные REA/ZIP/исследования не удалять. Код не коммитили.
