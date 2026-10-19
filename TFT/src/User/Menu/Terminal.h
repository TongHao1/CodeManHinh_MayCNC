#ifndef _TERMINAL_H_
#define _TERMINAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "SerialConnection.h"

typedef enum
{
  SRC_TERMINAL_GCODE = 0,
  SRC_TERMINAL_ACK,
  SRC_TERMINAL_COUNT
} TERMINAL_SRC;

void terminalCache(const char * stream, uint16_t streamLen, SERIAL_PORT_INDEX portIndex, TERMINAL_SRC src);
void menuTerminal(void);
void menuGcodeKeyboard(void);  // 4x4 G-code keyboard for menuGcode
void updateGcodeKeyboardPosition(const char * response);  // Update position from M114 response
void terminalNotifyAblError(const char * errorText);
void terminalNotifyAblState(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
