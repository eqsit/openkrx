# Сделано и оставшийся план OpenKRX

Актуально на 2026-11-02. Блок **41 пункта №34–74** перенесён в исходники:
TAS — 32, Pathfinding — 8, Avoid Fent — 1. Матрица реализации, исходные
особенности и ограничения: [TAS_PATH_FENT.md](TAS_PATH_FENT.md).
Номера соответствуют FEATURE_ESTIMATES.md; Legit и Basic исключены.
Реализация не означает подтверждённую абсолютную 1:1 идентичность.
Linux/Windows опубликованы в [v0.1.5](https://github.com/eqsit/openkrx/releases/tag/v0.1.5);
исходник сборок — `7470801c773f5db498a5c829f651948385891730`.

## Что сделано

- Реализованы Blatant / Avoid Freeze и Rocket Save; сохранены пользовательские
  дополнения: ручной прыжок, возврат оружия и поправка Fire-sync.
- Выполнен перенос/исследование пунктов 1–33 до TAS: настройки prediction,
  Version Spoofer, смена имени у финиша, Killsay / Deathsay, Fancy Chat Font,
  Chat Repeater, Mass Mention Spam, checksum и логирование, Auto Vote Kick,
  Discord RPC, Notifications, Tile Outlines, Mod Detector, Auto Verify,
  KoG Frozen HUD, AFK Protection, Fast Input / Others, Auto Edge, Ghost Move,
  Trajectory ESP и Find Tunes. Детали каждого пункта — в [PRE_TAS_PORT.md](PRE_TAS_PORT.md).
- Меню Misc / Visuals & HUD / Settings восстановлено по обработчикам GoodCrack:
  порядок и размеры блоков, условия полей, подсказки, режимы и бинды.
- Сборки Linux и Windows x86_64 опубликованы в
  [релизе v0.1.4](https://github.com/eqsit/openkrx/releases/tag/v0.1.4).
  Исходник сборок — `9d0aaa10f4bf1c271fce8dc579840f1194596939`.
  Размеры и GitHub SHA256 обоих архивов сверены с локальными файлами.
- Linux проверен в игровом прогоне, Windows — под Wine. Проверены prediction,
  movement, текстовые преобразования/RNG, checksum и Fast Input main/dummy/Others.
  Локальные ярлыки обновлены; исходное состояние кода сохранено в бэкапе.

Пункты 1–33 нельзя считать 33 отдельными активными системами:

- №8 Send Ads в оригинальном обработчике только сбрасывает флаг.
- №18 Prediction Benchmark используется в checksum metadata; отдельный
  измеряющий обработчик в исследованных ссылках оригинала не найден.
- №25 Track Point ESP и №26 Aimbot ESP: флаги и меню сохранены; собственная
  отрисовка в исследованных прямых ссылках оригинала не найдена.
- №27 Path ESP реализован вместе с Fent.
  Отдельная отрисовка пути для Blatant не добавлялась.

## Перенесённый блок — 41 пункт

### TAS — 32 пункта

- [x] №34 TAS TPS
- [x] №35 TAS Tick Control Pause
- [x] №36 TAS Tick Control Tick Count
- [x] №37 TAS Tick Control Step
- [x] №38 TAS Respawn
- [x] №39 TAS Player Prediction — исходные флаг/меню; consumer в original refs не найден
- [x] №40 TAS Recording
- [x] №41 TAS Pause
- [x] №42 TAS Playback
- [x] №43 TAS Clear Replay
- [x] №44 TAS Save Replay
- [x] №45 TAS Load Replay
- [x] №46 TAS Delete Replay
- [x] №47 TAS Replay Description
- [x] №48 TAS Auto Save Replay
- [x] №49 TAS Auto Replay
- [x] №50 TAS Restart on Freeze
- [x] №51 TAS Kill on Replay End
- [x] №52 TAS Sound
- [x] №53 TAS Effects
- [x] №54 TAS Show Real Aim
- [x] №55 TAS HUD
- [x] №56 TAS Recorded Path ESP
- [x] №57 TAS Predicted Path ESP
- [x] №58 TAS Start and End Position ESP
- [x] №59 TAS Fake Aim
- [x] №60 TAS Rewind
- [x] №61 TAS Forward
- [x] №62 TAS Auto Rewind
- [x] №63 TAS Auto Forward
- [x] №64 TAS Replay Vault
- [x] №65 TAS Replay Vault Auto Sync

Реализованы отдельный мир симуляции, управление тиками, запись и playback.
Vault проверен с внешним сервисом; он зависит от его доступности.

### Pathfinding — 8 пунктов

- [x] №66 Pathfinding
- [x] №67 Pathfinding Reset Goal
- [x] №68 Pathfinding Flow Field
- [x] №69 Pathfinding Tunnel Editor
- [x] №70 Pathfinding Tunnel Rendering
- [x] №71 Pathfinding Clear Tunnels
- [x] №72 Pathfinding Auto Tunnel from TAS
- [x] №73 Pathfinding Auto Tunnel Width

### Avoid Fent — 1 пункт

- [x] №74 Avoid Fent — алгоритм, worker и отрисовка Path ESP №27.

## Исключено из плана — не делать

Прямое решение пользователя от 2026-11-01. Это не отложенные задачи:

- №75 **Avoid Legit — НЕ НУЖЕН, НЕ РЕАЛИЗОВЫВАТЬ.**
- №76 **Avoid Basic — НЕ НУЖЕН, НЕ РЕАЛИЗОВЫВАТЬ.**

## Проверки и доводка уже перенесённого

Эти работы идут отдельно от 41 нового пункта:

- Прямое сравнение исполнения OpenKRX с запущенным оригинальным GoodCrack,
  включая Blatant / Rocket и редкие случаи физики; исправление найденных расхождений.
- Сравнение меню с оригиналом по изображениям.
- Проверка Discord RPC с реальным Discord и внешнего Auto Verify.
- Запуск релизного пакета на настоящей Windows; текущая проверка выполнена под Wine.

Абсолютная идентичность оригиналу пока не подтверждена. Подробный результат
переноса — [PRE_TAS_PORT.md](PRE_TAS_PORT.md), история — [JOURNAL.md](JOURNAL.md).
