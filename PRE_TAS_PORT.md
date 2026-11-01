# Перенос GoodCrack: пункты 1–33 до TAS

Эталон: `research/rea/krx-source-analysis.exe`, SHA256
`0477b21ed12ce04a3a657a26be2f75cfcfc9b7429d6ad07ff8ecd3359616dc3d`.

Код восстановлен по REA/Ghidra и встроен в текущий DDNet-порт. Сохранены
конфигурационные имена, defaults/ranges, условия вызова, порядок ботов и
известные особенности исходных алгоритмов. Таблица ниже описывает код, а не
доказанное совпадение каждого исполнения с Windows-оригиналом. Нативный
original/port A/B и пиксельный A/B оригинального меню не получены.

| № | Функция | Что перенесено / поведение оригинала |
|---:|---|---|
| 1 | Teleport Prediction Menu | Исходный checkbox, tooltip и глобальный teleport consumer; tele-in при выключенном параметре — терминальный freeze. |
| 2 | Version Spoofer | Getter:18091 при off; при on заданное значение10000..20000, default18080. |
| 3 | Change Name on Finish | Первые255 game/front finish tiles, средний элемент, дистанция512, latch; fixed/random name, copy16bytes, SendInfo. Все таблицы/пять вариантов генерации и последующие мутации с пределом15bytes. |
| 4 | Killsay / Deathsay | Исходные93/48 фразы, MSVC rand, FNG/Ninja gate, selfkill/suppress-events, формат255bytes. |
| 5 | Fancy Chat Font | Исходное UTF-8 преобразование ASCII букв только при отправке, slash-команды без преобразования. |
| 6 | Chat Repeater | Активный чужой sender, min byte length, строгий cooldown250, чередование регистра по байтам. |
| 7 | Mass Mention Spam | Исходный forward shuffle и независимый RNG префикса, исходные строки/zero-width вставки, cooldown250. |
| 8 | Send Ads | В эталонном обработчике235e90 флаг просто сбрасывается. Не придуман отправитель рекламы. |
| 9 | Send Checksum | При off запрос не получает ответа/error; при on исходный алгоритм SHA256/UUID/ranges и штатные ошибки. Данные конкретной Linux-сборки естественно отличаются от PE-образа. |
| 10 | Checksum Logging | Исходные условия/названия строк; UUID/digest десятичными байтами через comma-space; Error при неуспехе. |
| 11 | Auto Vote Kick | Vote timer gate, specific/random/same-team фильтры, MSVC rand, исходная128-byte консольная команда. |
| 12 | Discord RPC | Снимок startup config,3 исходных ID/custom stoll, SDKv3 flags, Offline/Online/map/logo, lifecycle. |
| 13 | Notifications | Очередь с mutex, expiry/duplicates, максимум3, исходные размеры/цвета/icons/renderflags; временное подавление kill messages и восстановление. Подключён исходный pre-TAS AutoTeam warning. |
| 14 | Tile Outlines | Исходные game/tele contour, категории/соседи/внутренние углы, far-zoom bounds, цвета/alpha/width. |
| 15 | Mod Detector | Исходные89 DDNet/25 KoG/156 подозрительных имён, auth/name/leave/log, HUD и scoreboard [MOD]/[WARN]. |
| 16 | Auto Verify | Исходный GET ger10.ddnet.tw, timeout/low-speed, synchronous wait,3 ошибки/lifetime latch, после component updates; Manual Verify в меню. |
| 17 | KoG Frozen HUD | Team/watch selection, snapshot membership, freeze counter/last notify, rows/columns/overflow, skins/ninja, cursor freeview. |
| 18 | Prediction Benchmark | Подтверждён параметр checksum. Самостоятельный измеряющий обработчик в исследованных ссылках эталона не найден; не добавлен выдуманный benchmark. |
| 19 | Advanced Prediction Settings | Исходные условные поля и tooltip, margin20..300. |
| 20 | Prediction Players Settings | Исходные deferred player/body/drag и movement collision loops; hook intersection не выключается. Серверная физика отделена от client options. |
| 21 | Prediction Death Tile Settings | Исходный глобальный death/AutoEdge/Avoid OR gate и terminal freeze; super исключение. |
| 22 | Prediction Move Restrictions Settings | Исходные gates вычисления stopper restrictions в core и character. |
| 23 | Prediction Heart Settings | Исходный gate freeze-heart pickup, без придуманного звука. |
| 24 | Avoid AFK Protection | Строгий таймер, rearm, прежняя мышь и оба direction key, исходная grounded-проверка; план Avoid при gate не очищается. |
| 25 | Avoid Track Point ESP | Исходный флаг/UI сохранён. В исследованном Blatant нет собственного renderer; direct xrefs — UI/registry. Не добавлена чуждая оригиналу отрисовка. |
| 26 | Avoid Aimbot ESP | Аналогично25: сохранён исходный флаг/UI, не изобретён renderer. |
| 27 | Avoid Path ESP | Исходная отрисовка относится к Fent. Render slots Basic/Blatant/Legit — пустой return. Флаг сохранён; Fent из пунктов после TAS не переносился. Самостоятельного Blatant path renderer не добавлено. |
| 28 | Fast Input Others | Дополнительный visual tick других игроков/dummy только при включённых FastInput+Others. |
| 29 | Fast Input | Raw cache/7 action comparisons; per-frame repredict; extra tick + восстановление физического мира; исходный порядок DummyFirst. |
| 30 | Auto Edge | Исходные candidate scan/order/ties, predicted-only26 steps, current target, direction0/-1/+1 и strict thresholds. |
| 31 | Ghost Move | Исходные cached-tick parity/alternation/aim/closest hook; сохранение process-static состояния; snapshot direction arrows. |
| 32 | Trajectory ESP | Исходная копия мира до ботов, moving tees,0..ticks inclusive, input order, freeze markers и fading/line/circle helpers. |
| 33 | Find Tunes | Исходные наблюдения projectile/explosion, fire+=2, brute-force float steps, tolerance/order. Выводит результат, автоматически настройки не применяет. |

Для25/26 bounded direct-reference search не исключает непрозрачных pointer aliases;
для27 зависимость Fent подтверждена vtables/selector/decision/render. Эти пункты
нельзя рекламировать как новые самостоятельные работающие ESP в Blatant.

Меню Misc/Visuals/Settings сверено с исходными251090/255740/25fdd0: порядок и
высоты карточек, conditional поля, checkbox/dropdown различия, key readers,
help links, prediction tooltips, Discord restart flow. Консольные параметры,
которых нет в оригинальном меню, не вынесены в выдуманную дополнительную карточку.
Пользовательские дополнения manual jump/weapon restore оставлены в OpenKRX.
TAS, Pathfinding и Fent/Legit/Basic остаются за границей этой задачи.

Локальные исследовательские Evidence не публикуются вместе с исходниками. Подробные Evidence/ограничения: `misc/REVIEW.md`, `movement/AUDIT.md`,
`visuals/REPORT.md`, `prediction/*.json`. Итоговые проверки и точная идентичность
установленной сборки записываются в `RESULT.md` и `verification.json`.

Первичный бэкап до работы:
`/home/a/backups/openkrx-before-pre-tas-20261101-155937` —1891file source.tar.gz,
SHA256 `f4372b8827c98cf1bd983c396ea8d38016d13c28ae2a328d30d6fb93b2e6fcf9`,
HEAD/status/patch и Git bundle. Коммит/публикация релиза не выполнялись.
