#include "BedLeveling.h"
#include "includes.h"

static void blUpdateState(MENUITEMS * menu, const uint8_t bedLevelState)
{
  if (bedLevelState == ENABLED)
  {
    menu->items[1].icon = ICON_LEVELING_ON;
    menu->items[1].label.index = LABEL_BL_ENABLE;
  }
  else
  {
    menu->items[1].icon = ICON_LEVELING_OFF;
    menu->items[1].label.index = LABEL_BL_DISABLE;
  }
}

#if DELTA_PROBE_TYPE == 2  // if Delta printer with removable probe

static void deltaMeshEditor(void)
{
  OPEN_MENU(menuMeshEditor);
}

static void deltaZOffset(void)
{
  storeCmd("M851\n");

  zOffsetSetMenu(true);  // use Probe Offset menu
  OPEN_MENU(menuZOffset);
}
#endif

void menuBedLeveling(void)
{
  MENUITEMS bedLevelingItems = {
    // title
    LABEL_ABL_SETTINGS,
    // icon                          label
    {
      {ICON_MESH_EDITOR,             LABEL_MESH_EDITOR},  // 0 Mesh Editor
      {ICON_LEVELING_OFF,            LABEL_BL_DISABLE},   // 1 ABL On/Off (sẽ cập nhật trạng thái bên dưới)
      {ICON_BACK,                    LABEL_BACK},         // 2 Back
      {ICON_NULL,                    LABEL_NULL},         // 3 (ẩn)
      {ICON_NULL,                    LABEL_NULL},         // 4 (ẩn)
      {ICON_NULL,                    LABEL_NULL},         // 5 (ẩn)
      {ICON_NULL,                    LABEL_NULL},         // 6 (ẩn)
      {ICON_NULL,                    LABEL_NULL},         // 7 (ẩn)
    }
  };

  KEY_VALUES key_num = KEY_IDLE;
  uint8_t levelStateOld = UNDEFINED;
  uint8_t levelStateNew = UNDEFINED;

  switch (infoMachineSettings.leveling)
  {
    case BL_BBL:
      bedLevelingItems.title.index = LABEL_ABL_SETTINGS_BBL;
      break;
    case BL_UBL:
      bedLevelingItems.title.index = LABEL_ABL_SETTINGS_UBL;
      break;
    case BL_MBL:
      bedLevelingItems.title.index = LABEL_MBL_SETTINGS;
      break;
    default:
      break;
  }

  if (infoMachineSettings.firmwareType == FW_MARLIN || infoMachineSettings.firmwareType == FW_REPRAPFW)
  {
    levelStateOld = levelStateNew = getParameter(P_ABL_STATE, 0);
    blUpdateState(&bedLevelingItems, levelStateNew);  // update icon & label 3
  }

  menuDrawPage(&bedLevelingItems);

  while (MENU_IS(menuBedLeveling))
  {
    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
        #if DELTA_PROBE_TYPE != 2
          OPEN_MENU(menuMeshEditor);
        #else
          popupDialog(DIALOG_TYPE_ALERT, LABEL_WARNING, LABEL_DISCONNECT_PROBE, LABEL_CONTINUE, LABEL_CANCEL, deltaMeshEditor, NULL, NULL);
        #endif
        break;

      case KEY_ICON_1:
        if (levelStateNew != UNDEFINED) {
          storeCmd((levelStateNew == ENABLED) ?
                   (infoMachineSettings.firmwareType != FW_REPRAPFW ? "M420 S0\n" : "G29 S2\n") :
                   (infoMachineSettings.firmwareType != FW_REPRAPFW ? "M420 S1\n" : "G29 S1\n"));
          // Chờ trạng thái cập nhật lại từ firmware (timeout tối đa 500ms)
          uint32_t wait = 0;
          uint8_t prevState = levelStateNew;
          do {
            loopProcess();
            levelStateNew = getParameter(P_ABL_STATE, 0);
            if (levelStateNew != prevState) break;
          } while (++wait < 50); // mỗi vòng ~10ms nếu loopProcess chạy nhanh
          blUpdateState(&bedLevelingItems, levelStateNew);
          menuDrawItem(&bedLevelingItems.items[1], 1);
        }
        break;

      case KEY_ICON_2:
        COOLDOWN_TEMPERATURE();
        CLOSE_MENU();
        break;

      default:
        break;
    }

    if (levelStateNew != UNDEFINED)  // it's Marlin or RepRap firmware
    {
      levelStateNew = getParameter(P_ABL_STATE, 0);

      if (levelStateOld != levelStateNew)  // check for bed leveling On/Off change
      {
        levelStateOld = levelStateNew;

        blUpdateState(&bedLevelingItems, levelStateOld);  // update icon & label 3
        menuDrawItem(&bedLevelingItems.items[1], 1);
      }
    }

    loopProcess();
  }
}
