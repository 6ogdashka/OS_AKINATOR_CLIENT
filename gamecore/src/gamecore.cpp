#include "gamecore/gamecore.h"

#include "data.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>

namespace gamecore {
namespace {
constexpr std::size_t cell(int character, int question)
{
    return static_cast<std::size_t>(character) * kQuestionCount + question;
}
CandidateSet normalized(const CandidateSet& candidates)
{
    CandidateSet out;
    for (CharacterId id : candidates) {
        if (id >= 0 && id < kCharacterCount) {
            out.push_back(id);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Persistent Python worker: real variance / Random Forest / CatBoost hints.
class HintEngine
{
public:
    static HintEngine& instance()
    {
        static HintEngine engine;
        return engine;
    }
    ~HintEngine() { stop(); }

    HintResult query(const QString& kind, const CandidateSet& candidates,
                     const std::vector<QuestionId>& excluded)
    {
        if (disabled_ || !ensureStarted()) {
            return {};
        }
        QJsonObject request;
        request[QStringLiteral("cmd")] = QStringLiteral("hint");
        request[QStringLiteral("kind")] = kind;
        QJsonArray ids;
        for (CharacterId id : candidates) {
            ids.append(id);
        }
        QJsonArray questions;
        for (QuestionId question : excluded) {
            questions.append(question);
        }
        request[QStringLiteral("candidates")] = ids;
        request[QStringLiteral("excluded")] = questions;

        QJsonObject response;
        if (!exchange(request, &response)) {
            stop();
            return {};
        }
        return {response.value(QStringLiteral("question")).toInt(-1),
                response.value(QStringLiteral("score")).toDouble(0.0)};
    }

private:
    bool ensureStarted()
    {
        if (initialized_ && process_.state() == QProcess::Running) {
            return true;
        }
        return start();
    }
    static QString scriptPath()
    {
        const QString configured = qEnvironmentVariable("AKINATOR_ML_SCRIPT");
        if (!configured.isEmpty()) {
            return configured;
        }
        if (QCoreApplication::instance()) {
            const QString beside =
                QCoreApplication::applicationDirPath() + QStringLiteral("/ml_hints.py");
            if (QFileInfo::exists(beside)) {
                return beside;
            }
        }
#ifdef AKINATOR_ML_SCRIPT_PATH
        const QString source = QStringLiteral(AKINATOR_ML_SCRIPT_PATH);
        if (QFileInfo::exists(source)) {
            return source;
        }
#endif
        return QStringLiteral("gamecore/ml_hints.py");
    }
    bool start()
    {
        stop();
        const QString script = scriptPath();
        if (!QFileInfo::exists(script)) {
            disabled_ = true;
            return false;
        }
        QString python = qEnvironmentVariable("AKINATOR_PYTHON");
        if (python.isEmpty()) {
#ifdef Q_OS_WIN
            python = QStringLiteral("python");
#else
            python = QStringLiteral("python3");
#endif
        }
        process_.setProgram(python);
        process_.setArguments({QStringLiteral("-u"), script, QStringLiteral("--serve")});
        process_.start();
        if (!process_.waitForStarted(10000)) {
            disabled_ = true;
            stop();
            return false;
        }
        QJsonObject init;
        init[QStringLiteral("cmd")] = QStringLiteral("init");
        QJsonArray matrix;
        for (std::int8_t value : data::kMatrix) {
            matrix.append(static_cast<int>(value));
        }
        init[QStringLiteral("matrix")] = matrix;
        QJsonObject response;
        if (!exchange(init, &response) || !response.value(QStringLiteral("ok")).toBool()) {
            disabled_ = true;
            stop();
            return false;
        }
        initialized_ = true;
        return true;
    }
    void stop()
    {
        initialized_ = false;
        buffer_.clear();
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(2000);
        }
    }
    bool exchange(const QJsonObject& request, QJsonObject* response)
    {
        if (process_.state() != QProcess::Running) {
            return false;
        }
        const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (process_.write(payload) != payload.size()) {
            return false;
        }
        if (!process_.waitForBytesWritten(5000)) {
            return false;
        }
        QByteArray line;
        if (!readLine(&line, 20000)) {
            return false;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            return false;
        }
        *response = document.object();
        return !response->contains(QStringLiteral("error"));
    }
    bool readLine(QByteArray* line, int timeoutMs)
    {
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            const int newline = buffer_.indexOf('\n');
            if (newline >= 0) {
                *line = buffer_.left(newline);
                buffer_.remove(0, newline + 1);
                return true;
            }
            const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
            if (remaining <= 0 || !process_.waitForReadyRead(remaining)) {
                return false;
            }
            buffer_.append(process_.readAllStandardOutput());
        }
    }

    QProcess process_;
    QByteArray buffer_;
    bool initialized_ = false;
    bool disabled_ = false;
};

HintResult pythonHint(const char* kind, const CandidateSet& candidates,
                      const std::vector<QuestionId>& excluded)
{
    const CandidateSet set = normalized(candidates);
    if (set.size() < 2) {
        return {};
    }
    return HintEngine::instance().query(QString::fromLatin1(kind), set, excluded);
}
} // namespace

const std::vector<std::string>& characterNames() { return data::kCharacterNames; }
const std::vector<std::string>& questionTexts() { return data::kQuestionTexts; }
const std::vector<std::int8_t>& matrix() { return data::kMatrix; }

int value(CharacterId character, QuestionId question)
{
    if (character < 0 || character >= kCharacterCount || question < 0
        || question >= kQuestionCount) {
        return 0;
    }
    return data::kMatrix[cell(character, question)];
}

CandidateSet filterCandidates(const CandidateSet& candidates, QuestionId question, Answer answer)
{
    const CandidateSet source = normalized(candidates);
    if (question < 0 || question >= kQuestionCount || (answer != 1 && answer != -1)) {
        return source;
    }
    CandidateSet result;
    for (CharacterId id : source) {
        if (value(id, question) == answer) {
            result.push_back(id);
        }
    }
    return result;
}

HintResult hintVariance(const CandidateSet& candidates, const std::vector<QuestionId>& excludedList)
{
    return pythonHint("variance", candidates, excludedList);
}

HintResult hintRandomForest(const CandidateSet& candidates,
                            const std::vector<QuestionId>& excludedList)
{
    return pythonHint("randomforest", candidates, excludedList);
}

HintResult hintCatBoost(const CandidateSet& candidates,
                        const std::vector<QuestionId>& excludedList)
{
    return pythonHint("catboost", candidates, excludedList);
}
} // namespace gamecore