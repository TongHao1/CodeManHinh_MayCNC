#include "Terminal.h"
#include "includes.h"
#include "Move.h"

#define X_MOVE_GCODE        "G0 X%.2f F%d\n"
#define Y_MOVE_GCODE        "G0 Y%.2f F%d\n"
#define Z_MOVE_GCODE        "G0 Z%.2f F%d\n"

static const char * const xyzMoveCmd[] = {X_MOVE_GCODE, Y_MOVE_GCODE, Z_MOVE_GCODE};

// Global variables for 4x4 keyboard
uint8_t gcode_item_moveLen_index = 1;
AXIS gcode_nowAxis = X_AXIS;
bool gcodeStepMode = false;  // false = axis selection, true = step selection

void storeGcodeMoveCmd(const AXIS xyz, const float amount)
{
  storeCmd(xyzMoveCmd[xyz], GET_BIT(infoSettings.inverted_axis, xyz) ? -amount : amount,
           xyz != Z_AXIS ? infoSettings.xy_speed[infoSettings.move_speed] : infoSettings.z_speed[infoSettings.move_speed]);
  gcode_nowAxis = xyz;
}

// Simple menuGcode function that opens the 4x4 G-code keyboard
void menuGcode(void)
{
  menuGcodeKeyboard();  // Open the 4x4 G-code keyboard
}

// void menuGcode(void)
// {
//   MENUITEMS menuItems = gcodeItems;
//   KEY_VALUES key_num = KEY_IDLE;
//   float amount = moveLenSteps[item_moveLen_index];

//   mustStoreCmd("G91\n");
//   mustStoreCmd("M114\n");

//   menuItems.items[3] = itemMoveLen[item_moveLen_index];

//   menuDrawPage(&menuItems);
//   drawGcodeXYZ();

//   while (MENU_IS(menuGcode))
//   {
//     key_num = menuKeyGetValue();
//     switch (key_num)
//     {
//       case KEY_ICON_0: nowAxis = X_AXIS; menuDrawPage(&menuItems); break;
//       case KEY_ICON_1: nowAxis = Y_AXIS; menuDrawPage(&menuItems); break;
//       case KEY_ICON_2: nowAxis = Z_AXIS; menuDrawPage(&menuItems); break;
//       case KEY_ICON_3:
//         item_moveLen_index = (item_moveLen_index + 1) % ITEM_MOVE_LEN_NUM;
//         menuItems.items[3] = itemMoveLen[item_moveLen_index];
//         menuDrawItem(&menuItems.items[3], 3);
//         amount = moveLenSteps[item_moveLen_index];
//         break;
//       case KEY_ICON_7: CLOSE_MENU(); break;
//       case KEY_INCREASE:
//         storeGcodeMoveCmd(nowAxis, amount);
//         break;
//       case KEY_DECREASE:
//         storeGcodeMoveCmd(nowAxis, -amount);
//         break;
//       case KEY_INFOBOX:
//         OPEN_MENU(menuNotification);
//         break;
//       default:
//         break;
//     }
//     loopProcess();
//     updateGcodeGantry();
//   }

//   mustStoreCmd("G90\n");
// }
