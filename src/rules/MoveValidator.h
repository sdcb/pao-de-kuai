#pragma once

#include "rules/HandPattern.h"

#include <string>

namespace pdk::rules {

struct MoveValidation {
    bool ok{false};
    HandPattern pattern{};
    std::string reason;
};

MoveValidation ValidateLead(const Cards& cards, int handSizeBeforePlay = -1);
MoveValidation ValidateFollow(
    const Cards& cards,
    const HandPattern& previous,
    int handSizeBeforePlay = -1);
bool CanBeat(const HandPattern& candidate, const HandPattern& previous);
bool HasAnyFollowMove(const Cards& hand, const HandPattern& previous, int handSizeBeforePlay = -1);

} // namespace pdk::rules
