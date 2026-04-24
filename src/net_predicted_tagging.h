#ifndef DK_NET_PREDICTED_TAGGING_H
#define DK_NET_PREDICTED_TAGGING_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

enum LocalPredictedDigState {
    LocalPredictedDig_None = 0,
    LocalPredictedDig_Land = 1,
    LocalPredictedDig_Gold = 2,
    LocalPredictedDig_Clear = 3,
};

void rebuild_local_predicted_dig_preview(void);
void clear_local_predicted_dig_preview(void);
unsigned char get_local_predicted_dig_state(MapSubtlCoord stl_x, MapSubtlCoord stl_y);

#ifdef __cplusplus
}
#endif

#endif
