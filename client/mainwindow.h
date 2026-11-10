#pragma once

#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

class Connection;

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Connection* connection, const QString& host, int port,
                        const QString& envWarning, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onConnectionClosed();

    void onMessage(const QStringList& parts);

    void onSecretSearchChanged(const QString& text);
    void onConfirmSecretClicked();
    void onAskClicked();
    void onTableClicked();
    void onGuessClicked();
    void onRematchClicked();
    void onLeaveClicked();

private:
    enum class UiState {
        Disconnected,
        Connecting,
        WaitingOpponent,
        MyTurn,
        WaitResult,
        OpponentTurn,
        MatchOver
    };

    enum class PendingRequest { None, Table, Guess };

    void buildUi();
    QWidget* buildConnectPage();
    QWidget* buildSecretPage();
    QWidget* buildGamePage();

    void applyState(UiState state);
    void resetGameData();
    void appendLog(const QString& text);
    void updateCandidatesLabel();
    void updateHintsLabel();

    void updateMySecretLabel();
    void updateTurnStatus();

    void updateStateImage();

    void onWelcome(int player);
    void onStart();
    void onTurnChanged(int player);
    void onAnswer(int questionId, const QString& answer, int remaining,
                  const QString& hintKind, double hintMetric);
    void onHintStatus(int variance, int randomForest, int catBoost);
    void onOpponentAsked(int questionId, const QString& answer);
    void onCandidates(const QList<int>& characterIds);
    void onRemoveOk(int remaining);
    void onGuessResult();
    void onMatchOver(int winner, int characterId);
    void onServerError(const QString& text);
    void onOpponentDisconnected(const QString& text);

    void requestHint(const QString& kind);
    int pickItemDialog(const QString& title, const QString& okText,
                       const QList<QPair<QString, int>>& items);
    int askQuestionDialog();
    void openTableDialog(const QList<int>& characterIds);
    int guessDialog(const QList<int>& characterIds);

    static QString characterName(int id);
    static QString questionText(int id);
    static QString answerText(const QString& answer);

    Connection* connection_ = nullptr;

    QStackedWidget* stack_ = nullptr;

    QLineEdit* hostField_ = nullptr;
    QSpinBox* portField_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QLabel* connectStatusLabel_ = nullptr;

    QLineEdit* searchField_ = nullptr;
    QListWidget* characterList_ = nullptr;
    QPushButton* confirmSecretButton_ = nullptr;
    QLabel* secretStatusLabel_ = nullptr;

    QLabel* statusLabel_ = nullptr;
    QLabel* mySecretLabel_ = nullptr;
    QLabel* candidatesLabel_ = nullptr;
    QLabel* hintsLabel_ = nullptr;
    QLabel* stateImage_ = nullptr;
    QPlainTextEdit* logText_ = nullptr;
    QPushButton* askButton_ = nullptr;
    QPushButton* hintVarianceButton_ = nullptr;
    QPushButton* hintRandomForestButton_ = nullptr;
    QPushButton* hintCatBoostButton_ = nullptr;
    QPushButton* tableButton_ = nullptr;
    QPushButton* guessButton_ = nullptr;
    QPushButton* rematchButton_ = nullptr;
    QPushButton* leaveButton_ = nullptr;

    UiState state_ = UiState::Disconnected;
    PendingRequest pending_ = PendingRequest::None;
    int localPlayer_ = 0;

    int mySecretId_ = -1;
    bool waitingAnswer_ = false;

    bool hintUsedLastTurn_ = false;
    bool lastMatchWon_ = false;
    int candidatesCount_ = 0;
    QSet<int> askedQuestions_;
    int hintsLeftVariance_ = 0;
    int hintsLeftRandomForest_ = 0;
    int hintsLeftCatBoost_ = 0;
};
