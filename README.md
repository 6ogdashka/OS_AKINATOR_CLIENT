# Бой Акинаторов — клиент

GUI-клиент для игры «Бой Акинаторов». Сервер и игровая логика — в отдельном репозитории: https://github.com/6ogdashka/OS_AKINATOR_SERVER

Клиент подключается к серверу по ZeroTier и не требует Python, numpy или серверных файлов.

## Что нужно

- Windows, Linux или macOS.
- Qt 6.5+ и Qt Creator; для запуска готового клиента — Qt 6 runtime.
- ZeroTier на машине игрока (адрес сервера сообщает хозяин сервера).
- CMake и компилятор C++17 для сборки.

## Сборка в Qt Creator

1. `File → Open File or Project…`
2. Выбрать `CMakeLists.txt` в корне этого репозитория.
3. Выбрать комплект Qt 6 → `Configure Project`.
4. `Build → Build Project` (`Ctrl+B`).
5. `Run` (`Ctrl+R`).

Готовый клиент появится в `build/bin`:

- `akinator-client` (`akinator-client.exe` в Windows);
- `libgamecore.so` / `gamecore.dll` — runtime-библиотека рядом с клиентом.

Собрать клиентскую папку для раздачи:

```bash
cmake --build build --target package-client
```

Результат: `build/dist/client`.

## Сборка через терминал

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

## Подключение

1. Установите ZeroTier и вступите в сеть сервера (network ID и авторизацию выдаёт хозяин сервера).
2. Запустите клиент.
3. В поле «Адрес сервера» введите ZeroTier-IP сервера, порт — `9999`.
4. Нажмите «Подключиться». Когда подключились оба игрока, выберите персонажа и играйте.

Адрес также можно задать переменными `GAME_SERVER_HOST` и `GAME_SERVER_PORT`.

## Windows: Qt DLL

Если клиент не стартует из-за отсутствующих Qt-библиотек, в папке с `.exe` выполните:

```bat
windeployqt akinator-client.exe
```

## Возможные проблемы

| Симптом | Решение |
|---|---|
| `Connection refused` | сервер не запущен, неверный ZeroTier-IP/порт |
| «Сервер занят» | лобби рассчитано на 2 игроков |
| Не найден `Qt6Widgets.dll` | выполнить `windeployqt akinator-client.exe` |
| Не найден `gamecore.dll` / `libgamecore.so` | переносить всю папку `bin`/`dist/client`, а не один `.exe` |
| Машины не видят друг друга | проверить ZeroTier (`OK`) и `Auth` на my.zerotier.com |

## Сервер

Инструкция по сборке и запуску сервера: https://github.com/6ogdashka/OS_AKINATOR_SERVER
