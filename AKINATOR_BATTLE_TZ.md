# ТЗ: Онлайн-игра «Бой Акинаторов» — задание для агента-разработчика

## 0. Как работать с этим документом (прочитай первым)

Ты — coding-агент. Реализуй проект целиком по этому документу.
**Не задавай уточняющих вопросов.** Если что-то неоднозначно — бери вариант,
указанный здесь, и продолжай работу.

- Создай проект в новой папке `akinator-battle/` рядом с этим файлом.
- Данные (60 персонажей × 139 вопросов) уже лежат в `gamecore/src/data.h`;
  отдельного CSV/генератора нет — это единственный источник истины.
- Выполняй задачи строго по порядку из раздела 11 (Задача 1 → 2 → … → 11).
- После каждой задачи проект обязан собираться: `cmake --build build`.
  Тесты, где указаны, должны быть зелёными.
- После каждой задачи коротко отчитайся: что сделал, какие файлы создал/изменил,
  вывод команд проверки.
- Не делай «всё сразу» и не выходи за рамки текущей задачи.

---

## 1. Допущения и риски

**Допущения (не менять):**
- Игра — дуэль Акинаторов: 2 игрока, каждый тайно загадывает персонажа из 60
  отобранных (`gamecore/src/data.h`), по очереди задают вопросы да/нет,
  кто первым угадает персонажа противника — тот победил.
- Таблица: 60 персонажей × 139 бинарных вопросов; значения только `1` (да) и
  `-1` (нет), варианта «не знаю» нет. Источник истины — `gamecore` (данные вшиты).
- Сервер авторитетный: хранит секреты и наборы кандидатов обоих игроков, сам
  считает ответ на вопрос (по строке секрета противника) и сам фильтрует
  кандидатов после **любого** вопроса — и обычного, и подсказки (см. раздел 7).
  Клиент только отображает.
  Обоснование: данные детерминированы —
  ответ всегда известен, нет рассинхрона и читерства.
- Подсказки (дисперсия / Random Forest / CatBoost) реализованы в Python-модуле
  `gamecore/ml_hints.py` (Python 3 + numpy): настоящие алгоритмы — максимум
  дисперсии, Random Forest (бэггинг + случайные подмножества признаков +
  Gini-важность) и градиентный бустинг CatBoost-стиля (oblivious-деревья,
  logloss, L2). `gamecore` держит один постоянный Python-процесс (`QProcess`,
  обмен JSON-строками) и вызывает его из `hintVariance/hintRandomForest/hintCatBoost`.
  Обоснование: полноценные ML-алгоритмы, а не эвристики «для вида»; таблица
  маленькая, поэтому подсказка считается за десятки миллисекунд.
- Протокол — текстовые команды: одна строка UTF-8, аргументы через пробел,
  разделитель `\n`.
- Свой секрет сервер не возвращает (это тайна от соперника), поэтому клиент
  запоминает выбранного персонажа локально и показывает его игроку подписью
  «Ваш персонаж: N» всю партию — чтобы не забыть, кого загадал.
- Персонажи и вопросы передаются по сети индексами (`characterId` 0..59,
  `questionId` 0..138); тексты/имена стороны берут локально из `gamecore`.
- Версии: Qt 6.5+, CMake ≥ 3.16, C++17, Python 3.10+ с numpy (для подсказок).
- Публичный доступ — через ZeroTier; собственный NAT traversal/P2P/relay не реализуется.
- Один матч одновременно; третий подключившийся отклоняется.

**Риски (и как закрываем):**
- Python-воркер может отсутствовать/упасть → `gamecore` запускает его лениво,
  один раз перезапускает при сбое, использует таймауты и при ошибке возвращает
  `question=-1` (подсказка не тратится). Фиксированные seed и гиперпараметры
  дают детерминизм; тесты раздела 10 проверяют валидность и детерминированность.
- Нет Python 3 или numpy → подсказки недоступны (`ERROR` «Нет информативного
  вопроса»), остальная игра работает. Зависимость ставится из
  `gamecore/requirements.txt`.
- Файрвол/NAT публичного сервера — не решается кодом (чек-лист раздела 12).
- Разрыв соединения — `OPPONENT_LEFT` + сброс лобби (раздел 7).
- Windows не находит `gamecore.dll` — единая папка вывода бинарников (раздел 4).
- `ml_hints.py` не оказался рядом с бинарниками — CMake копирует его POST_BUILD;
  путь к Python/скрипту переопределяется `AKINATOR_PYTHON`/`AKINATOR_ML_SCRIPT`.
- Русский текст — файлы в UTF-8, строки через `u8"..."`.

---

## 2. Архитектура и потоки данных

```
[Клиент A: Windows]                     [Сервер: Linux + ZeroTier]                  [Клиент B: Linux/macOS]
 QtWidgets (GUI)                        QTcpServer (listen)                         QtWidgets (GUI)
 QTcpSocket ──TCP (текст)──►  accept ◄──  секреты + кандидаты обоих  ──►  accept ◄──TCP (текст)── QTcpSocket
 protocol.h (текст)                        gamecore: matrix/filter/3 подсказки          protocol.h (текст)
 gamecore (имена/вопросы/данные)           protocol.h (текст)                           gamecore (имена/вопросы/данные)
                                           gamecore ──QProcess/JSON──► ml_hints.py
                                           (Python 3 + numpy: RF / CatBoost / variance)
```

**Роли целей:**
- `gamecore` (SHARED, C++ + Qt6::Core) — данные (имена/тексты/матрица),
  `filterCandidates` и C++-обёртки подсказок. Сами подсказки считает постоянный
  Python-воркер `gamecore/ml_hints.py` (numpy), запускаемый через `QProcess`.
  Используют **сервер, клиент, тесты**.
- `akinator_protocol` (INTERFACE, header-only) — разбор и сборка текстовых строк.
  Используют **сервер, клиент, тесты**.
- `akinator-server` (EXE) — авторитетный сервер.
- `akinator-client` (EXE) — GUI.
- `tst_gamecore`, `tst_protocol` (EXE) — unit-тесты.

**Поток данных (счастливый путь):**
1. Клиент читает env → предзаполняет host/port → `Connect`.
2. Оба клиента шлют `SECRET` (выбор персонажа из 60). Сервер хранит секреты.
3. Когда оба загадали → `START` + `TURN` (ходит игрок 1).
4. Ход: игрок шлёт `ASK` (или `HINT`). Сервер берёт значение вопроса у
   **секрета противника** → формирует ответ и сам сокращает **кандидатов
   спрашивающего** через `filterCandidates` (одинаково для `ASK` и `HINT`,
   см. раздел 7) → шлёт ему `ANSWER` (+вид подсказки для `HINT`), противнику
   `OPPONENT` (инфо) → `TURN` второму.
5. Игрок может `TABLE` / `REMOVE` (просмотр и ручное удаление из своего
   списка) — без смены хода.
6. Игрок шлёт `GUESS`. Верно → `OVER` обоим; неверно → `WRONG` + смена хода.
7. Первый верно угадавший побеждает.
---

## 3. Дерево файлов проекта

```
akinator-battle/
├── CMakeLists.txt              # корневой: C++17, опции client/server/tests, find_package(Qt6), единая папка bin/
├── README.md                   # описание, сборка, запуск, env, демо (структура — раздел 13)
├── .gitignore                  # build/, *.user, CMakeCache.txt
│
├── gamecore/                   # SHARED-библиотека (Qt6::Core только для QProcess)
│   ├── CMakeLists.txt          # add_library(gamecore SHARED ...), GAMECORE_BUILD, Qt6::Core, копирование ml_hints.py
│   ├── requirements.txt        # numpy>=1.24
│   ├── ml_hints.py             # Python-воркер: variance / Random Forest / CatBoost (JSON-lines)
│   ├── include/gamecore/
│   │   ├── export.h            # макрос GAMECORE_EXPORT (dllimport/dllexport / visibility)
│   │   └── gamecore.h          # типы, данные, filterCandidates, HintResult, 3 подсказки
│   └── src/
│       ├── gamecore.cpp        # filterCandidates + QProcess-мост к ml_hints.py
│       └── data.h              # единственный источник данных: 60 имён, 139 текстов, матрица 60×139 (±1)
│
├── common/                     # Header-only помощник протокола, использует Qt::Core
│   ├── CMakeLists.txt          # add_library(akinator_protocol INTERFACE), link Qt6::Core
│   └── protocol.h              # константы, takeLines/parseLine/readInt/makeLine
│
├── server/
│   ├── CMakeLists.txt          # add_executable(akinator-server ...), link Qt6::Core Qt6::Network akinator_protocol gamecore
│   ├── main.cpp                # QCoreApplication, чтение env, запуск Server
│   ├── server.h                # класс Server: QTcpServer, лобби, секреты, кандидаты, ходы
│   └── server.cpp              # accept, SECRET/ASK/HINT/GUESS/REMOVE, disconnect, рассылка
│
├── client/
│   ├── CMakeLists.txt          # add_executable(akinator-client ...), link Qt6::Widgets Qt6::Network akinator_protocol gamecore
│   ├── main.cpp                # QApplication, чтение env, создание MainWindow
│   ├── mainwindow.h            # GUI: выбор персонажа, вопросы, подсказки, таблица, угадать, статус
│   ├── mainwindow.cpp          # слоты кнопок, реакции на сообщения
│   ├── connection.h            # обёртка QTcpSocket + фрейминг строк по '\n'
│   └── connection.cpp          # readyRead → накопление буфера → эмит строк по '\n'
│
└── tests/
    ├── CMakeLists.txt          # add_executable(tst_gamecore, tst_protocol), add_test(...)
    ├── tst_gamecore.cpp        # тесты данных/filter/подсказок (QtTest)
    └── tst_protocol.cpp        # тесты текстового протокола (QtTest)
```

---

## 4. Структура CMake

**Корневой `CMakeLists.txt`:**
- `cmake_minimum_required(VERSION 3.16)`
- `project(akinator LANGUAGES CXX)`
- `set(CMAKE_CXX_STANDARD 17)` + `set(CMAKE_CXX_STANDARD_REQUIRED ON)`
- Опции: `AKINATOR_BUILD_CLIENT`, `AKINATOR_BUILD_SERVER`, `AKINATOR_BUILD_TESTS` (по умолчанию `ON`).
- `find_package(Qt6 REQUIRED COMPONENTS Core Network)`, плюс `Widgets` только при клиенте и `Test` только при тестах.
- **Единая папка вывода**: `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)` (важно для Windows DLL).
- `enable_testing()` только при `AKINATOR_BUILD_TESTS`.
- `add_subdirectory(gamecore)`, `add_subdirectory(common)`, затем `server`/`client`/`tests` по опциям.
- `client/CMakeLists.txt` и `server/CMakeLists.txt` — standalone-проекты: их можно открыть в Qt Creator напрямую.

**Qt-компоненты (обязательные):**

| Компонент | Кто использует | Зачем |
|---|---|---|
| `Core` | gamecore, protocol, server, client, tests | `QString`, `QByteArray`, `QObject`, `QCoreApplication`, `QProcess`, `QJson*` |
| `Network` | server, client | `QTcpServer`, `QTcpSocket` |
| `Widgets` | client | GUI |
| `Test` | tests | `QTest` |

**Python-зависимости подсказок:** Python 3.10+ и `numpy` (`gamecore/requirements.txt`). CMake не собирает Python-код, но POST_BUILD-командой копирует `ml_hints.py` в общую папку `bin/` рядом с бинарниками. Пути переопределяются переменными `AKINATOR_PYTHON` и `AKINATOR_ML_SCRIPT`.

**Цели и типы:**

| Цель | Тип | Линкует | Примечание |
|---|---|---|---|
| `gamecore` | `SHARED` | `Qt6::Core` | `GAMECORE_BUILD`, `AKINATOR_ML_SCRIPT_PATH`; POST_BUILD копирует `ml_hints.py` в `bin/`; `target_include_directories(gamecore PUBLIC include)` |
| `akinator_protocol` | `INTERFACE` | `Qt6::Core` | `target_include_directories(akinator_protocol INTERFACE .)` |
| `akinator-server` | EXE | `Qt6::Core`, `Qt6::Network`, `akinator_protocol`, `gamecore` | консольное |
| `akinator-client` | EXE | `Qt6::Widgets`, `Qt6::Network`, `akinator_protocol`, `gamecore` | standalone CMake для Qt Creator; на MSVC можно `WIN32_EXECUTABLE TRUE` |
| `tst_gamecore` | EXE | `gamecore`, `Qt6::Test` | — |
| `tst_protocol` | EXE | `akinator_protocol`, `Qt6::Core`, `Qt6::Test` | — |

**Подключение тестов и CTest:**
- В `tests/CMakeLists.txt`: `find_package(Qt6 REQUIRED COMPONENTS Test)`.
- `add_test(NAME tst_gamecore COMMAND tst_gamecore)` и `add_test(NAME tst_protocol COMMAND tst_protocol)`.
- Запуск: `ctest --test-dir build --output-on-failure`.
- Тесты находят `gamecore.dll` благодаря общей папке `bin/`.

**Packaging-цели:**
- `package-client` копирует клиент и runtime-библиотеку `gamecore` в `build/dist/client` (Python-воркер не копируется).
- `package-server` копирует сервер, `gamecore`, `ml_hints.py` и `requirements.txt` в `build/dist/server`.
- На Linux/macOS у клиента и сервера выставляется RPATH `$ORIGIN`/`@loader_path`, чтобы скопированная папка находила `gamecore` рядом с бинарником.
---

## 5. Публичный API `gamecore`

Файл `gamecore/include/gamecore/export.h` (экспорт символов):

```cpp
// export.h
#pragma once
#if defined(_WIN32)
  #if defined(GAMECORE_BUILD)
    #define GAMECORE_EXPORT __declspec(dllexport)
  #else
    #define GAMECORE_EXPORT __declspec(dllimport)
  #endif
#else
  #define GAMECORE_EXPORT __attribute__((visibility("default")))
#endif
```

Файл `gamecore/include/gamecore/gamecore.h` (только объявления):

```cpp
// gamecore.h
#pragma once
#include "gamecore/export.h"
#include <cstdint>
#include <string>
#include <vector>

namespace gamecore {

using CharacterId = int;   // 0..59
using QuestionId  = int;   // 0..138
using Answer      = int;   // -1 = нет, +1 = да
using CandidateSet = std::vector<CharacterId>; // отсортирован, без повторов

constexpr int kCharacterCount = 60;
constexpr int kQuestionCount  = 139;

// --- Данные (единственный источник — src/data.h) ---
GAMECORE_EXPORT const std::vector<std::string>& characterNames(); // 60 имён, UTF-8
GAMECORE_EXPORT const std::vector<std::string>& questionTexts();  // 139 текстов, UTF-8
// Матрица row-major: matrix[charId * kQuestionCount + questionId] ∈ {-1, +1}
GAMECORE_EXPORT const std::vector<std::int8_t>& matrix();

// --- Элементарные операции ---
GAMECORE_EXPORT int value(CharacterId character, QuestionId question);
// Оставить только тех кандидатов, у кого value == answer.
GAMECORE_EXPORT CandidateSet filterCandidates(const CandidateSet& candidates,
                                              QuestionId question, Answer answer);

// --- Подсказки (лучший вопрос) ---
// C++-обёртки: запрос уходит постоянному Python-воркеру gamecore/ml_hints.py.
struct HintResult {
    QuestionId question = -1; // -1, если информативного вопроса нет
    double     score    = 0.0;
};

// variance: максимум 4p(1-p) по оставшимся кандидатам.
GAMECORE_EXPORT HintResult hintVariance(const CandidateSet& candidates,
                                        const std::vector<QuestionId>& excluded);
// Random Forest: 100 деревьев, bootstrap + случайные подмножества признаков,
// важность по усреднённому падению Gini-примеси (фиксированный seed).
GAMECORE_EXPORT HintResult hintRandomForest(const CandidateSet& candidates,
                                            const std::vector<QuestionId>& excluded);
// CatBoost-стиль: 100 раундов градиентного бустинга с oblivious-деревьями,
// квадратичная аппроксимация logloss и L2-регуляризация (фиксированный seed).
GAMECORE_EXPORT HintResult hintCatBoost(const CandidateSet& candidates,
                                        const std::vector<QuestionId>& excluded);

} // namespace gamecore
```

**Контракты:**
- `value`, `filterCandidates`, подсказки **не бросают исключений**; входные
  индексы считаются валидными (валидация — на уровне сервера/протокола).
- `excluded` — уже заданные вопросы (исключаются из выбора; пустой вектор =
  ничего не исключено).
- Подсказки **детерминированы**: одинаковые входы → одинаковый `HintResult`.
- Подсказки требуют Python 3 + numpy; C++-обёртки запускают постоянный воркер
  лениво (один процесс на приложение), обмен — JSON-строки `init`/`hint`,
  таймаут запроса 20 с. При сбое воркер перезапускается; если он недоступен,
  возвращается `{question = -1, score = 0}` и исключение не пробрасывается.
- Если кандидатов < 2 или нет вопроса, который различает кандидатов и не входит
  в `excluded`, вернуть `{question = -1, score = 0}`.

**5.1. Протокол `gamecore` ↔ `ml_hints.py`**

- Запуск: `python3 -u ml_hints.py --serve` на Unix (`python -u ...` на Windows).
- Инициализация (один раз): `{"cmd":"init","matrix":[8340 значений]}` → `{"ok":true}`.
- Запрос подсказки: `{"cmd":"hint","kind":"variance|randomforest|catboost","candidates":[...],"excluded":[...]}` → `{"question":q,"score":s}`.
- Ошибка Python → `{"error":"..."}`; C++-обёртка возвращает `{question=-1, score=0}`.
- Параметры алгоритмов: Random Forest — 100 деревьев, глубина 4, min leaf 2, `mtry=max(2,√F)`, seed `0x5EED1234`; CatBoost-стиль — 100 раундов, глубина 3, min leaf 2, learning rate 0.15, L2=1.0, логистическая функция потерь; variance — точный максимум `4p(1-p)`.

---

## 6. Спецификация сетевого протокола (текстовые команды)

Каждое сообщение — одна строка UTF-8; команда и аргументы разделены пробелами,
строка завершена `\n`. Регистр команд — верхний. Индексы: `characterId` 0..59,
`questionId` 0..138.

### 6.1. Сообщения клиент → сервер

| Команда | Аргументы | Описание |
|---|---|---|
| `SECRET` | `<characterId>` | Загадать персонажа (один раз до старта) |
| `ASK` | `<questionId>` | Задать вопрос о персонаже противника |
| `HINT` | `<variance\|randomforest\|catboost>` | Использовать подсказку |
| `TABLE` | — | Запросить свой текущий список кандидатов |
| `REMOVE` | `<characterId> ...` | Удалить персонажей из списка вручную |
| `GUESS` | `<characterId>` | Угадать персонажа противника |
| `REMATCH` | — | Запрос новой партии (после `OVER`) |
| `LEAVE` | — | Вежливое отключение |

Примеры:
```
SECRET 1
ASK 0
HINT variance
TABLE
REMOVE 3 17 42
GUESS 24
REMATCH
```

### 6.2. Сообщения сервер → клиент

| Команда | Аргументы | Описание |
|---|---|---|
| `WELCOME` | `<player>` (1\|2) | Ваш номер в лобби |
| `START` | — | Оба загадали, игра начинается |
| `TURN` | `<player>` (1\|2) | Чей сейчас ход |
| `ANSWER` | `<questionId> <yes\|no> <remaining> [<hint> <metric>]` | Итог вопроса/подсказки (спрашивающему) |
| `HINTS` | `<variance> <randomforest> <catboost>` | Остаток подсказок (при старте и после подсказки) |
| `OPPONENT` | `<questionId> <yes\|no>` | Инфо противнику: что у него спросили |
| `CANDIDATES` | `<characterId> ...` | Текущий список кандидатов (ответ на `TABLE`) |
| `REMOVED` | `<remaining>` | Подтверждение ручного удаления |
| `WRONG` | — | Неверная догадка (при верной сразу `OVER`) |
| `OVER` | `<winner> <characterId>` | Конец игры; имя персонажа клиент берёт из `gamecore` |
| `ERROR` | `<text>` | Ошибка; текст — остаток строки |
| `OPPONENT_LEFT` | `<text>` | Противник отключился |

Примеры:
```
WELCOME 1
START
TURN 1
HINTS 1 1 1
ANSWER 0 yes 31
ANSWER 12 no 9 variance 0.960
OPPONENT 12 no
CANDIDATES 0 5 8 11
REMOVED 4
WRONG
OVER 2 24
ERROR Сейчас не ваш ход
OPPONENT_LEFT Противник отключился
```

### 6.3. Конечный автомат

**Сервер (стейтмашина матча):**
```
WAITING_SECRETS ──(SECRET от обоих)──► IN_GAME (turn=1)
IN_GAME:
   ASK/HINT ──► (ответ из секрета противника) ──► filterCandidates
            ──► ANSWER спрашивающему + OPPONENT противнику ──► TURN
   GUESS ──► верно? ──► OVER
             неверно? ──► WRONG ──► TURN
   TABLE / REMOVE ──► (без смены хода)
MATCHOVER ──(оба прислали REMATCH)──► WAITING_SECRETS (сброс секретов и кандидатов)
Любое состояние ──(disconnect любого игрока)──► OPPONENT_LEFT другому ──► LOBBY (освободить оба слота)
```
- `ASK`/`HINT`/`GUESS` вне своего хода → `ERROR` «Сейчас не ваш ход».
- `ASK` с уже заданным вопросом или `HINT` с израсходованным видом → `ERROR`.
- Вторая подряд подсказка (без обычного вопроса/догадки между ними) → `ERROR`
  «Подсказку нельзя использовать два хода подряд»; подсказка при этом не
  тратится и ход не меняется.
- `SECRET` повторно → `ERROR`.
- `GUESS` с несуществующим `characterId` или персонажем не из кандидатов → `ERROR`.

**Клиент:**
```
DISCONNECTED ──Connect──► CONNECTING ──(WELCOME)──► WAITING_OPPONENT(лобби)
WAITING_OPPONENT ──(START + TURN)──► MY_TURN | OPPONENT_TURN
MY_TURN: доступны «вопрос/подсказка/таблица/угадать» ──(ASK/HINT)──► WAIT_RESULT
WAIT_RESULT ──(ANSWER)──► (OVER?) иначе OPPONENT_TURN
OPPONENT_TURN: интерфейс хода заблокирован; оповещается через OPPONENT
Любое состояние ──(OVER)──► MATCHOVER
Любое состояние ──(OPPONENT_LEFT | ERROR)──► показать сообщение ──► DISCONNECTED/WAITING
```
---

## 7. Поведение сервера

- **Запуск:** читает env (раздел 9), создаёт `QTcpServer`, `listen(QHostAddress::Any, port)`, лог в stdout.
- **Лобби на 2 игрока:** 2 слота `QTcpSocket*`. При подключении: слот 1 →
  `welcome(player=1)`; слот 2 → `welcome(player=2)`; оба заняты →
  `error("Сервер занят")` + `disconnectFromHost()` (третий отклоняется).
- **Секреты:** хранит `secret[player]` (CharacterId или «не задан»). `SECRET`
  принимает только в WAITING_SECRETS; когда оба заданы → `START` + `TURN 1` +
  `HINTS` обоим.
- **Кандидаты:** хранит `candidates[player]` (изначально все 60). После каждого
  `ASK`/`HINT` игрока сервер берёт `value(secret[opponent], questionId)`,
  формирует `ANSWER` ("yes"/"no"), вызывает
  `gamecore::filterCandidates(candidates[player], questionId, answer)` и обновляет
  счётчик `remaining`.
  > **Принятое в реализации поведение (согласовано с заказчиком):** фильтрация
  > автоматическая для **любого** вопроса — и обычного `ASK`, и `HINT`.
  > Ответ авторитетный, поэтому сервер сам убирает неподходящих кандидатов и
  > присылает уже сокращённый `remaining`; клиенту не нужно чистить таблицу
  > руками. Ручной `REMOVE` остаётся как дополнительный инструмент (подстраховка,
  > удаление «на глаз»), но не обязателен для игры. Подсказка дополнительно
  > выбирает вопрос по текущему набору кандидатов.
- **Ответ — авторитетный:** значение берётся из таблицы секрета противника
  (детерминировано), противник вручную не отвечает; противник получает
  `OPPONENT` (инфо-лог).
- **Подсказки:** у каждого игрока три счётчика по 1. `HINT <kind>` → если остаток
  0 → `ERROR`; если **прошлым ходом того же игрока была подсказка** →
  `ERROR` «Подсказку нельзя использовать два хода подряд», то есть между двумя
  подсказками нужен обычный вопрос или догадка. Иначе вызвать соответствующую
  `gamecore::hint*` на `candidates[player]` с `excluded` = уже заданным вопросам;
  C++-обёртка синхронно спрашивает Python-воркер `ml_hints.py` (JSON-lines) и
  возвращает его `question/score`. Если `question == -1` → `ERROR` «Нет
  информативного вопроса» (подсказка не тратится). Иначе вычесть подсказку,
  применить вопрос как обычный (ответ+фильтр), вернуть `ANSWER` с видом подсказки
  и metric, затем `HINTS`.
- **Угадывание:** `GUESS <characterId>` → сравнить с `secret[opponent]`. Верно →
  `OVER <winner> <characterId>` обоим. Неверно → `WRONG` угадавшему + смена хода.
- **Валидация:** `questionId` вне 0..138, `characterId` вне 0..59, не тот ход,
  повторный вопрос → `ERROR`, соединение не рвать.
- **Отключения:** `disconnected` в любом состоянии → другому
  `OPPONENT_LEFT`, закрыть оба сокета, вернуть лобби к исходному
  (оба слота свободны, секреты/кандидаты сброшены).
- **Повтор (rematch):** в MATCHOVER запомнить, кто прислал `REMATCH`; когда оба —
  сбросить секреты/кандидаты/подсказки и перейти в WAITING_SECRETS (перезагадывание).

---

## 8. Поведение GUI (клиент)

**Экран «Подключение»:**
- `QLineEdit hostField` (из env, раздел 9), `QSpinBox portField` (1–65535, из env),
  `QPushButton connectButton`.

**Экран «Выбор персонажа» (после `WELCOME`, состояние WAITING_OPPONENT):**
- `QLineEdit searchField` + `QListWidget characterList` (60 имён из
  `gamecore::characterNames()`, с фильтром по строке поиска).
- `QPushButton confirmSecretButton` — шлёт `SECRET`. После выбора кнопка
  блокируется, статус «Ожидание второго игрока…».

**Экран «Игра» (после `START`):**
- `QLabel mySecretLabel` — «Ваш персонаж: N»: напоминает игроку его собственный
  секрет. Сервер свой секрет обратно не присылает, поэтому клиент запоминает
  выбор при `SECRET` и держит подпись всю партию (сбрасывается при новом
  выборе после `WELCOME`/реванша).
- `QLabel statusLabel` — «Ваш ход» / «Ход соперника» / «Ожидание ответа…».
- `QLabel candidatesLabel` — «Кандидатов: N».
- `QLabel hintsLabel` — остаток подсказок (или отдельные кнопки с цифрами).
- `QPlainTextEdit logText` (read-only) — история: «Вы спросили … → Да (осталось
  31)», «Соперник спросил … → Нет».
- Кнопки действия (активны только в MY_TURN):
  - `QPushButton askButton` («Задать вопрос») → диалог со списком 139 вопросов
    (`gamecore::questionTexts()`) + поиск; исключает уже заданные.
  - `QPushButton hintVarianceButton`, `hintCatBoostButton`, `hintRandomForestButton`
    («Подсказка: …») — блокируются при остатке 0 **и** пока действует запрет двух
    подсказок подряд (после подсказки до обычного вопроса/догадки).
  - `QPushButton tableButton` («Открыть таблицу») → диалог со списком текущих
    (уже отфильтрованных сервером) кандидатов и кнопкой «Удалить выбранных»
    (шлёт `REMOVE`) — для ручной подстраховки.
  - `QPushButton guessButton` («Угадать») → диалог-выбор из текущих кандидатов.

**Состояния и блокировки:**

| Состояние | host/port/connect | Выбор персонажа | Кнопки хода | Таблица/Угадать |
|---|---|---|---|---|
| DISCONNECTED | активны | скрыт | скрыты | скрыты |
| CONNECTING | заблокированы | скрыт | скрыты | скрыты |
| WAITING_OPPONENT | заблокированы | активен | скрыты | скрыты |
| MY_TURN | заблокированы | скрыт | активны | активны |
| WAIT_RESULT | заблокированы | скрыт | заблокированы | заблокированы |
| OPPONENT_TURN | заблокированы | скрыт | заблокированы | скрыты |
| MATCHOVER | заблокированы | скрыт | скрыты | скрыты |

- `ANSWER` → обновить `logText`, `candidatesLabel`; если пришёл вид подсказки —
  обновить `hintsLabel`.
- `OVER` → показать победителя и загаданного персонажа; кнопка «Реванш»
  (шлёт `REMATCH`) и «Выход».
- `OPPONENT_LEFT` / `ERROR` → `statusLabel`, разблокировать экран подключения.

---

## 9. Переменные окружения

| Переменная | Кто читает | Дефолт | Назначение |
|---|---|---|---|
| `GAME_SERVER_HOST` | клиент | `127.0.0.1` | адрес сервера (предзаполняет поле host) |
| `GAME_SERVER_PORT` | клиент и сервер | `9999` | порт (клиент — подключение, сервер — listen) |
| `AKINATOR_PYTHON` | gamecore | `python3` (Unix) / `python` (Windows) | интерпретатор Python для ML-воркера подсказок |
| `AKINATOR_ML_SCRIPT` | gamecore | `build/bin/ml_hints.py` | явный путь к `ml_hints.py` (иначе ищется рядом с бинарником / в исходниках) |

- **Сервер** слушает на `0.0.0.0` (Any) и порту из `GAME_SERVER_PORT`.
- **Клиент** использует значения полей GUI в момент «Подключиться»; поля
  предзаполняются из env (или дефолтом).
- **Некорректный порт** (не число / вне `[1,65535]`) → дефолт + предупреждение в
  stderr (сервер) / `statusLabel` (клиент).
- **Пустой host** → дефолт `127.0.0.1`.
- Приоритет (клиент): **значение в поле GUI** > env > дефолт.
---

## 10. Unit-тесты

### `tst_gamecore.cpp`

| # | Тест | Вход | Ожидаемый результат |
|---|---|---|---|
| 1 | `data_integrity` | обращение к данным | `characterNames().size()==60`, `questionTexts().size()==139`, `matrix().size()==60*139`, все значения ∈ {−1,+1} |
| 2 | `value_roundtrip` | `value(c,q)` для нескольких (c,q) | равно `matrix()[c*139+q]` |
| 3 | `filter_yes` | `filterCandidates(all60, q, +1)` | результат = вручную вычисленное по `matrix()` подмножество с `value==+1` |
| 4 | `filter_no` | `filterCandidates(all60, q, -1)` | подмножество с `value==-1` |
| 5 | `filter_empty` | пустой `candidates` | пустой результат |
| 6 | `filter_preservesOrder` | несортированный вход | результат отсортирован, без повторов |
| 7 | `variance_max` | `hintVariance(all60, {})` | `question` == вопросу с максимальным `4p(1-p)` (brute-force сравнение), `score>0` |
| 8 | `variance_excluded` | `hintVariance(c, excluded={best})` | возвращённый вопрос ∉ `excluded` |
| 9 | `variance_noInfo` | кандидаты с одинаковыми строками | `question==-1`, `score==0` |
| 10 | `rf_valid` | `hintRandomForest(all60, {})` | `0 <= question < 139`, `score > 0` |
| 11 | `rf_deterministic` | два вызова с одинаковым входом | идентичные `HintResult` |
| 12 | `catboost_valid` | `hintCatBoost(all60, {})` | `0 <= question < 139`, `score > 0` |
| 13 | `catboost_deterministic` | два вызова с одинаковым входом | идентичные `HintResult` |
| 14 | `hints_noInfo` | кандидаты с одинаковыми строками | все три подсказки возвращают `question==-1` |

> Тесты подсказок (7–14) требуют `python3` и `numpy`: `gamecore` при первом
> вызове `hint*` поднимает Python-воркер. Если Python/numpy недоступны, тесты
> подсказок упадут; для CI зависимость обязательна (`gamecore/requirements.txt`).

### `tst_protocol.cpp`

| # | Тест | Вход | Ожидаемый результат |
|---|---|---|---|
| 15 | `parseLine` | `"ASK 12"`, `"  TABLE  "`, `""` | `["ASK","12"]`, `["TABLE"]`, пустой список |
| 16 | `readInt` | `"REMOVE 3 17 42"` | 3 читается; `"REMOVE"` и отсутствующий аргумент — нет |
| 17 | `makeLine` | команды `START`, `ANSWER`, `ERROR` с текстом | строки с `\n`, аргументы через пробел |
| 18 | `takeLines` | буфер с 3 строками, CRLF и пустой строкой | 3 строки, буфер пуст |
| 19 | `partialLine` | `"ASK 5"` без `\n`, затем с `\n` | сначала пусто, после `\n` — одна строка |
---

## 11. Пошаговый план (11 задач)

После каждой задачи проект собирается и не ломает предыдущее.

**Задача 1. Каркас и CMake**
- Цель: пустой проект собирается целиком.
- Файлы: корневой `CMakeLists.txt`, `.gitignore`, пустые `gamecore/`, `common/`, `server/`, `client/`, `tests/` со своими `CMakeLists.txt` и `main.cpp` (печатают и выходят).
- Готово, если: `cmake -S . -B build` и `cmake --build build` без ошибок; exe в `build/bin`.
- Проверка: `cmake -S . -B build && cmake --build build`.

**Задача 2. Данные gamecore (единый источник + доступ)**
- Цель: вшитые данные 60×139 и функции доступа к ним.
- Файлы: `gamecore/src/data.h`, `gamecore/include/gamecore/export.h`, `gamecore/include/gamecore/gamecore.h`, `gamecore/src/gamecore.cpp` (частично), правка `gamecore/CMakeLists.txt`.
- Готово, если: `data.h` содержит имена/тексты/матрицу, библиотека собирается и экспортирует `characterNames/questionTexts/matrix/value`.
- Проверка: `cmake --build build`.

**Задача 3. gamecore: filterCandidates + hintVariance**
- Цель: базовая фильтрация в C++ и подсказка по дисперсии через Python-воркер.
- Файлы: `gamecore/src/gamecore.cpp`, `gamecore/include/gamecore/gamecore.h`, `gamecore/ml_hints.py`, `gamecore/requirements.txt`, правка `gamecore/CMakeLists.txt`.
- Готово, если: `filterCandidates` работает по контракту раздела 5, а `hintVariance` делегируется Python-воркеру и возвращает тот же вопрос, что brute-force.
- Проверка: `cmake --build build` + `python3 -c "import numpy"`.

**Задача 4. gamecore: hintRandomForest + hintCatBoost**
- Цель: две настоящие ML-подсказки в `ml_hints.py` (детерминированные).
- Файлы: `gamecore/ml_hints.py`, `gamecore/src/gamecore.cpp` (обёртки), `gamecore/requirements.txt`.
- Готово, если: Random Forest обучает 100 деревьев с bootstrap/mtry/Gini, CatBoost-стиль — 100 раундов oblivious-бустинга; обе функции возвращают валидный вопрос / `-1`, детерминированы.
- Проверка: `cmake --build build`.

**Задача 5. Тесты gamecore + ctest**
- Цель: покрыть данные/filter/подсказки тестами 1–14.
- Файлы: `tests/tst_gamecore.cpp`, правка `tests/CMakeLists.txt`.
- Готово, если: `ctest` зелёный (нужны Python 3 + numpy).
- Проверка: `cmake --build build && ctest --test-dir build --output-on-failure`.

**Задача 6. akinator_protocol (INTERFACE) + тесты**
- Цель: разбор и сборка текстовых команд раздела 6.
- Файлы: `common/protocol.h`, правка `common/CMakeLists.txt`, `tests/tst_protocol.cpp`, правка `tests/CMakeLists.txt`.
- Готово, если: тесты 15–19 зелёные.
- Проверка: `cmake --build build && ctest --test-dir build --output-on-failure`.

**Задача 7. Сервер: listen + env + лобби + SECRET**
- Цель: принимать 2 клиентов, собирать секреты, отклонять третьего.
- Файлы: `server/server.h`, `server/server.cpp`, `server/main.cpp`, правка `server/CMakeLists.txt`.
- Готово, если: сервер слушает порт из `GAME_SERVER_PORT`; два клиента получают `WELCOME`, после двух `SECRET` — `START`; третий получает `ERROR`.
- Проверка: запуск сервера + ручная проверка `WELCOME/START/ERROR` через `nc`/telnet (слать текстовые команды).

**Задача 8. Сервер: вопросы/подсказки/кандидаты/угадывание/победа + отключения**
- Цель: полный игровой цикл по разделу 7.
- Файлы: `server/server.cpp`, `server/server.h`.
- Готово, если: `ASK`/`HINT` → `ANSWER`+`OPPONENT`+смена хода; `GUESS` → `WRONG`/`OVER`; `TABLE`/`REMOVE`; отключение → `OPPONENT_LEFT`; `REMATCH` → сброс.
- Проверка: ручной тест двумя `nc`-сессиями по протоколу раздела 6 (полный матч до угадывания).

**Задача 9. Клиент: GUI + env + выбор персонажа**
- Цель: окна подключения и выбора секрета; чтение env.
- Файлы: `client/main.cpp`, `client/mainwindow.h/.cpp`, `client/connection.h/.cpp`, правка `client/CMakeLists.txt`.
- Готово, если: окно открывается, host/port из env, список 60 персонажей с поиском, `SECRET` отправляется.
- Проверка: `cmake --build build`, запуск `akinator-client` против запущенного сервера.

**Задача 10. Клиент: полный игровой цикл**
- Цель: вопросы/подсказки/таблица/угадать/матч по разделу 8.
- Файлы: `client/mainwindow.cpp`, `client/connection.cpp`.
- Готово, если: два GUI-клиента играют полный матч на localhost; состояния и блокировки как в разделе 8.
- Проверка: сервер + 2 клиента локально.

**Задача 11. Интеграция, README, финальный прогон**
- Цель: документация и сквозная проверка.
- Файлы: `README.md`, финальные правки.
- Готово, если: полный цикл «сборка → ctest → сервер → 2 клиента» по README; чек-лист раздела 12 выполним.
- Проверка: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure` + локальный матч.
---

## 12. Чек-лист демонстрации

1. `cmake -S . -B build && cmake --build build` — собирается на **Windows** и **Linux/macOS**.
2. `ctest --test-dir build --output-on-failure` — все тесты зелёные (показать вывод).
3. Показать, что `gamecore` — **SHARED**-библиотека (`.dll`/`.so`), реально используемая сервером/клиентом/тестами (там живут `filterCandidates` и C++-обёртки подсказок), а сами подсказки считает Python-воркер `ml_hints.py` (Random Forest / CatBoost / variance).
4. Показать `gamecore/src/data.h` → единственный источник данных 60×139 (без CSV/генератора).
5. Проверить `python3 -c "import numpy"`, запустить **сервер**: `GAME_SERVER_PORT=9999 ./akinator-server` (лог listen).
6. Порт доступен извне (фаервол/проброс).
7. **Windows**: `GAME_SERVER_HOST=<ZeroTier_IP> GAME_SERVER_PORT=9999 akinator-client`.
8. **Linux/macOS**: второй клиент с тем же адресом.
9. Оба выбирают персонажей из 60 → `START` → ход 1; в игровой панели у каждого видна плашка «Ваш персонаж: имя» с его собственным секретом.
10. Продемонстрировать: обычный вопрос → `ANSWER` с уменьшившимся `remaining` (кандидаты отфильтрованы автоматически); «Открыть таблицу» + ручное удаление → `REMOVED` с новым `remaining`; **3 подсказки** (дисперсия, Random Forest, CatBoost) → лучший вопрос + автофильтрация; **вторая подсказка подряд** → `ERROR` «Подсказку нельзя использовать два хода подряд»; угадывание → `OVER`.
11. Показать **env**: без переменных — дефолты; с `GAME_SERVER_HOST/PORT` — подключение к заданному адресу.
12. (Опционально) разрыв: закрыть клиент → второй видит «Противник отключился».
13. Показать отказ третьему игроку (`ERROR` «Сервер занят»).

---

## 13. Структура README

1. **Название и суть** — «Бой Акинаторов», 2 игрока, авторитетный сервер + 2 GUI-клиента.
2. **Требования** — Qt 6.5+, CMake ≥ 3.16, компилятор C++17, Python 3.10+ и numpy на сервере для подсказок.
3. **Сборка** — `cmake -S . -B build`, `cmake --build build`.
4. **Данные** — `gamecore/src/data.h` (единственный источник, без CSV/генератора).
5. **Тесты** — `ctest --test-dir build --output-on-failure`.
6. **Запуск сервера** — команда + env `GAME_SERVER_PORT`.
7. **Запуск клиента** — команда + env `GAME_SERVER_HOST`/`GAME_SERVER_PORT` + поля GUI.
8. **Как играть** — правила дуэли, вопросы, 3 подсказки, таблица кандидатов, угадывание.
9. **Сетевой доступ** — ZeroTier; P2P не используется.
10. **Архитектура** — модули (`gamecore`, `akinator_protocol`, `server`, `client`, `tests`).
11. **Протокол** — ссылка на раздел 6.
12. **Демо-чек-лист** — копия раздела 12.
13. **Авторы** (до 2 человек).

---

## 14. Чего делать НЕЛЬЗЯ

- **Не менять архитектуру** (авторитетный TCP-сервер + 2 GUI-клиента, не P2P).
- **Не добавлять** БД, файловое хранилище, auth, статистику.
- **Не реализовывать** NAT traversal, UPnP, STUN/TURN, peer-to-peer, relay.
- **Не подключать** сторонние C++-библиотеки (Boost, nlohmann/json, libcatboost, сокеты не из Qt) — только Qt6 + STL.
- **Не добавлять** лишние Qt-модули (QML, Multimedia, WebEngine) — только Core/Network/Widgets/Test.
- **Python разрешён только** для ML-подсказок (`gamecore/ml_hints.py`: Python 3 + numpy). Не использовать sklearn/libcatboost и не генерировать данные — данные уже в `gamecore/src/data.h`.
- **Не менять** формат данных (`1`=да, `-1`=нет) и количество персонажей/вопросов (60/139).
- **Не менять** публичный API `gamecore` (имена, сигнатуры, семантику `HintResult`).
- **Не менять** имена env-переменных и дефолты.
- **Не разрешать** клиенту считать ответ/победителя — только сервер.
- **Не поддерживать** больше 2 игроков и несколько одновременных матчей.
- **Не генерировать** весь проект одной задачей — строго по разделу 11.
- **Не удалять/игнорировать** тесты и `enable_testing()`.

---

## 15. Итоговая инструкция по выполнению

Работай по разделу 11 строго последовательно. Начинай с **Задачи 1** сразу, без
дополнительных вопросов. Перед тестами подсказок установи Python-зависимости:

```bash
python3 -m pip install -r gamecore/requirements.txt
```

Когда все 11 задач выполнены, ещё раз прогони:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

и убедись, что README.md содержит все пункты раздела 13. На этом работа завершена.
