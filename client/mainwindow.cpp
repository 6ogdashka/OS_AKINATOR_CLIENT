#include "mainwindow.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include <cstddef>

#include "connection.h"
#include "gamecore/gamecore.h"
#include "protocol.h"

namespace {
constexpr int kConnectPage = 0;
constexpr int kSecretPage = 1;
constexpr int kGamePage = 2;
constexpr int kStateImageSide = 264;
constexpr int kConnectImageSide = 190;
constexpr int kSecretImageSide = 132;
QPixmap loadStatePixmap(const QString& name, int side)
{
    return QPixmap(QStringLiteral(":/assets/state_%1.png").arg(name))
        .scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
QLabel* makeLabel(QWidget* parent, const QString& text = {}, const QString& name = {})
{
    auto* label = new QLabel(text, parent);
    if (!name.isEmpty()) label->setObjectName(name);
    return label;
}
QPushButton* makeButton(QWidget* parent, const QString& text, const QString& name = {})
{
    auto* button = new QPushButton(text, parent);
    if (!name.isEmpty()) button->setObjectName(name);
    return button;
}
QLabel* makeImageLabel(QWidget* parent, const QString& name, int side)
{
    auto* label = makeLabel(parent, {}, QStringLiteral("stateImage"));
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(loadStatePixmap(name, side));
    return label;
}
void setErrorStyle(QLabel* label, bool error)
{
    if (label->property("error").toBool() == error) {
        return;
    }
    label->setProperty("error", error);
    label->style()->unpolish(label);
    label->style()->polish(label);
}
QString connectionErrorMessage(const QString& raw, const QString& endpoint)
{
    if (raw.contains(QStringLiteral("refused"), Qt::CaseInsensitive)) {
        return QStringLiteral("%1 — по адресу %2 никто не принимает подключения. "
                              "Проверьте адрес, порт и что сервер запущен.")
            .arg(raw, endpoint);
    }
    if (raw.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
        || raw.contains(QStringLiteral("unreachable"), Qt::CaseInsensitive)) {
        return QStringLiteral("%1 — %2 не отвечает. Проверьте сеть и фаервол.")
            .arg(raw, endpoint);
    }
    return raw;
}
void filterList(QListWidget* list, const QString& text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        item->setHidden(!needle.isEmpty() && !item->text().contains(needle, Qt::CaseInsensitive));
    }
}
void resetSecretPage(QLineEdit* search, QListWidget* list, QPushButton* confirm)
{
    search->clear();
    search->setEnabled(true);
    list->setEnabled(true);
    confirm->setEnabled(true);
}
int visibleItemCount(QListWidget* list)
{
    int visible = 0;
    for (int row = 0; row < list->count(); ++row) {
        visible += list->item(row)->isHidden() ? 0 : 1;
    }
    return visible;
}
}

MainWindow::MainWindow(Connection* connection, const QString& host, int port,
                       const QString& envWarning, QWidget* parent)
    : QMainWindow(parent)
    , connection_(connection)
{
    buildUi();
    hostField_->setText(host);
    portField_->setValue(port);
    if (!envWarning.isEmpty()) {
        connectStatusLabel_->setText(envWarning);
    }
    connect(connection_, &Connection::connected, this, [this]() {
        connectButton_->setEnabled(false);
        hostField_->setEnabled(false);
        portField_->setEnabled(false);
        connectStatusLabel_->setText(QStringLiteral("Подключение установлено, ждём приветствия…"));
    });
    connect(connection_, &Connection::messageReceived, this, &MainWindow::onMessage);
    connect(connection_, &Connection::errorOccurred, this, &MainWindow::onServerError);
    connect(connection_, &Connection::connectionClosed, this, &MainWindow::onConnectionClosed);
    resetGameData();
    applyState(UiState::Disconnected);
}

void MainWindow::onMessage(const QStringList& parts)
{
    if (parts.isEmpty()) {
        return;
    }
    const QString& cmd = parts[0];
    if (cmd == QLatin1String("WELCOME")) {
        onWelcome(parts.value(1).toInt());
    } else if (cmd == QLatin1String("START")) {
        onStart();
    } else if (cmd == QLatin1String("TURN")) {
        onTurnChanged(parts.value(1).toInt());
    } else if (cmd == QLatin1String("ANSWER")) {
        if (parts.size() < 4) {
            appendLog(QStringLiteral("Некорректное сообщение ANSWER"));
        } else {
            onAnswer(parts[1].toInt(), parts[2], parts[3].toInt(), parts.value(4),
                     parts.value(5).toDouble());
        }
    } else if (cmd == QLatin1String("HINTS")) {
        onHintStatus(parts.value(1).toInt(), parts.value(2).toInt(), parts.value(3).toInt());
    } else if (cmd == QLatin1String("OPPONENT") && parts.size() >= 3) {
        onOpponentAsked(parts[1].toInt(), parts[2]);
    } else if (cmd == QLatin1String("CANDIDATES")) {
        QList<int> ids;
        for (int i = 1; i < parts.size(); ++i) {
            ids << parts[i].toInt();
        }
        onCandidates(ids);
    } else if (cmd == QLatin1String("REMOVED")) {
        onRemoveOk(parts.value(1).toInt());
    } else if (cmd == QLatin1String("WRONG")) {
        onGuessResult();
    } else if (cmd == QLatin1String("OVER")) {
        onMatchOver(parts.value(1).toInt(), parts.value(2).toInt());
    } else if (cmd == QLatin1String("ERROR")) {
        onServerError(parts.mid(1).join(QLatin1Char(' ')));
    } else if (cmd == QLatin1String("OPPONENT_LEFT")) {
        onOpponentDisconnected(parts.mid(1).join(QLatin1Char(' ')));
    } else {
        appendLog(QStringLiteral("Неизвестное сообщение сервера: %1").arg(cmd));
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Бой Акинаторов"));
    resize(1040, 720);
    setMinimumSize(920, 640);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildConnectPage());
    stack_->addWidget(buildSecretPage());
    stack_->addWidget(buildGamePage());
    setCentralWidget(stack_);
}

QWidget* MainWindow::buildConnectPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("page"));
    auto* title = makeLabel(page, QStringLiteral("БОЙ АКИНАТОРОВ"), QStringLiteral("brandTitle"));
    title->setAlignment(Qt::AlignCenter);
    auto* subtitle = makeLabel(page,
        QStringLiteral("Дуэль на двоих: 60 персонажей, 139 вопросов и три ML-подсказки"),
        QStringLiteral("brandSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    auto* image = makeImageLabel(page, QStringLiteral("idle"), kConnectImageSide);
    hostField_ = new QLineEdit(page);
    hostField_->setPlaceholderText(QStringLiteral("127.0.0.1"));
    portField_ = new QSpinBox(page);
    portField_->setRange(1, 65535);
    portField_->setValue(9999);
    portField_->setMaximumWidth(140);
    connectButton_ =
        makeButton(page, QStringLiteral("Подключиться"), QStringLiteral("primaryButton"));
    connectButton_->setCursor(Qt::PointingHandCursor);
    connectStatusLabel_ = makeLabel(page, {}, QStringLiteral("mutedLabel"));
    connectStatusLabel_->setWordWrap(true);
    connectStatusLabel_->setAlignment(Qt::AlignCenter);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setSpacing(10);
    form->addRow(QStringLiteral("Адрес сервера:"), hostField_);
    form->addRow(QStringLiteral("Порт:"), portField_);
    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("cardHighlight"));
    card->setMaximumWidth(540);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 26, 28, 24);
    cardLayout->setSpacing(12);
    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addWidget(image, 0, Qt::AlignHCenter);
    cardLayout->addLayout(form);
    cardLayout->addSpacing(4);
    cardLayout->addWidget(connectButton_);
    cardLayout->addWidget(connectStatusLabel_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addStretch();
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    return page;
}

QWidget* MainWindow::buildSecretPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("page"));
    auto* title = makeLabel(page, QStringLiteral("Загадайте персонажа"), QStringLiteral("pageTitle"));
    auto* subtitle = makeLabel(page,
        QStringLiteral("Соперник будет задавать вопросы именно про него"),
        QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    auto* image = makeImageLabel(page, QStringLiteral("think"), kSecretImageSide);
    searchField_ = new QLineEdit(page);
    searchField_->setObjectName(QStringLiteral("searchField"));
    searchField_->setPlaceholderText(QStringLiteral("Поиск персонажа…"));
    searchField_->setClearButtonEnabled(true);
    characterList_ = new QListWidget(page);
    characterList_->setAlternatingRowColors(true);
    characterList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    for (int id = 0; id < gamecore::kCharacterCount; ++id) {
        auto* item = new QListWidgetItem(characterName(id), characterList_);
        item->setData(Qt::UserRole, id);
    }
    confirmSecretButton_ =
        makeButton(page, QStringLiteral("Загадать выбранного"), QStringLiteral("primaryButton"));
    confirmSecretButton_->setCursor(Qt::PointingHandCursor);
    secretStatusLabel_ = makeLabel(page, {}, QStringLiteral("mutedLabel"));
    secretStatusLabel_->setWordWrap(true);
    auto* headerText = new QVBoxLayout;
    headerText->setSpacing(4);
    headerText->addWidget(title);
    headerText->addWidget(subtitle);
    headerText->addStretch();
    auto* header = new QHBoxLayout;
    header->setSpacing(16);
    header->addWidget(image);
    header->addLayout(headerText, 1);
    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("card"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(12);
    cardLayout->addLayout(header);
    cardLayout->addWidget(searchField_);
    cardLayout->addWidget(characterList_, 1);
    cardLayout->addWidget(confirmSecretButton_);
    cardLayout->addWidget(secretStatusLabel_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->addWidget(card, 1);
    connect(searchField_, &QLineEdit::textChanged, this,
            [this](const QString& text) { filterList(characterList_, text); });
    connect(characterList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onConfirmSecretClicked(); });
    connect(confirmSecretButton_, &QPushButton::clicked, this,
            &MainWindow::onConfirmSecretClicked);
    return page;
}

QWidget* MainWindow::buildGamePage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("page"));
    statusLabel_ = makeLabel(page, {}, QStringLiteral("statusLabel"));
    statusLabel_->setWordWrap(true);
    mySecretLabel_ = makeLabel(page, {}, QStringLiteral("mySecretLabel"));
    mySecretLabel_->setWordWrap(true);
    mySecretLabel_->setAlignment(Qt::AlignCenter);
    mySecretLabel_->setToolTip(QStringLiteral("Персонаж, которого вы загадали: соперник "
                                              "задаёт вопросы именно про него."));
    candidatesLabel_ = makeLabel(page, {}, QStringLiteral("statLabel"));
    candidatesLabel_->setAlignment(Qt::AlignCenter);
    hintsLabel_ = makeLabel(page, {}, QStringLiteral("hintStat"));
    hintsLabel_->setWordWrap(true);
    hintsLabel_->setAlignment(Qt::AlignCenter);
    stateImage_ = makeImageLabel(page, QStringLiteral("idle"), kStateImageSide);
    askButton_ = makeButton(page, QStringLiteral("Задать вопрос"), QStringLiteral("primaryButton"));
    const auto makeHint = [page](const QString& text, const QString& name) {
        auto* button = makeButton(page, text, QStringLiteral("hintButton"));
        button->setToolTip(
            QStringLiteral("Подсказка %1: по одной на партию, не две подсказки подряд").arg(name));
        return button;
    };
    hintVarianceButton_ = makeHint(QStringLiteral("Подсказка: дисперсия"),
                                   QStringLiteral("«дисперсия»"));
    hintRandomForestButton_ = makeHint(QStringLiteral("Random Forest"),
                                       QStringLiteral("Random Forest"));
    hintCatBoostButton_ = makeHint(QStringLiteral("CatBoost"), QStringLiteral("CatBoost"));
    tableButton_ = makeButton(page, QStringLiteral("Открыть таблицу"));
    guessButton_ = makeButton(page, QStringLiteral("Угадать"));
    rematchButton_ = makeButton(page, QStringLiteral("Реванш"), QStringLiteral("primaryButton"));
    leaveButton_ = makeButton(page, QStringLiteral("Выход"), QStringLiteral("dangerButton"));
    logText_ = new QPlainTextEdit(page);
    logText_->setReadOnly(true);
    logText_->setMinimumHeight(160);
    auto* logTitle = makeLabel(page, QStringLiteral("Журнал партии"), QStringLiteral("pageSubtitle"));
    auto* avatarCard = new QFrame(page);
    avatarCard->setObjectName(QStringLiteral("cardHighlight"));
    avatarCard->setFixedWidth(300);
    auto* avatarLayout = new QVBoxLayout(avatarCard);
    avatarLayout->setContentsMargins(16, 18, 16, 18);
    avatarLayout->setSpacing(10);
    avatarLayout->addWidget(stateImage_, 0, Qt::AlignHCenter);
    avatarLayout->addWidget(mySecretLabel_);
    avatarLayout->addWidget(candidatesLabel_);
    avatarLayout->addWidget(hintsLabel_);
    avatarLayout->addStretch();
    auto* buttons = new QGridLayout;
    buttons->setSpacing(10);
    buttons->addWidget(askButton_, 0, 0, 1, 2);
    buttons->addWidget(guessButton_, 0, 2);
    buttons->addWidget(hintVarianceButton_, 1, 0);
    buttons->addWidget(hintRandomForestButton_, 1, 1);
    buttons->addWidget(hintCatBoostButton_, 1, 2);
    buttons->addWidget(tableButton_, 2, 0, 1, 2);
    buttons->addWidget(leaveButton_, 2, 2);
    buttons->addWidget(rematchButton_, 3, 0, 1, 3);
    auto* actionsCard = new QFrame(page);
    actionsCard->setObjectName(QStringLiteral("card"));
    auto* actionsLayout = new QVBoxLayout(actionsCard);
    actionsLayout->setContentsMargins(16, 16, 16, 16);
    actionsLayout->addLayout(buttons);
    auto* right = new QVBoxLayout;
    right->setSpacing(12);
    right->addWidget(statusLabel_);
    right->addWidget(actionsCard);
    right->addWidget(logTitle);
    right->addWidget(logText_, 1);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    layout->addWidget(avatarCard, 0);
    layout->addLayout(right, 1);
    connect(askButton_, &QPushButton::clicked, this, &MainWindow::onAskClicked);
    connect(hintVarianceButton_, &QPushButton::clicked, this,
            [this] { requestHint(QStringLiteral("variance")); });
    connect(hintRandomForestButton_, &QPushButton::clicked, this,
            [this] { requestHint(QStringLiteral("randomforest")); });
    connect(hintCatBoostButton_, &QPushButton::clicked, this,
            [this] { requestHint(QStringLiteral("catboost")); });
    connect(tableButton_, &QPushButton::clicked, this,
            [this] { requestCandidates(PendingRequest::Table); });
    connect(guessButton_, &QPushButton::clicked, this,
            [this] { requestCandidates(PendingRequest::Guess); });
    connect(rematchButton_, &QPushButton::clicked, this, &MainWindow::onRematchClicked);
    connect(leaveButton_, &QPushButton::clicked, this, &MainWindow::onLeaveClicked);
    return page;
}

void MainWindow::applyState(UiState state)
{
    state_ = state;
    const bool canConnect = state == UiState::Disconnected;
    hostField_->setEnabled(canConnect);
    portField_->setEnabled(canConnect);
    connectButton_->setEnabled(canConnect);
    stack_->setCurrentIndex(state == UiState::WaitingOpponent ? kSecretPage
                           : (state == UiState::Disconnected || state == UiState::Connecting)
                               ? kConnectPage
                               : kGamePage);
    const bool myTurn = state == UiState::MyTurn;
    const bool inGame = myTurn || state == UiState::WaitResult || state == UiState::OpponentTurn;
    const bool matchOver = state == UiState::MatchOver;
    const bool showCandidates = myTurn || state == UiState::WaitResult;
    for (QPushButton* button :
         {askButton_, hintVarianceButton_, hintRandomForestButton_, hintCatBoostButton_}) {
        button->setVisible(inGame);
    }
    tableButton_->setVisible(showCandidates);
    guessButton_->setVisible(showCandidates);
    rematchButton_->setVisible(matchOver);
    leaveButton_->setVisible(inGame || matchOver);
    askButton_->setEnabled(myTurn);
    tableButton_->setEnabled(myTurn);
    guessButton_->setEnabled(myTurn);
    const bool hints = myTurn && !hintUsedLastTurn_;
    hintVarianceButton_->setEnabled(hints && hintsLeftVariance_ > 0);
    hintRandomForestButton_->setEnabled(hints && hintsLeftRandomForest_ > 0);
    hintCatBoostButton_->setEnabled(hints && hintsLeftCatBoost_ > 0);
    QString image = QStringLiteral("idle");
    switch (state) {
    case UiState::MyTurn:
        statusLabel_->setText(QStringLiteral("Ваш ход"));
        image = QStringLiteral("think");
        break;
    case UiState::WaitResult:
        statusLabel_->setText(QStringLiteral("Ожидание ответа…"));
        break;
    case UiState::OpponentTurn:
        statusLabel_->setText(QStringLiteral("Ход соперника"));
        break;
    case UiState::MatchOver:
        statusLabel_->setText(QStringLiteral("Партия завершена"));
        image = lastMatchWon_ ? QStringLiteral("happy") : QStringLiteral("hard");
        break;
    default:
        statusLabel_->clear();
        break;
    }
    setErrorStyle(statusLabel_, false);
    updateCandidatesLabel();
    updateHintsLabel();
    stateImage_->setPixmap(loadStatePixmap(image, kStateImageSide));
}

void MainWindow::resetGameData(bool keepSecret)
{
    askedQuestions_.clear();
    pending_ = PendingRequest::None;
    hintUsedLastTurn_ = false;
    candidatesCount_ = gamecore::kCharacterCount;
    hintsLeftVariance_ = hintsLeftRandomForest_ = hintsLeftCatBoost_ = 1;
    if (!keepSecret) {
        mySecretId_ = -1;
    }
    logText_->clear();
    updateMySecretLabel();
}

void MainWindow::appendLog(const QString& text) { logText_->appendPlainText(text); }

void MainWindow::updateCandidatesLabel()
{
    candidatesLabel_->setText(QStringLiteral("Кандидатов: %1").arg(candidatesCount_));
}

void MainWindow::updateHintsLabel()
{
    QString text = QStringLiteral("Подсказки — дисперсия: %1, Random Forest: %2, CatBoost: %3")
                       .arg(hintsLeftVariance_)
                       .arg(hintsLeftRandomForest_)
                       .arg(hintsLeftCatBoost_);
    if (hintUsedLastTurn_) {
        text += QStringLiteral(" · подсказка была в прошлый ход: сначала обычный вопрос");
    }
    hintsLabel_->setText(text);
}

void MainWindow::updateMySecretLabel()
{
    if (mySecretId_ < 0) {
        mySecretLabel_->setText(QStringLiteral("Ваш персонаж: не выбран"));
        return;
    }
    const QString name = characterName(mySecretId_);
    mySecretLabel_->setText(QStringLiteral("Ваш персонаж: %1").arg(name));
    mySecretLabel_->setToolTip(QStringLiteral("Вы загадали «%1» — соперник задаёт вопросы "
                                              "про него.").arg(name));
}

void MainWindow::onConnectClicked()
{
    QString host = hostField_->text().trimmed();
    if (host.isEmpty()) {
        host = QStringLiteral("127.0.0.1");
        hostField_->setText(host);
    }
    const int port = portField_->value();
    connectStatusLabel_->setText(QStringLiteral("Подключение к %1:%2…").arg(host).arg(port));
    setErrorStyle(connectStatusLabel_, false);
    applyState(UiState::Connecting);
    connection_->connectToServer(host, static_cast<quint16>(port));
}

void MainWindow::onConnectionClosed()
{
    if (state_ != UiState::Connecting && state_ != UiState::Disconnected) {
        connectStatusLabel_->setText(QStringLiteral("Соединение с сервером закрыто"));
    }
    secretStatusLabel_->clear();
    applyState(UiState::Disconnected);
}

void MainWindow::onWelcome(int player)
{
    localPlayer_ = player;
    resetGameData();
    resetSecretPage(searchField_, characterList_, confirmSecretButton_);
    characterList_->setCurrentRow(0);
    secretStatusLabel_->setText(
        QStringLiteral("Вы — игрок %1. Загадайте персонажа для соперника.").arg(player));
    connectStatusLabel_->setText(QStringLiteral("Вы — игрок %1").arg(player));
    applyState(UiState::WaitingOpponent);
}

void MainWindow::onConfirmSecretClicked()
{
    if (state_ != UiState::WaitingOpponent) {
        return;
    }
    QListWidgetItem* item = characterList_->currentItem();
    if (!item || item->isHidden()) {
        secretStatusLabel_->setText(QStringLiteral("Выберите персонажа из списка"));
        return;
    }
    const int id = item->data(Qt::UserRole).toInt();
    connection_->send(QStringLiteral("SECRET"), {QString::number(id)});
    mySecretId_ = id;
    updateMySecretLabel();
    confirmSecretButton_->setEnabled(false);
    characterList_->setEnabled(false);
    searchField_->setEnabled(false);
    secretStatusLabel_->setText(
        QStringLiteral("Загадан «%1». Ожидание второго игрока…").arg(item->text()));
}

void MainWindow::onStart()
{
    resetGameData(true);
    appendLog(QStringLiteral("Партия начинается."));
    appendLog(QStringLiteral("Вы — игрок %1.").arg(localPlayer_));
    if (mySecretId_ >= 0) {
        appendLog(QStringLiteral("Вы загадали «%1» — соперник угадывает именно его.")
                      .arg(characterName(mySecretId_)));
    }
    applyState(UiState::OpponentTurn);
}

void MainWindow::onTurnChanged(int player)
{
    appendLog(player == localPlayer_ ? QStringLiteral("— Ваш ход —")
                                     : QStringLiteral("— Ход соперника —"));
    applyState(player == localPlayer_ ? UiState::MyTurn : UiState::OpponentTurn);
}

void MainWindow::onAnswer(int questionId, const QString& answer, int remaining,
                          const QString& hintKind, double hintMetric)
{
    askedQuestions_.insert(questionId);
    candidatesCount_ = remaining;
    hintUsedLastTurn_ = !hintKind.isEmpty();
    const QString question = questionText(questionId);
    const QString answerValue = answerText(answer);
    if (hintKind.isEmpty()) {
        appendLog(QStringLiteral("Вы спросили «%1» → %2. Осталось %3 кандидатов.")
                      .arg(question, answerValue)
                      .arg(remaining));
    } else {
        appendLog(QStringLiteral("Подсказка «%1» (metric %2): «%3» → %4. Осталось %5. "
                                 "Следующая подсказка — после обычного вопроса.")
                      .arg(hintKind)
                      .arg(hintMetric, 0, 'f', 3)
                      .arg(question, answerValue)
                      .arg(remaining));
    }
    applyState(UiState::OpponentTurn);
}

void MainWindow::onHintStatus(int variance, int randomForest, int catBoost)
{
    hintsLeftVariance_ = variance;
    hintsLeftRandomForest_ = randomForest;
    hintsLeftCatBoost_ = catBoost;
    applyState(state_);
}

void MainWindow::onOpponentAsked(int questionId, const QString& answer)
{
    appendLog(QStringLiteral("Соперник спросил «%1» → %2").arg(questionText(questionId), answerText(answer)));
}

void MainWindow::onCandidates(const QList<int>& characterIds)
{
    candidatesCount_ = characterIds.size();
    updateCandidatesLabel();
    const PendingRequest pending = pending_;
    pending_ = PendingRequest::None;
    if (pending == PendingRequest::Table) {
        openTableDialog(characterIds);
    } else if (pending == PendingRequest::Guess) {
        const int chosen = guessDialog(characterIds);
        if (chosen >= 0) {
            connection_->send(QStringLiteral("GUESS"), {QString::number(chosen)});
            applyState(UiState::WaitResult);
        }
    }
}

void MainWindow::onRemoveOk(int remaining)
{
    candidatesCount_ = remaining;
    updateCandidatesLabel();
    appendLog(QStringLiteral("Убрано вручную, осталось %1 кандидатов").arg(remaining));
}

void MainWindow::onGuessResult()
{
    hintUsedLastTurn_ = false;
    appendLog(QStringLiteral("Неверная догадка — ход переходит к сопернику."));
    applyState(UiState::OpponentTurn);
}

void MainWindow::onMatchOver(int winner, int characterId)
{
    const bool won = winner == localPlayer_;
    lastMatchWon_ = won;
    const QString character = characterName(characterId);
    appendLog(won ? QStringLiteral("Победа! Соперник загадал «%1».").arg(character)
                  : QStringLiteral("Поражение: соперник угадал «%1».").arg(character));
    applyState(UiState::MatchOver);
    statusLabel_->setText(
        won ? QStringLiteral("Вы победили! Соперник загадал «%1»").arg(character)
            : QStringLiteral("Вы проиграли. Соперник угадал «%1»").arg(character));
}

void MainWindow::onServerError(const QString& text)
{
    appendLog(QStringLiteral("Ошибка: %1").arg(text));
    if (state_ == UiState::Disconnected || state_ == UiState::Connecting) {
        const QString endpoint = QStringLiteral("%1:%2")
                                     .arg(hostField_->text().trimmed())
                                     .arg(portField_->value());
        connectStatusLabel_->setText(connectionErrorMessage(text, endpoint));
        setErrorStyle(connectStatusLabel_, true);
        return;
    }
    pending_ = PendingRequest::None;
    if (state_ == UiState::WaitResult) {
        applyState(UiState::MyTurn);
    }
    statusLabel_->setText(QStringLiteral("Ошибка: %1").arg(text));
    setErrorStyle(statusLabel_, true);
}

void MainWindow::onOpponentDisconnected(const QString& text)
{
    appendLog(QStringLiteral("Соперник отключился: %1").arg(text));
    connectStatusLabel_->setText(text);
    applyState(UiState::WaitResult);
    statusLabel_->setText(QStringLiteral("Противник отключился"));
}

int MainWindow::pickItemDialog(const QString& title, const QString& okText,
                               const QList<QPair<QString, int>>& items)
{
    if (items.isEmpty()) {
        return -1;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(440, 520);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("Поиск…"));
    auto* list = new QListWidget(&dialog);
    for (const auto& [text, id] : items) {
        auto* item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, id);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(okText);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);
    connect(search, &QLineEdit::textChanged, &dialog,
            [list](const QString& text) { filterList(list, text); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog,
            [&dialog](QListWidgetItem*) { dialog.accept(); });
    list->setCurrentRow(0);
    if (dialog.exec() != QDialog::Accepted) {
        return -1;
    }
    QListWidgetItem* item = list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

int MainWindow::askQuestionDialog()
{
    QList<QPair<QString, int>> items;
    for (int id = 0; id < gamecore::kQuestionCount; ++id) {
        if (!askedQuestions_.contains(id)) {
            items.append({questionText(id), id});
        }
    }
    return pickItemDialog(QStringLiteral("Задать вопрос"), QStringLiteral("Спросить"), items);
}

int MainWindow::guessDialog(const QList<int>& characterIds)
{
    QList<QPair<QString, int>> items;
    for (int id : characterIds) {
        items.append({characterName(id), id});
    }
    if (items.isEmpty()) {
        appendLog(QStringLiteral("Список кандидатов пуст — угадывать некого."));
        return -1;
    }
    return pickItemDialog(QStringLiteral("Угадать персонажа"), QStringLiteral("Угадать"), items);
}

void MainWindow::openTableDialog(const QList<int>& characterIds)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Таблица кандидатов"));
    dialog.resize(440, 540);
    auto* countLabel =
        new QLabel(QStringLiteral("Кандидатов: %1").arg(characterIds.size()), &dialog);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("Поиск…"));
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (int id : characterIds) {
        auto* item = new QListWidgetItem(characterName(id), list);
        item->setData(Qt::UserRole, id);
    }
    auto* removeButton = new QPushButton(QStringLiteral("Удалить выбранных"), &dialog);
    auto* closeButton = new QPushButton(QStringLiteral("Закрыть"), &dialog);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(countLabel);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    auto* row = new QHBoxLayout;
    row->addWidget(removeButton);
    row->addStretch();
    row->addWidget(closeButton);
    layout->addLayout(row);
    const auto refresh = [list, countLabel] {
        countLabel->setText(QStringLiteral("Кандидатов: %1").arg(visibleItemCount(list)));
    };
    connect(search, &QLineEdit::textChanged, &dialog, [list, refresh](const QString& text) {
        filterList(list, text);
        refresh();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(removeButton, &QPushButton::clicked, this, [this, list, refresh] {
        const auto selected = list->selectedItems();
        if (selected.isEmpty()) {
            return;
        }
        QStringList ids;
        for (const QListWidgetItem* item : selected) {
            ids << QString::number(item->data(Qt::UserRole).toInt());
        }
        connection_->send(QStringLiteral("REMOVE"), ids);
        for (QListWidgetItem* item : selected) {
            delete list->takeItem(list->row(item));
        }
        refresh();
    });
    dialog.exec();
}

void MainWindow::onAskClicked()
{
    if (state_ != UiState::MyTurn) {
        return;
    }
    const int questionId = askQuestionDialog();
    if (questionId < 0) {
        return;
    }
    connection_->send(QStringLiteral("ASK"), {QString::number(questionId)});
    applyState(UiState::WaitResult);
}

void MainWindow::requestHint(const QString& kind)
{
    if (state_ != UiState::MyTurn) {
        return;
    }
    connection_->send(QStringLiteral("HINT"), {kind});
    applyState(UiState::WaitResult);
}

void MainWindow::requestCandidates(PendingRequest pending)
{
    if (state_ != UiState::MyTurn) {
        return;
    }
    pending_ = pending;
    connection_->send(QStringLiteral("TABLE"));
}

void MainWindow::onRematchClicked()
{
    connection_->send(QStringLiteral("REMATCH"));
    resetGameData();
    resetSecretPage(searchField_, characterList_, confirmSecretButton_);
    secretStatusLabel_->setText(QStringLiteral("Загадайте персонажа для реванша."));
    applyState(UiState::WaitingOpponent);
}

void MainWindow::onLeaveClicked()
{
    connection_->send(QStringLiteral("LEAVE"));
    connection_->disconnectFromServer();
    connectStatusLabel_->setText(QStringLiteral("Вы вышли из игры"));
    applyState(UiState::Disconnected);
}

QString MainWindow::characterName(int id)
{
    if (id < 0 || id >= gamecore::kCharacterCount) return QStringLiteral("#%1").arg(id);
    return QString::fromStdString(gamecore::characterNames()[static_cast<std::size_t>(id)]);
}

QString MainWindow::questionText(int id)
{
    if (id < 0 || id >= gamecore::kQuestionCount) return QStringLiteral("#%1").arg(id);
    return QString::fromStdString(gamecore::questionTexts()[static_cast<std::size_t>(id)]);
}

QString MainWindow::answerText(const QString& answer)
{
    return answer == QStringLiteral("yes") ? QStringLiteral("Да") : QStringLiteral("Нет");
}