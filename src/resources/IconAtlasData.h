#pragma once

/*
 * Icon order inside the icon atlas.  Nothing includes this yet (the UI draws icons
 * by index), but it documents the sheet's order and gives the drawing code a
 * checked name once it is converted.
 *
 * Pure C: the enum used to be a scoped enum, so the names are prefixed now.
 */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ICON_PLAY = 0,
    ICON_PAUSE = 1,
    ICON_BACK = 2,
    ICON_SETTINGS = 3,
    ICON_STATS = 4,
    ICON_HELP = 5
};

#ifdef __cplusplus
}
#endif
