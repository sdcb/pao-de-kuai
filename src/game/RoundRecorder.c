#include "game/RoundRecorder.h"

void RoundRecorder_Init(RoundRecorder *recorder)
{
    /* NULL root means "the process current directory", matching the old default. */
    StatStore_Init(&recorder->store, NULL);
}

bool RoundRecorder_AppendToday(RoundRecorder *recorder, const RoundRecord *record)
{
    char date[PDK_DATE_KEY_CAP];

    TodayDateKey(date, PDK_DATE_KEY_CAP);
    return StatStore_AppendRound(&recorder->store, date, record);
}
