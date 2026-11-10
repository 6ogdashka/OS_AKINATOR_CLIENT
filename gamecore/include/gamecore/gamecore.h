#pragma once

#include "gamecore/export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gamecore {
using CharacterId = int;
using QuestionId  = int;
using Answer      = int;
using CandidateSet = std::vector<CharacterId>;

constexpr int kCharacterCount = 60;
constexpr int kQuestionCount  = 139;

GAMECORE_EXPORT const std::vector<std::string>& characterNames();
GAMECORE_EXPORT const std::vector<std::string>& questionTexts();

GAMECORE_EXPORT const std::vector<std::int8_t>& matrix();

GAMECORE_EXPORT int value(CharacterId character, QuestionId question);

GAMECORE_EXPORT CandidateSet filterCandidates(const CandidateSet& candidates,
                                              QuestionId question, Answer answer);

struct HintResult {
    QuestionId question = -1;
    double     score    = 0.0;
};

GAMECORE_EXPORT HintResult hintVariance(const CandidateSet& candidates,
                                        const std::vector<QuestionId>& excluded);

GAMECORE_EXPORT HintResult hintRandomForest(const CandidateSet& candidates,
                                            const std::vector<QuestionId>& excluded);

GAMECORE_EXPORT HintResult hintCatBoost(const CandidateSet& candidates,
                                        const std::vector<QuestionId>& excluded);
}
