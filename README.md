# Бой Акинаторов

Дуэль на двоих: каждый тайно загадывает персонажа, игроки по очереди задают вопросы «да/нет», кто первым угадал персонажа соперника — тот победил. Ответы считает только сервер.

## Что нужно

- Машина с Linux для сервера и 1–2 машины с Windows/Linux/macOS для клиентов.
- Qt 6.5+ и Qt Creator (на Linux — ещё CMake и компилятор C++17).
- Python 3.10+ и пакет `numpy` на машине сервера — для ML-подсказок.
- ZeroTier на всех машинах — для игры через интернет без настройки роутера.

## 1. Скачать проект

```bash
git clone https://github.com/6ogdashka/OS_AKINATOR.git
cd OS_AKINATOR
```

## 2. Собрать проект

### Через Qt Creator (одинаково для Windows, Linux и macOS)

1. `File → Open File or Project…` → выбрать `CMakeLists.txt` в корне проекта.
2. В окне `Configure Project` выбрать комплект Qt 6 и нажать `Configure Project`.
3. `Build → Build Project` (`Ctrl+B`, в macOS `Cmd+B`).

Готовые файлы — в папке сборки, в подкаталоге `bin`:

- `akinator-server` (`akinator-server.exe` в Windows);
- `akinator-client` (`akinator-client.exe` в Windows).

### Через терминал (Linux, для сервера)

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools python3 python3-numpy
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Файлы появятся в `build/bin`. Если `python3-numpy` из apt недоступен:
`python3 -m pip install -r gamecore/requirements.txt`.

## 3. ZeroTier: объединить машины в одну сеть

Повторить на каждой машине (Windows и Linux).

**Linux:**

```bash
curl -s https://install.zerotier.com | sudo bash   # установить
sudo zerotier-cli join <NETWORK_ID>                # вступить в сеть
```

**Windows:**

1. Скачать установщик с https://www.zerotier.com/download/ и установить.
2. В трее нажать правой кнопкой на значок ZeroTier → `Join Network…`.
3. Вставить `<NETWORK_ID>` и нажать `Join`.

`<NETWORK_ID>` — идентификатор сети, который вы создали на https://my.zerotier.com.

После подключения машин зайдите на https://my.zerotier.com, откройте свою сеть и поставьте галочку `Auth` у каждого нового участника — иначе соединение не разрешено.

**Узнать адрес сервера в ZeroTier:**

- Linux: `sudo zerotier-cli listnetworks` — нужен IP из строки вида `10.x.x.x/24`; либо:

```bash
ip -4 -o addr show | awk '/ zt/ {split($4, a, "/"); print a[1]; exit}'
```

- Windows: правой кнопкой на значке ZeroTier в трее → `Show Networks…` → столбец `Managed IPs`.

## 4. Запустить сервер на Linux

```bash
GAME_SERVER_PORT=9999 ./build/bin/akinator-server
```

В логе должно появиться `Сервер слушает 0.0.0.0:9999`. Если на Linux включён фаервол:

```bash
sudo ufw allow 9999/tcp
```

## 5. Запустить клиенты и подключиться

1. Запустить `akinator-client` на каждом компьютере.
   - В Windows: `akinator-client.exe`. Если клиент не стартует из-за Qt-библиотек, выполните в папке с `.exe`: `windeployqt akinator-client.exe`.
2. В поле «Адрес сервера» ввести ZeroTier-адрес сервера из шага 3 (например, `10.126.245.249`), порт — `9999`.
3. Нажать «Подключиться». Когда подключились оба, выбрать персонажа и нажать «Загадать выбранного».
4. Дальше по очереди: «Задать вопрос» (ответ считает сервер), «Открыть таблицу», три подсказки, «Угадать». Побеждает тот, кто первым угадал персонажа соперника.

## Возможные проблемы

| Симптом | Решение |
|---|---|
| `Connection refused` | сервер не запущен, неверный адрес или порт закрыт фаерволом |
| «Сервер занят» | лобби рассчитано на 2 игроков — закройте лишний клиент |
| «Не найден Qt6Widgets.dll» | выполнить `windeployqt akinator-client.exe` |
| «Не найден gamecore.dll» | переносить всю папку `bin`, а не один `.exe` |
| Машины не видят друг друга | проверить статус ZeroTier (`OK`) и галочки `Auth` на my.zerotier.com |
