#pragma once

#include "game/AiStrategy.h"

#include <map>
#include <string>
#include <vector>

namespace pdk::game::ai_internal {

struct Candidate {
    rules::Cards cards;
    rules::HandPattern pattern;
    rules::Cards remainder;
    int disruptionPenalty{};
    int score{};
};

int PatternBaseScore(rules::PatternType type);
std::map<rules::Rank, int> CountRanks(const rules::Cards& cards);
int UnknownRankCount(const Candidate& candidate, const AiContext& context, rules::Rank rank);
int UnknownHigherControlCount(const Candidate& candidate, const AiContext& context, rules::Rank rank);
std::vector<Candidate> GenerateCandidates(const rules::Cards& hand, const AiContext& context);
void DeduplicateCandidates(std::vector<Candidate>& candidates);
std::string CandidateKey(const Candidate& candidate);

} // namespace pdk::game::ai_internal
