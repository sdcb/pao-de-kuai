#pragma once

/*
 * Strategy identity recorded with every turn.
 *
 * Pure C: both fields are pointers to string literals produced by the factories
 * below, so there is nothing to allocate or free and the struct stays copyable.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StrategyMetadata {
    const char *strategy;
    const char *strategyVersion;
} StrategyMetadata;

static inline StrategyMetadata HumanStrategyMetadata(void)
{
    StrategyMetadata metadata;
    metadata.strategy = "human";
    metadata.strategyVersion = "1";
    return metadata;
}

static inline StrategyMetadata BasicStrategyMetadata(void)
{
    StrategyMetadata metadata;
    metadata.strategy = "basic";
    metadata.strategyVersion = "1";
    return metadata;
}

static inline StrategyMetadata StrongStrategyMetadata(void)
{
    StrategyMetadata metadata;
    metadata.strategy = "strong";
    metadata.strategyVersion = "2.1";
    return metadata;
}

static inline StrategyMetadata RulesStrategyMetadata(void)
{
    StrategyMetadata metadata;
    metadata.strategy = "rules-forced";
    metadata.strategyVersion = "pdk48-v1";
    return metadata;
}

static inline StrategyMetadata UnknownStrategyMetadata(void)
{
    StrategyMetadata metadata;
    metadata.strategy = "unknown";
    metadata.strategyVersion = "unknown";
    return metadata;
}

#ifdef __cplusplus
}
#endif
