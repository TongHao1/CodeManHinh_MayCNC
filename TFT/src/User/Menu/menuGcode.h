#ifndef _MENU_GCODE_H_
#define _MENU_GCODE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "coordinate.h"

// External variables for 4x4 keyboard
extern uint8_t gcode_item_moveLen_index;
extern AXIS gcode_nowAxis;
extern bool gcodeStepMode;

// Functions
void menuGcode(void); // G-code menu function
void storeGcodeMoveCmd(const AXIS xyz, const float amount);

#ifdef __cplusplus
}
#endif

#endif
