#pragma once

#include <string>

namespace pdk::game {

struct StrategyMetadata {
    std::string strategy;
    std::string strategyVersion;
};

inline StrategyMetadata HumanStrategyMetadata() {
    return {"human", "1"};
}

inline StrategyMetadata BasicStrategyMetadata() {
    return {"basic", "1"};
}

inline StrategyMetadata StrongStrategyMetadata() {
    return {"strong", "2.1"};
}

inline StrategyMetadata RulesStrategyMetadata() {
    return {"rules-forced", "pdk48-v1"};
}

} // namespace pdk::game
