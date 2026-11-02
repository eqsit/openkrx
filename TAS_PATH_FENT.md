# TAS, Pathfinding и Fent — перенос GoodCrack/KRX

Актуально: 2026-11-02. В исходники OpenKRX перенесён блок №34–74 исходного
FEATURE_ESTIMATES.md. Legit и Basic исключены по решению пользователя.
Алгоритмы восстановлены через REA-agents/Ghidra по аналитическому PE SHA256
`0477b21ed12ce04a3a657a26be2f75cfcfc9b7429d6ad07ff8ecd3359616dc3d`.
Это восстановление по бинарнику с проверками порта; абсолютная идентичность
исполнению оригинала и изображениям его меню не установлена.

## Использование

TAS находится в Settings → KRX → TAS. Start recording создаёт отдельный мир;
сетевой персонаж остаётся на месте. Pause, Rewind и Forward управляют этим миром.
Перед Playback остановите запись и вернитесь в начальную позицию сохранённого
TAS. В начале записи есть исходные 50 нейтральных кадров.

Для направления глаз/оружия по записанному вводу включите Show real aim.
При выключенной настройке локальный взгляд следует мыши, как в оригинале.
Fake Aim меняет прицел в записи с проверкой сохранения траектории.

Fent находится в Avoid → Fent. Настройте цель Pathfinding, включите поиск и
дождитесь `Replay transferred to TAS (... frames). Ready for playback.`.
Полоска во время поиска показывает вычисляемую траекторию. Затем используйте
TAS → Playback replay с той же стартовой позиции. Прерывание поиска не передаёт
готовый результат. Playback во время поиска теперь объясняет необходимость
подождать. Fent может остановиться на частичном маршруте: сохранены исходные
условия остановки, включая расстояние до цели в пять тайлов и finish score.

Replay Vault синхронизируется с исходным krxclient/krx-replays. Браузер открывает
папки и загружает TAS двойным нажатием или Enter. Сохранения лежат в `replays/`;
автосохранения — `replays/auto/`. Поддерживаются `.tas` и старые JSON, есть
конвертация старого формата. Vault и расчёт поля вынесены из главного потока;
установка файлов ограничена восемью файлами или бюджетом 2 мс за кадр.

## Матрица переноса

В таблице «перенесено» означает наличие восстановленной логики и интеграции,
а не результат прямого A/B с работающим оригиналом.

| № | Возможность | Реализация / проверка |
|---|---|---|
| 34 | TPS | Целочисленный интервал миллисекунд; live TPS10 |
| 35 | Tick Control Pause | Пауза после rewind/forward; live |
| 36 | Tick Count | Число шагов, включая 0; live ±3 |
| 37 | Tick Control Step | Один шаг при удержании; live |
| 38 | Respawn | Новый мир и neutral50; live |
| 39 | Player Prediction | Флаг/меню перенесены; в найденных original refs нет consumer, reset всегда prediction=true |
| 40 | Recording | Изолированные миры и снимки, main/dummy; live |
| 41 | Pause | Остановка тиков; live |
| 42 | Playback | Полный input каждый tick, позиционная проверка, обе tee; live |
| 43 | Clear Replay | Очистка записи, подтверждение в меню; live |
| 44 | Save Replay | LE32 + zlib JSON, атомарная запись; native/live |
| 45 | Load Replay | Транзакционная загрузка, вложенные папки; native/live/UI |
| 46 | Delete Replay | Удаление выбранного файла; live |
| 47 | Description | Сохранение/чтение метаданных; native/live |
| 48 | Auto Save | Отдельный enable и интервал, каталог auto; live |
| 49 | Auto Replay | Повторное воспроизведение; live |
| 50 | Restart on Freeze | Проверка freeze и перезапуск; live |
| 51 | Kill on End | Отправка kill при auto replay; source review, отдельного сетевого A/B нет |
| 52 | Sound | События TAS мира, переключатель; live smoke |
| 53 | Effects | События мира, airjump/hammer/explosion/damage; live smoke |
| 54 | Show Real Aim | Угол по recorded input при playback; source review/render smoke |
| 55 | HUD | Ввод, reload, jumps, grounded по 30e1b0; screenshot smoke |
| 56 | Recorded Path | Цвет, линии/точки, сегмент; render smoke |
| 57 | Predicted Path | 51 тик, own/others/freeze цвета; render smoke |
| 58 | Start/End ESP | Tee-маркеры и рамки; render smoke |
| 59 | Fake Aim | Robot/Spin/Rand/Smooth, критический hook/fire; native + live dummy |
| 60 | Rewind | Восстановление мира и fire, усечение обеих записей; live |
| 61 | Forward | Исходная подача одного input, запись обеих tee; live |
| 62 | Auto Rewind | Исходные back4–9, две slot-input, active client, probe21; live freeze |
| 63 | Auto Forward | До одного тика перед оттаиванием; live freeze |
| 64 | Replay Vault | Реальная загрузка 179 файлов и commit cache; live |
| 65 | Auto Sync | Один раз при открытии TAS page; source review/UI |
| 66 | Pathfinding | Reverse Theta*, occupancy, Gaussian3x3; native |
| 67 | Reset Goal | Поиск middle-of-255 finish либо ручная цель; native/live |
| 68 | Flow Field | Отрицательный parent-flow, стрелки; native/render smoke |
| 69 | Tunnel Editor | Левая/правая кнопки и интерполяция тайлов; source review |
| 70 | Tunnel Rendering | Розовая заливка; render smoke |
| 71 | Clear Tunnels | Обнуление маски; live |
| 72 | Auto Tunnel | Flood8 и маршрут обеих tee; live |
| 73 | Auto Tunnel Width | Полуширина config×16; native/source review |
| 74 | Fent | Исходный GA и Path ESP №27, worker/cancel/передача в TAS; native/live |

## Существенные детали алгоритмов

TAS: neutral50, первый playback input1, exact-position проверка; integer-ms
TPS gate; отрицательный rewind intra; отдельные current/previous worlds;
neutral network input сохраняет fire/aim/flags/weapons. Original forward
подаёт один input, а записывает обе tee. Player Prediction не получил
придуманного потребителя: в исследованных ссылках оригинала его тоже нет.

Pathfinding: reverse Theta* с Euclidean cost, исходный Bresenham; solid/nohook/
death/freeze/deep/tele/tunnels mask, square light-tile support; один Gaussian
3×3 pass с нормализацией. Автоцель — средний элемент среди максимум 255 finish.

Fent: MT19937 и Windows uniform/Bernoulli, packed32 input, исходная ошибочная
sticky-jump запись Direction, score alignment×1000 и coast12×1750, elite20%,
tournament5, crossover/mutation, decay .96/min.1 и MSVC sort с сохранением ties.
Invalid evaluation сохраняет предыдущий score, initial score=0. Исходный
финальный выбор не пересчитывает последнее поколение. Path ESP принадлежит
Fent; отдельный выдуманный путь Blatant не добавлен.

## Проверки и ограничения

- Дополнительная проверка реальных оригинальных записей AiP-Gores: **13 053
  кадра, 0 расхождений позиции** в Linux native simulation (3 записи оригинала,
  ранее снятые под Wine). Стенд должен передавать DDNet extended flags:
  без них fallback ошибочно выводит jetpack из tuning strength.
- На одном private сервере Low: оригинал под Wine31.55–38.26с,
  OpenKRX Linux32.98–34.64с (по3 прогона). Это ограниченный замер случайного
  поиска; подробности и хеши — research/fent-fidelity-20261102/ORIGINAL_AB_RESULT.md.

- Native на ssh home: **2 357 031 проверка, 0 ошибок**, 179 настоящих `.tas`,
  **1 010 553 кадра**; codec roundtrip, 100k packed inputs, flow, physics,
  fake aim с hook/jetpack, MSVC sort ties/permutations, legacy JSON.
- Изолированный Linux server/client/Xvfb: запись, pause/step/rewind/respawn,
  save/load/validate/delete, dummy, четыре fake-aim режима, auto save, tunnel,
  Vault/cache, Fent completion/cancellation/disconnect, freeze rewind/forward.
- Fent → Playback: раннее нажатие отклонено с сообщением; после передачи
  89 кадров реальная tee сдвинулась с X272 до X319. Прерывание пользователем
  незавершённого поиска само по себе не считается ошибкой алгоритма.
- Vault full sync: 179 файлов, 613 запросов heartbeat; максимальное время
  ответа главного потока 14.18 мс, p99 13.02 мс на тестовой машине.
- Регрессии Avoid, tile/projectile physics, Fire-sync, Rocket restore и
  pre-TAS movement/prediction прошли. Тестовая карта для projectile fixture
  выбирается самим runner: `--map` подставляет AiP-Gores и для неё непригоден.
- Окончательная Windows-сборка проверена под Wine; это не нативный Windows
  A/B оригинала. Pixel A/B меню, редкие physics сценарии и все варианты карт
  остаются отдельной проверкой. Само наличие пункта меню их не доказывает.

Намеренные адаптации: bounds/CRC в файлах и архивах, атомарные сохранения,
worker cancellation/join, isolated collision, async Vault/field, отсутствие
NaN для пустого Fent candidate. Original одноимённые enable/interval autosave
разделены: `openkrx_autosavereplay_enabled` и `krx_autosavereplay` (интервал).
Защита от проигрывания neutral prefix при ещё работающем Fent — улучшение UX.

Локальные Evidence и журналы: `research/tas-path-fent-20261101/` (167 функций,
12 controller, 18 codec, меню/регистрация/assembly и inline REA Evidence).
`owned-evidence.json` — локальная подборка полных записей; после прерывания
сессии лимитом/перезагрузкой это не выдаётся за экспорт прежнего MCP ledger.
Оригинальный PE и сырые исследования не включаются в релизные пакеты.
