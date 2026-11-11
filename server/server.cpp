#include "server.h"

#include "protocol.h"

#include <QDebug>

#include <algorithm>
#include <numeric>

namespace {
constexpr int kHintsPerKind = 1;
int toInt(const QStringList& parts, int index)
{
    if (parts.size() <= index) {
        return -1;
    }
    bool ok = false;
    const int value = parts.at(index).toInt(&ok);
    return ok ? value : -1;
}
}

Server::Server(QObject* parent)
    : QObject(parent)
{
    connect(&server_, &QTcpServer::newConnection, this, &Server::onNewConnection);
}

bool Server::start(quint16 port, const QHostAddress& address)
{
    if (!server_.listen(address, port)) {
        qCritical().noquote() << QStringLiteral("Не удалось слушать адрес %1:%2: %3")
                                     .arg(address.toString())
                                     .arg(port)
                                     .arg(server_.errorString());
        return false;
    }
    return true;
}

int Server::slotOf(const QTcpSocket* socket) const
{
    for (int i = 0; socket && i < kPlayerCount; ++i) {
        if (players_[i].socket == socket) {
            return i;
        }
    }
    return -1;
}

void Server::send(int slot, const QStringList& parts)
{
    if (slot < 0 || slot >= kPlayerCount || !players_[slot].socket) {
        return;
    }
    players_[slot].socket->write(proto::makeLine(parts));
    players_[slot].socket->flush();
}

void Server::broadcast(const QStringList& parts)
{
    for (int slot = 0; slot < kPlayerCount; ++slot) {
        send(slot, parts);
    }
}

void Server::sendError(int slot, const QString& text)
{
    send(slot, {QStringLiteral("ERROR"), text});
}
void Server::sendHintStatus(int slot)
{
    const std::array<int, 3>& hints = players_[slot].hints;
    send(slot, {QStringLiteral("HINTS"), QString::number(hints[0]), QString::number(hints[1]),
                QString::number(hints[2])});
}

void Server::beginWaitingSecrets()
{
    for (Player& player : players_) {
        player.candidates.clear();
        player.asked.clear();
        player.hints = {};
        player.lastTurnWasHint = false;
        player.wantsRematch = false;
    }
    turn_ = 1;
    state_ = State::WaitingSecrets;
}

void Server::maybeStartMatch()
{
    if (state_ == State::WaitingSecrets && players_[0].hasSecret && players_[1].hasSecret) {
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
        player.hints = {kHintsPerKind, kHintsPerKind, kHintsPerKind};
        player.lastTurnWasHint = false;
        player.wantsRematch = false;
    }
    for (int slot = 0; slot < kPlayerCount; ++slot) {
        send(slot, {QStringLiteral("START")});
        send(slot, {QStringLiteral("TURN"), QString::number(turn_)});
        sendHintStatus(slot);
    }
}

void Server::onNewConnection()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();
        const int slot = !players_[0].socket ? 0 : !players_[1].socket ? 1 : -1;
        if (slot < 0) {
            socket->write(proto::makeLine(
                {QStringLiteral("ERROR"), QStringLiteral("Сервер занят")}));
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        Player& player = players_[slot];
        player = Player{};
        player.socket = socket;
        player.id = slot + 1;
        connect(socket, &QTcpSocket::readyRead, this, &Server::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &Server::onDisconnected);
        send(slot, {QStringLiteral("WELCOME"), QString::number(player.id)});
        if (bothConnected() && state_ == State::Lobby) {
            beginWaitingSecrets();
        }
    }
}

void Server::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    const int slot = slotOf(socket);
    if (slot < 0) {
        return;
    }
    QByteArray& buffer = players_[slot].buffer;
    buffer.append(socket->readAll());
    if (buffer.size() > proto::kMaxLineBytes) {
        sendError(slot, QStringLiteral("Слишком длинное сообщение"));
        buffer.clear();
        socket->disconnectFromHost();
        return;
    }
    for (const QString& line : proto::takeLines(&buffer)) {
        if (slotOf(socket) < 0) {
            return;
        }
        handleMessage(slot, proto::parseLine(line));
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
    dropLobby(slot);
}

void Server::dropLobby(int closingSlot)
{
    send(opponentSlot(closingSlot),
         {QStringLiteral("OPPONENT_LEFT"), QStringLiteral("Противник отключился")});
    state_ = State::Lobby;
    turn_ = 1;
    for (Player& player : players_) {
        if (player.socket) {
            QObject::disconnect(player.socket, nullptr, this, nullptr);
            player.socket->disconnectFromHost();
            player.socket->deleteLater();
        }
        player = Player{};
    }
}

void Server::handleMessage(int slot, const QStringList& parts)
{
    if (parts.isEmpty()) {
        return;
    }
    const QString& command = parts[0];
    if (command == QLatin1String("SECRET")) {
        handleSetSecret(slot, toInt(parts, 1));
    } else if (command == QLatin1String("ASK")) {
        handleQuestion(slot, toInt(parts, 1));
    } else if (command == QLatin1String("HINT")) {
        handleHint(slot, parts.value(1));
    } else if (command == QLatin1String("TABLE")) {
        handleGetCandidates(slot);
    } else if (command == QLatin1String("REMOVE")) {
        handleRemove(slot, parts);
    } else if (command == QLatin1String("GUESS")) {
        handleGuess(slot, toInt(parts, 1));
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
    if (state_ == State::InGame) {
        return true;
    }
    sendError(slot, QStringLiteral("Сейчас игра не идёт"));
    return false;
}

bool Server::ensureTurn(int slot)
{
    if (!ensureInGame(slot)) {
        return false;
    }
    if (turn_ == players_[slot].id) {
        return true;
    }
    sendError(slot, QStringLiteral("Сейчас не ваш ход"));
    return false;
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
    } else if (player.hasSecret) {
        sendError(slot, QStringLiteral("Персонаж уже загадан"));
    } else if (characterId < 0 || characterId >= gamecore::kCharacterCount) {
        sendError(slot, QStringLiteral("Недопустимый characterId"));
    } else {
        player.secret = characterId;
        player.hasSecret = true;
        maybeStartMatch();
    }
}

void Server::handleQuestion(int slot, int questionId)
{
    if (!ensureTurn(slot)) {
        return;
    }
    if (questionId < 0 || questionId >= gamecore::kQuestionCount) {
        sendError(slot, QStringLiteral("Недопустимый questionId"));
    } else if (isAsked(slot, questionId)) {
        sendError(slot, QStringLiteral("Этот вопрос уже задан"));
    } else {
        players_[slot].lastTurnWasHint = false;
        applyQuestion(slot, questionId, {}, 0.0);
    }
}

void Server::handleHint(int slot, const QString& kind)
{
    if (!ensureTurn(slot)) {
        return;
    }
    Player& player = players_[slot];
    const int index = kind == QLatin1String("variance")     ? 0
                    : kind == QLatin1String("randomforest") ? 1
                    : kind == QLatin1String("catboost")     ? 2
                                                            : -1;
    if (index < 0) {
        sendError(slot, QStringLiteral("Неизвестная подсказка: %1").arg(kind));
        return;
    }
    if (player.hints[index] <= 0) {
        sendError(slot, QStringLiteral("Подсказка «%1» уже использована").arg(kind));
        return;
    }
    if (player.lastTurnWasHint) {
        sendError(slot, QStringLiteral("Подсказку нельзя использовать два хода подряд — "
                                       "сначала задайте обычный вопрос или угадайте"));
        return;
    }
    const gamecore::HintResult result =
        index == 0 ? gamecore::hintVariance(player.candidates, player.asked)
        : index == 1 ? gamecore::hintRandomForest(player.candidates, player.asked)
                     : gamecore::hintCatBoost(player.candidates, player.asked);
    if (result.question < 0) {
        sendError(slot, QStringLiteral("Нет информативного вопроса"));
        return;
    }
    --player.hints[index];
    player.lastTurnWasHint = true;
    sendHintStatus(slot);
    applyQuestion(slot, result.question, kind, result.score);
}

void Server::applyQuestion(int slot, int questionId, const QString& hintKind, double metric)
{
    Player& player = players_[slot];
    const bool positive = gamecore::value(players_[opponentSlot(slot)].secret, questionId) > 0;
    const QString answer = positive ? QStringLiteral("yes") : QStringLiteral("no");
    player.asked.push_back(questionId);
    player.candidates =
        gamecore::filterCandidates(player.candidates, questionId, positive ? 1 : -1);
    QStringList response{QStringLiteral("ANSWER"), QString::number(questionId), answer,
                         QString::number(player.candidates.size())};
    if (!hintKind.isEmpty()) {
        response << hintKind << QString::number(metric, 'f', 3);
    }
    send(slot, response);
    send(opponentSlot(slot), {QStringLiteral("OPPONENT"), QString::number(questionId), answer});
    switchTurn();
}

void Server::switchTurn()
{
    turn_ = turn_ == 1 ? 2 : 1;
    broadcast({QStringLiteral("TURN"), QString::number(turn_)});
}
void Server::handleGetCandidates(int slot)
{
    if (!ensureInGame(slot)) {
        return;
    }
    QStringList response{QStringLiteral("CANDIDATES")};
    for (int id : players_[slot].candidates) {
        response << QString::number(id);
    }
    send(slot, response);
}

void Server::handleRemove(int slot, const QStringList& parts)
{
    if (!ensureInGame(slot)) {
        return;
    }
    gamecore::CandidateSet& candidates = players_[slot].candidates;
    for (int i = 1; i < parts.size(); ++i) {
        const int id = toInt(parts, i);
        const auto it = std::find(candidates.begin(), candidates.end(), id);
        if (it != candidates.end()) {
            candidates.erase(it);
        }
    }
    send(slot, {QStringLiteral("REMOVED"), QString::number(candidates.size())});
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
    player.lastTurnWasHint = false;
    const int secret = players_[opponentSlot(slot)].secret;
    if (characterId == secret) {
        state_ = State::MatchOver;
        for (Player& other : players_) {
            other.wantsRematch = false;
        }
        broadcast({QStringLiteral("OVER"), QString::number(player.id), QString::number(secret)});
        return;
    }
    send(slot, {QStringLiteral("WRONG")});
    switchTurn();
}

void Server::handleRematch(int slot)
{
    if (state_ != State::MatchOver) {
        sendError(slot, QStringLiteral("Реванш возможен только после окончания партии"));
        return;
    }
    players_[slot].wantsRematch = true;
    if (players_[0].wantsRematch && players_[1].wantsRematch) {
        for (Player& player : players_) {
            player.hasSecret = false;
            player.secret = -1;
        }
        beginWaitingSecrets();
    }
}

void Server::handleLeave(int slot) { dropLobby(slot); }