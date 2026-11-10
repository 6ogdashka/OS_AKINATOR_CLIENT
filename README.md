# Бой Акинаторов

## Сборка в Qt Creator

Инструкция одинакова для Windows, Linux и macOS.

1. Установите Qt 6.5+ вместе с Qt Creator.
2. Откройте Qt Creator и выберите `File → Open File or Project…`.
3. Укажите `CMakeLists.txt` в корне проекта.
4. В окне `Configure Project` выберите комплект Qt 6 и нажмите `Configure Project`.
5. Соберите проект: `Build → Build Project` (`Ctrl+B`, в macOS `Cmd+B`).

Готовые файлы находятся в папке сборки, в подкаталоге `bin`:

- `akinator-server` (`akinator-server.exe` в Windows);
- `akinator-client` (`akinator-client.exe` в Windows).

Если Qt Creator предложил другую папку сборки, готовые файлы будут в её подкаталоге `bin`.
