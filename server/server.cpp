#include "server.h"

#include "protocol.h"

#include <QDebug>

#include <algorithm>
#include <numeric>

namespace {
constexpr int kHintsPerKind = 1;
}

Server::Server(QObject* parent)
    : QObject(parent)
{
    connect(&server_, &QTcpServer::newConnection, this, &Server::onNewConnection);
}

Server::~Server() = default;

bool Server::start(quint16 port, const QHostAddress& address)
{
    if (!server_.listen(address, port)) {
        qCritical().noquote() << QStringLiteral("Не удалось слушать адрес %1:%2: %3")
                                     .arg(address.toString())
                                     .arg(port)
                                     .arg(server_.errorString());
        return false;
    }
    qInfo().noquote() << QStringLiteral("Сервер слушает %1:%2")
                             .arg(server_.serverAddress().toString())
                             .arg(server_.serverPort());
    return true;
}

int Server::slotOf(const QTcpSocket* socket) const
{
    if (!socket) {
        return -1;
    }
    for (int i = 0; i < kPlayerCount; ++i) {
        if (players_[i].socket == socket) {
            return i;
        }
    }
    return -1;
}

int Server::opponentSlot(int slot)
{
    return slot == 0 ? 1 : 0;
}

bool Server::bothConnected() const
{
    return players_[0].socket != nullptr && players_[1].socket != nullptr;
}

void Server::send(int slot, const QStringList& parts)
{
    if (slot < 0 || slot >= kPlayerCount) {
        return;
    }
    Player& player = players_[slot];
    if (!player.socket) {
        return;
    }
    player.socket->write(proto::makeLine(parts));
    player.socket->flush();
}

void Server::broadcast(const QStringList& parts)
{
    for (int i = 0; i < kPlayerCount; ++i) {
        send(i, parts);
    }
}

void Server::sendError(int slot, const QString& text)
{
    qWarning().noquote() << QStringLiteral("Ошибка игроку %1: %2")
                                .arg(slot + 1)
                                .arg(text);
    send(slot, {QStringLiteral("ERROR"), text});
}

void Server::sendHintStatus(int slot)
{
    const Player& player = players_[slot];
    send(slot, {QStringLiteral("HINTS"),
                QString::number(player.hintsVariance),
                QString::number(player.hintsRandomForest),
                QString::number(player.hintsCatBoost)});
}

void Server::beginWaitingSecrets()
{
    for (Player& player : players_) {
        player.candidates.clear();
        player.asked.clear();
        player.hintsVariance = 0;
        player.hintsRandomForest = 0;
        player.hintsCatBoost = 0;
        player.lastTurnWasHint = false;
        player.wantsRematch = false;
    }
    turn_ = 1;
    state_ = State::WaitingSecrets;
    qInfo().noquote() << QStringLiteral("Ожидание секретов от обоих игроков");
}

void Server::clearSecrets()
{
    for (Player& player : players_) {
        player.hasSecret = false;
        player.secret = -1;
    }
}

void Server::maybeStartMatch()
{
    if (state_ != State::WaitingSecrets) {
        return;
    }
    if (players_[0].hasSecret && players_[1].hasSecret) {
        startMatch();
    }
}

void Server::startMatch()
{
    state_ = State::InGame;
    turn_ = 1;

    gamecore::CandidateSet all(gamecore::kCharacterCount);
    std::iota(all.begin(), all.end(), 0);

    for (Player& player : players_) {
        player.candidates = all;
        player.asked.clear();
        player.hintsVariance = kHintsPerKind;
        player.hintsRandomForest = kHintsPerKind;
        player.hintsCatBoost = kHintsPerKind;
        player.lastTurnWasHint = false;
        player.wantsRematch = false;
    }

    for (int slot = 0; slot < kPlayerCount; ++slot) {
        send(slot, {QStringLiteral("START")});
        send(slot, {QStringLiteral("TURN"), QString::number(turn_)});
        sendHintStatus(slot);
    }

    qInfo().noquote() << QStringLiteral("Игра началась: ход игрока %1 (кандидатов у каждого: %2)")
                             .arg(turn_)
                             .arg(players_[0].candidates.size());
}
void Server::onNewConnection()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();

        int slot = -1;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (!players_[i].socket) {
                slot = i;
                break;
            }
        }

        if (slot < 0) {
            socket->write(proto::makeLine(
                {QStringLiteral("ERROR"), QStringLiteral("Сервер занят")}));
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            qWarning().noquote() << QStringLiteral("Третье подключение отклонено (сервер занят)");
            continue;
        }

        Player& player = players_[slot];
        player = Player{};
        player.socket = socket;
        player.id = slot + 1;

        connect(socket, &QTcpSocket::readyRead, this, &Server::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &Server::onDisconnected);

        send(slot, {QStringLiteral("WELCOME"), QString::number(player.id)});
        qInfo().noquote() << QStringLiteral("Игрок %1 подключился (%2:%3)")
                                 .arg(player.id)
                                 .arg(socket->peerAddress().toString())
                                 .arg(socket->peerPort());

        if (bothConnected() && state_ == State::Lobby) {
            beginWaitingSecrets();
            qInfo().noquote() << QStringLiteral("Оба игрока на месте");
        }
    }
}

void Server::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }
    int slot = slotOf(socket);
    if (slot < 0) {
        socket->readAll();
        return;
    }

    players_[slot].buffer.append(socket->readAll());

    if (players_[slot].buffer.size() > proto::kMaxLineBytes) {
        sendError(slot, QStringLiteral("Слишком длинное сообщение"));
        players_[slot].buffer.clear();
        socket->disconnectFromHost();
        return;
    }

    const QStringList lines = proto::takeLines(&players_[slot].buffer);
    for (const QString& line : lines) {
        const int current = slotOf(socket);
        if (current < 0) {
            return;
        }

        const QStringList parts = proto::parseLine(line);
        if (!parts.isEmpty()) {
            handleMessage(current, parts);
        }
    }
}

void Server::onDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    const int slot = slotOf(socket);
    if (slot < 0) {
        if (socket) {
            socket->deleteLater();
        }
        return;
    }
    qInfo().noquote() << QStringLiteral("Игрок %1 отключился").arg(players_[slot].id);
    dropLobby(slot);
}

void Server::dropLobby(int closingSlot)
{
    const int other = opponentSlot(closingSlot);
    QTcpSocket* dying = players_[closingSlot].socket;
    QTcpSocket* survivor = players_[other].socket;

    if (survivor) {
        send(other, {QStringLiteral("OPPONENT_LEFT"),
                     QStringLiteral("Противник отключился")});
    }

    state_ = State::Lobby;
    for (Player& player : players_) {
        player = Player{};
    }
    turn_ = 1;

    if (dying) {
        QObject::disconnect(dying, nullptr, this, nullptr);
        dying->disconnectFromHost();
        dying->deleteLater();
    }
    if (survivor) {
        QObject::disconnect(survivor, nullptr, this, nullptr);
        survivor->disconnectFromHost();
        survivor->deleteLater();
    }

    qInfo().noquote() << QStringLiteral("Лобби сброшено, сервер ждёт новых игроков");
}

void Server::handleMessage(int slot, const QStringList& parts)
{
    if (parts.isEmpty()) {
        return;
    }

    const QString command = parts[0];
    if (command == QLatin1String("SECRET")) {
        int characterId = -1;
        if (!proto::readInt(parts, 1, &characterId)) {
            sendError(slot, QStringLiteral("Команда SECRET требует characterId"));
            return;
        }
        handleSetSecret(slot, characterId);
    } else if (command == QLatin1String("ASK")) {
        int questionId = -1;
        if (!proto::readInt(parts, 1, &questionId)) {
            sendError(slot, QStringLiteral("Команда ASK требует questionId"));
            return;
        }
        handleQuestion(slot, questionId);
    } else if (command == QLatin1String("HINT")) {
        if (parts.size() < 2) {
            sendError(slot, QStringLiteral("Команда HINT требует вид подсказки"));
            return;
        }
        handleHint(slot, parts[1]);
    } else if (command == QLatin1String("TABLE")) {
        handleGetCandidates(slot);
    } else if (command == QLatin1String("REMOVE")) {
        handleRemove(slot, parts);
    } else if (command == QLatin1String("GUESS")) {
        int characterId = -1;
        if (!proto::readInt(parts, 1, &characterId)) {
            sendError(slot, QStringLiteral("Команда GUESS требует characterId"));
            return;
        }
        handleGuess(slot, characterId);
    } else if (command == QLatin1String("REMATCH")) {
        handleRematch(slot);
    } else if (command == QLatin1String("LEAVE")) {
        handleLeave(slot);
    } else {
        sendError(slot, QStringLiteral("Неизвестная команда: %1").arg(command));
    }
}

bool Server::ensureInGame(int slot)
{
    if (slot < 0 || slot >= kPlayerCount || !players_[slot].socket) {
        return false;
    }
    if (state_ != State::InGame) {
        sendError(slot, QStringLiteral("Сейчас игра не идёт"));
        return false;
    }
    return true;
}

bool Server::ensureTurn(int slot)
{
    if (!ensureInGame(slot)) {
        return false;
    }
    if (turn_ != players_[slot].id) {
        sendError(slot, QStringLiteral("Сейчас не ваш ход"));
        return false;
    }
    return true;
}

bool Server::isAsked(int slot, int questionId) const
{
    const std::vector<int>& asked = players_[slot].asked;
    return std::find(asked.begin(), asked.end(), questionId) != asked.end();
}

void Server::handleSetSecret(int slot, int characterId)
{
    Player& player = players_[slot];

    if (state_ != State::WaitingSecrets && state_ != State::Lobby) {
        sendError(slot, QStringLiteral("Сейчас нельзя загадывать персонажа"));
        return;
    }
    if (player.hasSecret) {
        sendError(slot, QStringLiteral("Персонаж уже загадан"));
        return;
    }
    if (characterId < 0 || characterId >= gamecore::kCharacterCount) {
        sendError(slot, QStringLiteral("Недопустимый characterId"));
        return;
    }

    player.secret = characterId;
    player.hasSecret = true;
    qInfo().noquote() << QStringLiteral("Игрок %1 загадал персонажа #%2 (%3)")
                             .arg(player.id)
                             .arg(player.secret)
                             .arg(QString::fromStdString(
                                 gamecore::characterNames()[static_cast<std::size_t>(player.secret)]));

    maybeStartMatch();
}

void Server::handleQuestion(int slot, int questionId)
{
    if (!ensureTurn(slot)) {
        return;
    }
    if (questionId < 0 || questionId >= gamecore::kQuestionCount) {
        sendError(slot, QStringLiteral("Недопустимый questionId"));
        return;
    }
    if (isAsked(slot, questionId)) {
        sendError(slot, QStringLiteral("Этот вопрос уже задан"));
        return;
    }
    players_[slot].lastTurnWasHint = false;
    applyQuestion(slot, questionId, QString(), 0.0);
}

void Server::handleHint(int slot, const QString& kind)
{
    if (!ensureTurn(slot)) {
        return;
    }

    Player& player = players_[slot];

    int* left = nullptr;
    if (kind == QLatin1String("variance")) {
        left = &player.hintsVariance;
    } else if (kind == QLatin1String("randomforest")) {
        left = &player.hintsRandomForest;
    } else if (kind == QLatin1String("catboost")) {
        left = &player.hintsCatBoost;
    } else {
        sendError(slot, QStringLiteral("Неизвестная подсказка: %1").arg(kind));
        return;
    }

    if (*left <= 0) {
        sendError(slot, QStringLiteral("Подсказка «%1» уже использована").arg(kind));
        return;
    }
    if (player.lastTurnWasHint) {
        sendError(slot, QStringLiteral("Подсказку нельзя использовать два хода подряд — "
                                       "сначала задайте обычный вопрос или угадайте"));
        return;
    }

    gamecore::HintResult result;
    if (kind == QLatin1String("variance")) {
        result = gamecore::hintVariance(player.candidates, player.asked);
    } else if (kind == QLatin1String("randomforest")) {
        result = gamecore::hintRandomForest(player.candidates, player.asked);
    } else {
        result = gamecore::hintCatBoost(player.candidates, player.asked);
    }

    if (result.question < 0) {
        sendError(slot, QStringLiteral("Нет информативного вопроса"));
        return;
    }

    --(*left);
    player.lastTurnWasHint = true;

    qInfo().noquote() << QStringLiteral("Игрок %1 использует подсказку %2 -> вопрос #%3 (score %4)")
                             .arg(player.id)
                             .arg(kind)
                             .arg(result.question)
                             .arg(result.score);

    sendHintStatus(slot);
    applyQuestion(slot, result.question, kind, result.score);
}

void Server::applyQuestion(int slot, int questionId, const QString& hintKind, double metric)
{
    Player& player = players_[slot];
    const Player& opponent = players_[opponentSlot(slot)];

    const bool yes = gamecore::value(opponent.secret, questionId) > 0;
    const QString answer = yes ? QStringLiteral("yes") : QStringLiteral("no");

    player.asked.push_back(questionId);

    player.candidates =
        gamecore::filterCandidates(player.candidates, questionId, yes ? 1 : -1);

    QStringList response{QStringLiteral("ANSWER"),
                         QString::number(questionId),
                         answer,
                         QString::number(player.candidates.size())};
    if (!hintKind.isEmpty()) {
        response << hintKind << QString::number(metric, 'f', 3);
    }

    send(slot, response);
    send(opponentSlot(slot), {QStringLiteral("OPPONENT"),
                              QString::number(questionId),
                              answer});

    qInfo().noquote() << QStringLiteral("Игрок %1 спросил #%2 (%3) -> %4, %5: %6")
                             .arg(player.id)
                             .arg(questionId)
                             .arg(QString::fromStdString(
                                 gamecore::questionTexts()[static_cast<std::size_t>(questionId)]))
                             .arg(answer)
                             .arg(hintKind.isEmpty() ? QStringLiteral("таблица отфильтрована")
                                                     : QStringLiteral("подсказка отфильтровала"))
                             .arg(player.candidates.size());

    switchTurn();
}

void Server::switchTurn()
{
    turn_ = turn_ == 1 ? 2 : 1;
    broadcast({QStringLiteral("TURN"), QString::number(turn_)});
    qInfo().noquote() << QStringLiteral("Ход переходит к игроку %1").arg(turn_);
}

void Server::handleGetCandidates(int slot)
{
    if (!ensureInGame(slot)) {
        return;
    }
    const Player& player = players_[slot];

    QStringList response{QStringLiteral("CANDIDATES")};
    for (int id : player.candidates) {
        response << QString::number(id);
    }
    send(slot, response);

    qInfo().noquote() << QStringLiteral("Игрок %1 запросил кандидатов (%2)")
                             .arg(player.id)
                             .arg(player.candidates.size());
}

void Server::handleRemove(int slot, const QStringList& parts)
{
    if (!ensureInGame(slot)) {
        return;
    }

    Player& player = players_[slot];
    gamecore::CandidateSet& candidates = player.candidates;

    int removed = 0;
    for (int i = 1; i < parts.size(); ++i) {
        bool ok = false;
        const int id = parts.at(i).toInt(&ok);
        if (!ok) {
            continue;
        }
        const auto it = std::find(candidates.begin(), candidates.end(), id);
        if (it != candidates.end()) {
            candidates.erase(it);
            ++removed;
        }
    }

    send(slot, {QStringLiteral("REMOVED"), QString::number(candidates.size())});
    qInfo().noquote() << QStringLiteral("Игрок %1 удалил %2 кандидатов, осталось %3")
                             .arg(player.id)
                             .arg(removed)
                             .arg(candidates.size());
}

void Server::handleGuess(int slot, int characterId)
{
    if (!ensureTurn(slot)) {
        return;
    }

    Player& player = players_[slot];
    if (characterId < 0 || characterId >= gamecore::kCharacterCount) {
        sendError(slot, QStringLiteral("Недопустимый characterId"));
        return;
    }
    if (std::find(player.candidates.begin(), player.candidates.end(), characterId)
        == player.candidates.end()) {
        sendError(slot, QStringLiteral("Этого персонажа нет среди ваших кандидатов"));
        return;
    }

    const Player& opponent = players_[opponentSlot(slot)];
    player.lastTurnWasHint = false;
    const int secret = opponent.secret;
    const QString secretName =
        QString::fromStdString(gamecore::characterNames()[static_cast<std::size_t>(secret)]);

    if (characterId == secret) {
        state_ = State::MatchOver;
        for (Player& p : players_) {
            p.wantsRematch = false;
        }
        broadcast({QStringLiteral("OVER"),
                   QString::number(player.id),
                   QString::number(secret)});
        qInfo().noquote() << QStringLiteral("Игрок %1 угадал «%2» и победил")
                                 .arg(player.id)
                                 .arg(secretName);
        return;
    }

    const QString guessedName =
        QString::fromStdString(gamecore::characterNames()[static_cast<std::size_t>(characterId)]);
    send(slot, {QStringLiteral("WRONG")});
    qInfo().noquote() << QStringLiteral("Игрок %1 ошибся: «%2» вместо «%3»")
                             .arg(player.id)
                             .arg(guessedName)
                             .arg(secretName);
    switchTurn();
}

void Server::handleRematch(int slot)
{
    if (slot < 0 || slot >= kPlayerCount || !players_[slot].socket) {
        return;
    }
    if (state_ != State::MatchOver) {
        sendError(slot, QStringLiteral("Реванш возможен только после окончания партии"));
        return;
    }

    players_[slot].wantsRematch = true;
    qInfo().noquote() << QStringLiteral("Игрок %1 хочет реванш").arg(players_[slot].id);

    if (players_[0].wantsRematch && players_[1].wantsRematch) {
        clearSecrets();
        beginWaitingSecrets();
        qInfo().noquote() << QStringLiteral("Оба игрока готовы к реваншу");
    }
}

void Server::handleLeave(int slot)
{
    if (slot < 0 || slot >= kPlayerCount) {
        return;
    }
    qInfo().noquote() << QStringLiteral("Игрок %1 покинул игру").arg(slot + 1);
    dropLobby(slot);
}
