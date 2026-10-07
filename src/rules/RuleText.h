#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Both return a pointer to a string literal that lives for the whole process, so
 * callers must not free them.  These are the shared rule summary shown on the
 * Help screen and the scoring blurb next to it. */
const char *SharedGameRulesText(void);
const char *HumanHelpText(void);

#ifdef __cplusplus
}
#endif
