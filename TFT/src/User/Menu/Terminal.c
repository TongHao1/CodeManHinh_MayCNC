#include "Terminal.h"
#include "includes.h"
#include "BedLeveling.h"
#include "menuGcode.h"
#include <math.h>

#if LCD_ENCODER_SUPPORT
  #include "LCD_Encoder.h"
#endif

#define MAX_GCODE_COUNT       3  // gcode history slots
#define MAX_PAGE_COUNT        20
#define MAX_TERMINAL_BUF_SIZE (NOBEYOND(600, RAM_SIZE * 45, 4800))

#define MILLING_MOTOR_ON_CMD  "M42 P16 S245"
#define MILLING_MOTOR_ON_CMD1  "M42 P16 S200"
#define MILLING_MOTOR_ON_CMD2  "M42 P16 S100"
#define MILLING_MOTOR_OFF_CMD "M42 P16 S0"

typedef struct
{
  CMD      gcodeTable[MAX_GCODE_COUNT];  // array of gcodes
  uint8_t  gcodeIndex;                   // current gcode index
} KEYBOARD_DATA;

typedef struct
{
  char *   pageTable[MAX_PAGE_COUNT];  // array to terminal page pointers within terminal buffer
  uint8_t  maxPageCount;               // maximum page count allowed
  uint8_t  pageCount;                  // current page count
  uint8_t  pageHead;                   // index of first page
  uint8_t  pageTail;                   // index of last page
  uint8_t  pageIndex;                  // current page index
  uint16_t bufSize;                    // maximum buffer/cache size
  uint16_t bufTail;                    // last copy index for copying data in buffer
  uint8_t  lastSrc;
} TERMINAL_DATA;

typedef enum
{
  KEYBOARD_VIEW = 0,
  TERMINAL_VIEW
} TERMINAL_WINDOW;

typedef enum
{
  GKEY_PREV = 0,
  GKEY_NEXT,
  GKEY_CLEAR,
  GKEY_SEND,
  GKEY_ABC_123,
  GKEY_SPACE,
  GKEY_DEL,
  GKEY_BACK,
  KEY_COUNT,
  GKEY_INCREASE = KEY_INCREASE,
  GKEY_DECREASE = KEY_DECREASE,
  GKEY_IDLE = IDLE_TOUCH,
} GKEY_VALUES;

typedef enum
{
  TERM_PAGE_DOWN = 0,
  TERM_PAGE_UP,
  TERM_TOGGLE_ACK,
  TERM_BACK,
  TERM_KEY_COUNT,
  TERM_KEY_INCREASE = KEY_INCREASE,
  TERM_KEY_DECREASE = KEY_DECREASE,
  TERM_KEY_IDLE = IDLE_TOUCH,
} TERM_KEY_VALUES;

// keyboard layouts
#define LAYOUT_1_COL_COUNT 6
#define LAYOUT_1_ROW_COUNT 4
#define LAYOUT_2_COL_COUNT 7
#define LAYOUT_2_ROW_COUNT 4
#define LAYOUT_3_COL_COUNT 10
#define LAYOUT_3_ROW_COUNT 6

#if LCD_WIDTH < 480
  #define KB_TYPE_STANDARD
  #define KB_COL_COUNT LAYOUT_1_COL_COUNT
  #define KB_ROW_COUNT LAYOUT_1_ROW_COUNT
#elif LCD_WIDTH < 800
  #define KB_TYPE_EXTENDED
  #define KB_COL_COUNT LAYOUT_2_COL_COUNT
  #define KB_ROW_COUNT LAYOUT_2_ROW_COUNT
#else
  #define KB_TYPE_QWERTY
  #define KB_COL_COUNT LAYOUT_3_COL_COUNT
  #define KB_ROW_COUNT LAYOUT_3_ROW_COUNT
#endif

#define EDITOR_KEY_COUNT (KEY_COUNT + (KB_COL_COUNT * KB_ROW_COUNT))

#define LAYOUT_QWERTY 0
#define LAYOUT_QWERTZ 1
#define LAYOUT_AZERTY 2

#define ROW_NUM        (KB_ROW_COUNT + 2)
#define CTRL_COL_COUNT 4
#define CTRL_WIDTH           (LCD_WIDTH / CTRL_COL_COUNT)
#define TERMINAL_CTRL_WIDTH  (LCD_WIDTH / (TERM_KEY_COUNT + 1))
#define CTRL_HEIGHT          ROW_HEIGHT
#define ROW_HEIGHT           (LCD_HEIGHT / ROW_NUM)

#define TEXTBOX_INSET         4
#define TEXTBOX_BUTTON_INSET  2
#define COMMAND_START_ROW     0
#define KB_START_ROW          1
#define KEY_WIDTH             (LCD_WIDTH / KB_COL_COUNT)
#define KEY_HEIGHT            ROW_HEIGHT
#define CTRL_START_ROW        (KB_START_ROW + KB_ROW_COUNT)

#define CURSOR_H_OFFSET 2
#define CURSOR_START_X CURSOR_H_OFFSET
#define CURSOR_END_Y   ((KB_START_ROW + KB_ROW_COUNT) * KEY_HEIGHT)

static const GUI_RECT textBoxRect = {
  TEXTBOX_INSET,
  (COMMAND_START_ROW * CTRL_HEIGHT) + TEXTBOX_INSET,
  (3 * CTRL_WIDTH) - TEXTBOX_INSET,
  ((COMMAND_START_ROW + 1) * CTRL_HEIGHT) - TEXTBOX_INSET,
};

// keyboard rectangles
static const GUI_RECT editorKeyRect[EDITOR_KEY_COUNT] = {
  // row text box helpers + send button
  {0 * CTRL_WIDTH + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET,
   1 * CTRL_WIDTH - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET},
  {1 * CTRL_WIDTH + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET,
   2 * CTRL_WIDTH - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET},
  {2 * CTRL_WIDTH + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET,
   3 * CTRL_WIDTH - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET},
  {3 * CTRL_WIDTH + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET,
   4 * CTRL_WIDTH - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET},

  // row control bar
  {0 * CTRL_WIDTH, CTRL_START_ROW * CTRL_HEIGHT, 1 * CTRL_WIDTH, LCD_HEIGHT},
  {1 * CTRL_WIDTH, CTRL_START_ROW * CTRL_HEIGHT, 2 * CTRL_WIDTH, LCD_HEIGHT},
  {2 * CTRL_WIDTH, CTRL_START_ROW * CTRL_HEIGHT, 3 * CTRL_WIDTH, LCD_HEIGHT},
  {3 * CTRL_WIDTH, CTRL_START_ROW * CTRL_HEIGHT, 4 * CTRL_WIDTH, LCD_HEIGHT},

  // row 1
  {0 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  #endif

  // row 2
  {0 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  #endif

  // row 3
  {0 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  #endif

  // row 4
  {0 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  #endif

  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    // row 5
    {0 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {1 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {2 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {3 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {4 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {5 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {6 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {7 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},

    // row 6
    {0 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {1 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {2 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {3 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {4 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {5 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {6 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {7 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
  #endif
};

// area rectangles
static const GUI_RECT editorAreaRect[3] = {
  {0, COMMAND_START_ROW * CTRL_HEIGHT, LCD_WIDTH, ROW_HEIGHT},                // text box + send area
  {0, ROW_HEIGHT,                      LCD_WIDTH, LCD_HEIGHT - CTRL_HEIGHT},  // keyboard area
  {0, CTRL_START_ROW * CTRL_HEIGHT,    LCD_WIDTH, LCD_HEIGHT}                 // control bar area
};

static const GUI_RECT terminalKeyRect[TERM_KEY_COUNT] = {
  {1 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 2 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // page down
  {2 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 3 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // page up
  {3 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 4 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // ACK
  {4 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 5 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // Back
};

static const GUI_RECT terminalPageRect = {
  0 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 1 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT};  // page number

static const GUI_RECT terminalAreaRect[2] = {
  {0,            0, LCD_WIDTH, CURSOR_END_Y},  // terminal area
  {0, CURSOR_END_Y, LCD_WIDTH,   LCD_HEIGHT},  // control area
};

// keyboard keys for first layout
static const char * const gcodeKey123[EDITOR_KEY_COUNT] = {
  "Prev", "Next", "Clear", "Send", "ABC", "Space", "Del", "Back",
  #if KB_COL_COUNT == LAYOUT_1_COL_COUNT
    "1", "2", "3", "M", "G", "T",
    "4", "5", "6", "X", "Y", "Z",
    "7", "8", "9", "E", "F", "R",
    ".", "0", "-", "/", "S", "V",
  #elif KB_COL_COUNT == LAYOUT_2_COL_COUNT
    "1", "2", "3", "M", "G", "T", "V",
    "4", "5", "6", "X", "Y", "Z", "S",
    "7", "8", "9", "E", "F", "R", "Q",
    ".", "0", "-", "/", "I", "J", "P",
  #else
    "A", "B", "C", "D", "E", "F", "G", "1", "2", "3",
    "H", "I", "J", "K", "L", "M", "N", "4", "5", "6",
    "O", "P", "Q", "R", "S", "T", "U", "7", "8", "9",
    "V", "W", "X", "Y", "Z", "(", ")", ".", "0", "-",
    "!", "@", "#", "%", "&", ",", ";", "*", "/", "+",
    "~", "`", "$","\\","\"", "'", ":", "_", "=", "?",
  #endif
};

// keyboard keys for second layout
static const char * const gcodeKeyABC[EDITOR_KEY_COUNT] = {
  "Prev", "Next", "Clear", "Send", "123", "Space", "Del", "Back",
  #if KB_COL_COUNT == LAYOUT_1_COL_COUNT
    "A", "B", "C", "D", "H", "I",
    "J", "K", "L", "N", "O", "P",
    ",", ";", ":", "Q", "U", "W",
    "+", "*", "?", "!", "#", "&",
  #elif KB_COL_COUNT == LAYOUT_2_COL_COUNT
    "A", "B", "C", "D", "H", "K", "L",
    ",", ";", ":", "N", "O", "U", "W",
    "+", "*", "?", "!", "#", "&", "$",
    "/", "=", "(", ")", "@", "_", "%",
  #else
    #if TERMINAL_KEYBOARD_LAYOUT == LAYOUT_QWERTY
      "!", "@", "#", "%", "&", "*", "(", ")", "-", "+",
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
      "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
      "A", "S", "D", "F", "G", "H", "J", "K", "L", ";",
      "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/",
      "~", "`", "$","\\","\"", "'", ":", "_", "=", "?",
    #elif TERMINAL_KEYBOARD_LAYOUT == LAYOUT_QWERTZ
      "!", "\"", "$", "%", "&", "/", "(", ")", "=", "?",
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
      "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P",
      "A", "S", "D", "F", "G", "H", "J", "K", "L", "@",
      "Y", "X", "C", "V", "B", "N", "M", ",", ".", "-",
      "|", ";", ":", "_", "#", "~", "+", "*", "'", "\\",
    #elif TERMINAL_KEYBOARD_LAYOUT == LAYOUT_AZERTY
      "#", "@", "~", "&", "(", ")", "_", "'", "\"", "%",
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
      "A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P",
      "Q", "S", "D", "F", "G", "H", "J", "K", "L", "M",
      "W", "X", "C", "V", "B", "N", ".", ",", ":", ";",
      "-", "+", "*", "\\", "|", "/", "?","!", "$", "=",
    #endif
  #endif
};

static const uint16_t fontSrcColor[3][3] = {
  // Gcode                  ACK                    Background
  {COLORSCHEME1_TERM_GCODE, COLORSCHEME1_TERM_ACK, COLORSCHEME1_TERM_BACK},  // Material Dark
  {COLORSCHEME2_TERM_GCODE, COLORSCHEME2_TERM_ACK, COLORSCHEME2_TERM_BACK},  // Material Light
  {COLORSCHEME3_TERM_GCODE, COLORSCHEME3_TERM_ACK, COLORSCHEME3_TERM_BACK},  // High Contrast
};

static KEYBOARD_DATA * keyboardData;
static TERMINAL_DATA * terminalData;
static char * terminalBuf;
static TERMINAL_WINDOW curView = KEYBOARD_VIEW;

static bool numpad =
  #ifdef KB_TYPE_QWERTY
    false;  // show qwerty as default for larger
  #else
    true;
  #endif

// ★ PLANE MILLING - Global variables (must be before menuKeyboardView)
typedef struct {
  float lastFinalDepth;           // Final depth from previous pass (-0.50mm)
  float X_lastPass[4];            // 4 corner X positions
  float Y_lastPass[4];            // 4 corner Y positions
  uint8_t passCount;              // Pass number (2, 3, ...)
  uint8_t isValid;                // Data valid flag (1=yes, 0=no)
  uint32_t savedTimestamp;        // When data was saved (Unix timestamp)
  uint8_t meshCompensationEnabled; // Mesh compensation enabled during save
} MILLING_PASS_DATA;

typedef struct {
  float X[4];
  float Y[4];
  uint8_t cornerCount;
} PLANE_CORNERS;

static PLANE_CORNERS planeCorners = {{0}, {0}, 0};  // Store 4 corners
static bool planeCornerReady = false;               // All 4 corners ready
static bool planeCornerCaptured[4] = {false, false, false, false};
static int8_t pendingPlaneCornerIndex = -1;
static MILLING_PASS_DATA lastPassData = {0};       // Persistent between passes
static bool useIncrementalMode = false;            // Mode selection
static bool gcodeNumpad = true;                    // Keyboard mode (numpad vs ABC)
static bool gcodeAblValidationPending = false;     // Waiting for live ABL result before auto-starting generated file
static bool gcodeAblValidationFailed = false;      // Live ABL failed, block any pending auto-start
static FILE_SOURCE gcodePendingAutoStartSource = FS_TFT_SD;
static char gcodePendingAutoStartFilename[50] = {0};

// ★ Forward declarations for plane milling functions
static void clearPlaneCornerState(void);
static int8_t getNextMissingPlaneCornerIndex(void);
static void updateDerivedPlaneCornersFromDiagonal(void);
static void syncMillingTotalDepthFromLastPass(void);
static void prepareNextPassPreview(void);
static void clearLastPassData(void);
static void loadLastPassData(void);
static void saveLastPassData(float depth, const PLANE_CORNERS * corners);
static float getMeshOffsetAtPosition(float x, float y);
static void calculateMeshOffsetsAtCorners(const PLANE_CORNERS * corners);
static void generatePlaneMillingGcode(const PLANE_CORNERS * corners, float totalDepth);
static void generatePlaneMillingGcode_Incremental(const PLANE_CORNERS * corners, float totalDepth, bool useLastPassDepth);

static inline void keyboardDrawButton(uint8_t index, uint8_t isPressed)
{
  if (index >= COUNT(editorKeyRect))
    return;

  // setup colors and button info
  #ifdef KEYBOARD_MATERIAL_THEME
    uint16_t fontColor = CTRL_FONT_COLOR;
    uint16_t bgColor = KEY_BG_COLOR;
    uint16_t lineColor = BAR_BORDER_COLOR;
    uint8_t lineWidth = 1;
    GUI_RECT rectBtn = {editorKeyRect[index].x0 + 3, editorKeyRect[index].y0 + 3,
                        editorKeyRect[index].x1 - 3, editorKeyRect[index].y1 - 3};

    switch (index)
    {
      case GKEY_SEND:
        bgColor = CTRL_SEND_BG_COLOR;
        break;

      case GKEY_BACK:
        bgColor = CTRL_BACK_BG_COLOR;
        break;

      case GKEY_ABC_123:
        bgColor = CTRL_ABC_BG_COLOR;
        break;

      default:
        if (index < GKEY_SEND)  // if pressed a key on text box
        {
          fontColor = TEXTBOX_FONT_COLOR;
          bgColor = CTRL_ABC_BG_COLOR;
        }
        else
        {
          fontColor = KEY_FONT_COLOR;
        }
        break;
    }

    BUTTON btn = {.fontColor  = fontColor,
                  .backColor  = bgColor,
                  .context    = (uint8_t *)((numpad) ? gcodeKey123[index] : gcodeKeyABC[index]),
                  .lineColor  = lineColor,
                  .lineWidth  = lineWidth,
                  .pBackColor = fontColor,
                  .pFontColor = bgColor,
                  .pLineColor = lineColor,
                  .radius     = BTN_ROUND_CORNER,
                  .rect       = rectBtn};

    setFontSize(index > GKEY_SEND ? FONT_SIZE_LARGE : FONT_SIZE_NORMAL);

    // draw button
    GUI_DrawButton(&btn, isPressed);

    if (index < GKEY_SEND)  // if key on text box, draw a status info on Send button area
    {
      char statusText[10];

      if (isPressed)  // if pressed key
      {
        if (index < GKEY_CLEAR)
        {
          keyboardData->gcodeIndex = (keyboardData->gcodeIndex + MAX_GCODE_COUNT + (2 * (index == GKEY_NEXT)) - 1) % MAX_GCODE_COUNT;
          sprintf(statusText, "%s %d/%d", gcodeKey123[index], keyboardData->gcodeIndex + 1, MAX_GCODE_COUNT);
        }
        else
        {
          strcpy(statusText, gcodeKey123[GKEY_CLEAR]);
        }
      }
      else  // if released key
      {
        strcpy(statusText, gcodeKey123[GKEY_SEND]);
      }

      rectBtn = (GUI_RECT){editorKeyRect[GKEY_SEND].x0 + 3, editorKeyRect[GKEY_SEND].y0 + 3,
                           editorKeyRect[GKEY_SEND].x1 - 3, editorKeyRect[GKEY_SEND].y1 - 3};

      fontColor = CTRL_FONT_COLOR;
      bgColor = CTRL_SEND_BG_COLOR;

      BUTTON btn2 = {.fontColor  = fontColor,
                     .backColor  = bgColor,
                     .context    = (uint8_t *) statusText,
                     .lineColor  = lineColor,
                     .lineWidth  = lineWidth,
                     .pBackColor = fontColor,
                     .pFontColor = bgColor,
                     .pLineColor = lineColor,
                     .radius     = BTN_ROUND_CORNER,
                     .rect       = rectBtn};

      // draw button
      GUI_DrawButton(&btn2, isPressed);
    }
  #else  // KEYBOARD_MATERIAL_THEME
    uint16_t fontColor;
    uint16_t bgColor;

    if (isPressed)
    {
      if (index > GKEY_BACK)  // if pressed a key on keyboard
      {
        fontColor = KEY_BG_COLOR;
        bgColor = KEY_FONT_COLOR;
      }
      else  // if pressed a key on send area or control bar
      {
        fontColor = BAR_BG_COLOR;
        bgColor = BAR_FONT_COLOR;
      }
    }
    else
    {
      if (index > GKEY_BACK)  // if pressed a key on keyboard
      {
        fontColor = KEY_FONT_COLOR;
        bgColor = KEY_BG_COLOR;
      }
      else  // if pressed a key on send area or control bar
      {
        fontColor = BAR_FONT_COLOR;
        bgColor = BAR_BG_COLOR;
      }
    }

    drawStandardValue(&editorKeyRect[index], VALUE_STRING, (numpad) ? gcodeKey123[index] : gcodeKeyABC[index],
                      (index > GKEY_SEND) ? FONT_SIZE_LARGE : FONT_SIZE_NORMAL, fontColor, bgColor, 1, true);

    if (index < GKEY_SEND)  // if key on text box, draw a status info on Send button area
    {
      char statusText[10];

      if (isPressed)  // if pressed key
      {
        if (index < GKEY_CLEAR)
        {
          keyboardData->gcodeIndex = (keyboardData->gcodeIndex + MAX_GCODE_COUNT + (2 * (index == GKEY_NEXT)) - 1) % MAX_GCODE_COUNT;
          sprintf(statusText, "%s %d/%d", gcodeKey123[index], keyboardData->gcodeIndex + 1, MAX_GCODE_COUNT);
        }
        else
        {
          strcpy(statusText, gcodeKey123[GKEY_CLEAR]);
        }

        fontColor = BAR_BG_COLOR;
        bgColor = BAR_FONT_COLOR;
      }
      else  // if released key
      {
        strcpy(statusText, gcodeKey123[GKEY_SEND]);

        fontColor = BAR_FONT_COLOR;
        bgColor = BAR_BG_COLOR;
      }

      drawStandardValue(&editorKeyRect[GKEY_SEND], VALUE_STRING, statusText, FONT_SIZE_NORMAL, fontColor, bgColor, 1, true);
    }
  #endif  // KEYBOARD_MATERIAL_THEME
}

static inline void drawGcodeText(char * gcode)
{
  drawStandardValue(&textBoxRect, VALUE_STRING, gcode, FONT_SIZE_NORMAL, TEXTBOX_FONT_COLOR, TEXTBOX_BG_COLOR, 1, true);
}

static inline void drawKeyboard(void)
{
  #ifndef KEYBOARD_MATERIAL_THEME
    GUI_SetColor(KEY_BORDER_COLOR);

    // draw vertical button borders
    for (int i = 0; i < (KB_COL_COUNT - 1); i++)
    {
      GUI_VLine(editorKeyRect[i + GKEY_BACK + 1].x1, editorAreaRect[1].y0, editorAreaRect[1].y1);
    }

    // draw horizontal button borders
    for (int i = 0; i < (KB_ROW_COUNT - 1); i++)
    {
      GUI_HLine(editorAreaRect[1].x0, editorKeyRect[(i * KB_COL_COUNT) + GKEY_BACK + 1].y1, editorAreaRect[1].x1);
    }
  #endif

  for (uint8_t i = GKEY_SEND; i < COUNT(gcodeKey123); i++)  // draw all the visible keys (text box keys are skipped)
  {
    keyboardDrawButton(i, false);
  }
}

static inline void keyboardDrawMenu(void)
{
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(editorKeyRect), editorKeyRect, keyboardDrawButton, NULL);

  // clear keyboard area
  GUI_SetBkColor(KB_BG_COLOR);
  GUI_ClearPrect(&editorAreaRect[1]);

  // clear bar area
  GUI_SetBkColor(BAR_BG_COLOR);
  GUI_ClearPrect(&editorAreaRect[0]);
  GUI_ClearPrect(&editorAreaRect[2]);

  #ifndef KEYBOARD_MATERIAL_THEME
    GUI_SetColor(BAR_BORDER_COLOR);

    // draw text box area shadow border
    GUI_DrawPrect(&textBoxRect);

    // draw bar area shadow border
    GUI_HLine(editorAreaRect[0].x0, editorAreaRect[0].y1 - 1, editorAreaRect[0].x1);  // last row of bar 1 area used for shadow border
    GUI_HLine(editorAreaRect[2].x0, editorAreaRect[2].y0, editorAreaRect[2].x1);      // first row of bar 2 area used for shadow border
  #endif

  GUI_SetTextMode(GUI_TEXTMODE_TRANS);

  // draw keyboard
  drawKeyboard();
}

static inline void menuKeyboardView(void)
{
  KEY_VALUES key_num = KEY_IDLE;
  uint8_t saveGcodeIndex = keyboardData->gcodeIndex;
  uint8_t nowIndex = 0;
  uint8_t lastIndex = 0xFF;  // trigger text box draw
  bool saveEnabled = true;
  CMD gcodeBuf = {'\0'};

  keyboardDrawMenu();

  while (curView == KEYBOARD_VIEW)
  {
    if (MENU_IS_NOT(menuTerminal))
      break;

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case GKEY_IDLE:
        break;

      case GKEY_PREV:
      case GKEY_NEXT:
        nowIndex = sprintf(gcodeBuf, keyboardData->gcodeTable[keyboardData->gcodeIndex]);  // load gcode from history table and update gcode size
        lastIndex = ~nowIndex;  // trigger text box redraw
        saveEnabled = false;
        break;

      case GKEY_CLEAR:
        nowIndex = 0;  // reset gcode size
        // ★ ALSO CLEAR PASS DATA: Reset to Pass 1
        clearLastPassData();
        planeCorners.cornerCount = 0;
        planeCornerReady = false;
        popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, "Pass data cleared - Ready for Pass 1");
        break;

      case GKEY_SEND:
        if (nowIndex)
        {
          if (saveEnabled == true)  // avoid saving again a gcode called from gcode history table
          {
            strcpy(keyboardData->gcodeTable[saveGcodeIndex], gcodeBuf);  // save gcode to history table
            saveGcodeIndex = (saveGcodeIndex + 1) % MAX_GCODE_COUNT;     // move to next save index in the gcode history table
          }

          strcpy(&gcodeBuf[nowIndex], "\n");
          handleCmd(gcodeBuf);
        }

        keyboardData->gcodeIndex = saveGcodeIndex;  // save and update gcode index
        curView = TERMINAL_VIEW;
        break;

      case GKEY_ABC_123:
        TOGGLE_BIT(numpad, 0);

        drawKeyboard();
        break;

      case GKEY_SPACE:
        if (nowIndex > 0 && nowIndex < CMD_MAX_SIZE - 2)  // -2 to leave space for '\n' and '\0' char
          gcodeBuf[nowIndex++] = ' ';
        break;

      case GKEY_DEL:
        nowIndex -= !(!nowIndex);
        break;

      case GKEY_BACK:
        CLOSE_MENU();
        break;

      default:
        if (nowIndex < CMD_MAX_SIZE - 2)  // -2 to leave space for '\n' and '\0' char
        {
          gcodeBuf[nowIndex++] = (numpad) ? gcodeKey123[key_num][0] : gcodeKeyABC[key_num][0];
          saveEnabled = true;
        }
        break;
    }

    if (lastIndex != nowIndex)
    {
      lastIndex = nowIndex;  // update gcode size
      gcodeBuf[nowIndex] = '\0';

      drawGcodeText(gcodeBuf);

      if (*gcodeBuf == '\0')  // text area empty
      {
        for (uint8_t i = 0; i < GKEY_SEND; i++)  // draw text box keys
        {
          keyboardDrawButton(i, false);
        }
      }
    }

    loopBackEnd();
  }

  // restore default
  GUI_RestoreColorDefault();
}

static inline void saveGcodeTerminalCache(const char * str, uint16_t strLen)
{
  uint16_t len = 0;

  if ((terminalData->bufTail + strLen) < terminalData->bufSize)
  {
    memcpy(terminalBuf + terminalData->bufTail, str, strLen);
    terminalData->bufTail += strLen;
  }
  else
  {
    len = (terminalData->bufTail + strLen) - terminalData->bufSize;
    memcpy(terminalBuf + terminalData->bufTail, str, (strLen - len));
    terminalData->bufTail = 0;
    memcpy(terminalBuf + terminalData->bufTail, str + (strLen - len), len);
    terminalData->bufTail += len;
  }
}

void terminalCache(const char * stream, uint16_t streamLen, SERIAL_PORT_INDEX portIndex, TERMINAL_SRC src)
{
  #ifdef TERMINAL_KEYBOARD_VIEW_SUPPRESS_ACK
    if (curView == KEYBOARD_VIEW)
      return;
  #endif

  char * srcId[SRC_TERMINAL_COUNT] = {"\5", "\6"};

  // copy string source identifier
  if (terminalData->lastSrc != src)
  {
    saveGcodeTerminalCache(srcId[src], 1);
    terminalData->lastSrc = src;
  }

  if (src == SRC_TERMINAL_GCODE)
  {
    if (serialPort[portIndex].id[0] != '\0')                // if not empty string
      saveGcodeTerminalCache(serialPort[portIndex].id, 1);  // serial port ID (e.g. "2" for SERIAL_PORT_2)

    saveGcodeTerminalCache(">>", 2);
  }

  saveGcodeTerminalCache(stream, streamLen);
}

// reverse lookup for source identifier
static TERMINAL_SRC getLastSrc(char * ptr)
{
  TERMINAL_SRC lastSrc = SRC_TERMINAL_COUNT;
  char * endPtr = (ptr + 1);

  // set end of search pointer
  if (endPtr > (terminalBuf + terminalData->bufSize))
    endPtr = terminalBuf;

  while (ptr != endPtr)
  {
    // check each byte for any source identifier
    if (ptr[0] == 0x5 || ptr[0] == 0x6)
    {
      lastSrc = ptr[0] - 0x5;

      break;
    }

    ptr--;

    // loop to end if reached starting point of buffer
    if (ptr < terminalBuf)
      ptr = terminalBuf + terminalData->bufSize;
  }

  if (lastSrc == SRC_TERMINAL_COUNT)  // if a source identifier is not found
  {
    // if terminal has a valid source, use it (it should be always that because
    // at least a message has been buffered if this function is invoked)
    if (terminalData->lastSrc != SRC_TERMINAL_COUNT)
      lastSrc = terminalData->lastSrc;
    else
      lastSrc = SRC_TERMINAL_ACK;  // otherwise, use ACK type as default source (it should never happen)
  }

  return lastSrc;
}

static inline void terminalDrawButton(uint8_t index, uint8_t isPressed)
{
  if (index >= TERM_KEY_COUNT)
    return;

  const char * terminalKey[] = {"<", ">", textSelect(itemToggle[infoSettings.terminal_ack].index), "Back"};

  #ifdef KEYBOARD_MATERIAL_THEME
    uint16_t fontcolor = KEY_FONT_COLOR;
    uint16_t bgcolor = KEY_BG_COLOR;
    GUI_RECT rectBtn = {terminalKeyRect[index].x0 + 3, terminalKeyRect[index].y0 + 3,
                        terminalKeyRect[index].x1 - 3, terminalKeyRect[index].y1 - 3};

    if (index == TERM_BACK)
    {
      fontcolor = CTRL_FONT_COLOR;
      bgcolor = CTRL_BACK_BG_COLOR;
    }

    BUTTON btn = {.fontColor  = fontcolor,
                  .backColor  = bgcolor,
                  .context    = (uint8_t *) terminalKey[index],
                  .lineColor  = bgcolor,
                  .lineWidth  = 0,
                  .pBackColor = fontcolor,
                  .pFontColor = bgcolor,
                  .pLineColor = fontcolor,
                  .radius     = BTN_ROUND_CORNER,
                  .rect       = rectBtn};

    setFontSize(FONT_SIZE_LARGE);
    GUI_DrawButton(&btn, isPressed);
    setFontSize(FONT_SIZE_NORMAL);
  #else
    uint16_t color;
    uint16_t bgColor;

    if (isPressed)
    {
      color = BAR_BG_COLOR;
      bgColor = BAR_FONT_COLOR;
    }
    else
    {
      color = BAR_FONT_COLOR;
      bgColor = BAR_BG_COLOR;
    }

    drawStandardValue(&terminalKeyRect[index], VALUE_STRING, terminalKey[index], FONT_SIZE_LARGE, color, bgColor, 1, true);
  #endif  // KEYBOARD_MATERIAL_THEME
}

static uint8_t terminalUpdatePageCount(void)
{
  if (terminalData->pageTail >= terminalData->pageHead)
    terminalData->pageCount = abs(terminalData->pageTail - terminalData->pageHead);
  else
    terminalData->pageCount = abs(terminalData->pageTail + (terminalData->maxPageCount - terminalData->pageHead));

  return terminalData->pageCount;
}

static inline void terminalDrawPageNumber(void)
{
  char tempstr[10];

  sprintf(tempstr, "%d/%d", (terminalData->pageCount + 1) - terminalData->pageIndex, terminalData->pageCount + 1);

  drawStandardValue(&terminalPageRect, VALUE_STRING, &tempstr, FONT_SIZE_LARGE, BAR_FONT_COLOR, BAR_BG_COLOR, 1, true);
}

static inline void terminalDrawMenu(void)
{
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(terminalKeyRect), terminalKeyRect, terminalDrawButton, NULL);

  // clear terminal area
  GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);
  GUI_ClearPrect(&terminalAreaRect[0]);

  // clear bar area
  GUI_SetBkColor(BAR_BG_COLOR);
  GUI_ClearPrect(&terminalAreaRect[1]);

  // draw bar area shadow border
  GUI_SetColor(BAR_BORDER_COLOR);
  GUI_HLine(terminalAreaRect[1].x0, terminalAreaRect[1].y0, terminalAreaRect[1].x1);  // first row of bar area used for shadow border

  // draw keyboard
  for (uint8_t i = 0; i < COUNT(terminalKeyRect); i++)
  {
    terminalDrawButton(i, false);
  }

  terminalDrawPageNumber();
}

static void menuTerminalView(void)
{
  KEY_VALUES key_num = KEY_IDLE;
  CHAR_INFO info;
  TERMINAL_SRC src = SRC_TERMINAL_ACK;  // default source
  uint8_t oldPageCount = 0;
  uint8_t oldPageIndex = 0;
  uint8_t pageTableIndex = 0;
  uint16_t bufIndex = 0;
  int16_t cursorX = terminalAreaRect[0].x0 + CURSOR_H_OFFSET;
  int16_t cursorY = terminalAreaRect[0].y0;

  terminalDrawMenu();

  while (curView == TERMINAL_VIEW)
  {
    if (MENU_IS_NOT(menuTerminal))
      break;

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case TERM_PAGE_UP:  // page up
        if (terminalData->pageIndex < terminalData->pageCount)
          terminalData->pageIndex++;
        break;

      case TERM_PAGE_DOWN:  // page down
        if (terminalData->pageIndex > 0)
          terminalData->pageIndex--;
        break;

      case TERM_TOGGLE_ACK:  // toggle ack in terminal
        TOGGLE_BIT(infoSettings.terminal_ack, 0);

        terminalDrawButton(TERM_TOGGLE_ACK, false);
        break;

      case TERM_BACK:  // back
        curView = KEYBOARD_VIEW;
        break;

      default:
        break;
    }

    // scroll a certain number of pages from the last page (tail)
    if (oldPageIndex != terminalData->pageIndex)
    {
      oldPageIndex = terminalData->pageIndex;

      if (terminalData->pageTail >= terminalData->pageIndex)
        pageTableIndex = terminalData->pageTail - terminalData->pageIndex;
      else
        pageTableIndex = terminalData->pageTail + (terminalData->maxPageCount - terminalData->pageIndex);

      bufIndex = terminalData->pageTable[pageTableIndex] - terminalBuf;
      src = getLastSrc(terminalData->pageTable[pageTableIndex]);

      cursorX = CURSOR_START_X;
      cursorY = terminalAreaRect[0].y0;

      GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);
      GUI_ClearPrect(&terminalAreaRect[0]);

      terminalDrawPageNumber();
    }

    while (bufIndex != terminalData->bufTail)
    {
      if (terminalData->pageHead != terminalData->pageTail)  // if more than one page is available
      {
        // check if first page (head) is out of page range
        if (terminalData->pageTable[terminalData->pageTail] - terminalBuf >= terminalData->bufTail ||
            (terminalData->pageTable[terminalData->pageHead] > terminalData->pageTable[terminalData->pageTail] &&
             terminalData->pageTable[terminalData->pageHead] - terminalBuf <= terminalData->bufTail))
        {
          // move to next first page and move on top of the loop and check again (with this block of code)
          terminalData->pageHead = (terminalData->pageHead + 1) % terminalData->maxPageCount;
          terminalUpdatePageCount();  // update page count

          break;
        }
      }

      // check if page index is out of page count (needed when page count is updated)
      if (terminalData->pageIndex > terminalData->pageCount)
      {
        // set index to first page in page range and move on top of the loop to select the new page
        // to display (with block of code "if (oldPageIndex != terminalData->pageIndex)")
        terminalData->pageIndex = terminalData->pageCount;

        break;
      }

      //
      // WARNING: In case the buffer contains multi character codes (e.g. requiring 2 or 3 bytes),
      //          a buffer overflow can occur in both the getCharacterInfo and GUI_DispOne functions in case the codes
      //          are not contiguous in the buffer (the first part was buffered in the tail and the last part in the head)
      //
      getCharacterInfo((uint8_t *)(terminalBuf + bufIndex), &info);

      if (info.bytes == 0)  // if '\0' is found, move to next byte in the buffer (avoiding an infinite loop due to info.bytes set to 0)
      {
        bufIndex = (bufIndex + 1) % terminalData->bufSize;

        break;
      }

      // check source identifier
      if (info.codePoint == 0x5 || info.codePoint == 0x6)
        src = info.codePoint - 0x5;

      // check next line
      if (cursorX + info.advanceWidth > terminalAreaRect[0].x1 ||
          (terminalBuf[bufIndex] == '\n' && cursorX != CURSOR_START_X))
      {
        cursorX = CURSOR_START_X;
        cursorY += info.pixelHeight;
      }

      if (terminalBuf[bufIndex] != '\n')
      {
        // check next page
        if (cursorY + info.pixelHeight > terminalAreaRect[0].y1)
        {
          if (terminalData->pageIndex != 0)  // do not scroll pages if we are not in the last page (tail)
            break;

          // move to next last page (new page) and save the page pointer
          terminalData->pageTail = (terminalData->pageTail + 1) % terminalData->maxPageCount;
          terminalData->pageTable[terminalData->pageTail] = terminalBuf + bufIndex;

          // move to next first page if we reached the maximun page count
          if (terminalData->pageTail == terminalData->pageHead && (terminalData->pageCount + 1) == terminalData->maxPageCount)
            terminalData->pageHead = (terminalData->pageHead + 1) % terminalData->maxPageCount;

          terminalUpdatePageCount();  // update page count

          cursorX = CURSOR_START_X;
          cursorY = terminalAreaRect[0].y0;

          GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);
          GUI_ClearPrect(&terminalAreaRect[0]);
        }

        GUI_SetColor(fontSrcColor[infoSettings.terminal_color_scheme][src]);
        GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);

        GUI_DispOne(cursorX, cursorY, &info);
        cursorX += info.advanceWidth;
      }

      if (oldPageCount != terminalData->pageCount)
      {
        oldPageCount = terminalData->pageCount;

        terminalDrawPageNumber();
      }

      bufIndex += info.bytes;

      if (bufIndex >= terminalData->bufSize)
        bufIndex = 0;
    }

    loopBackEnd();
  }

  terminalBuf[0] = '\0';
  terminalData->pageCount = 0;
  terminalData->pageHead = 0;
  terminalData->pageTail = 0;
  terminalData->pageIndex = 0;
  terminalData->bufTail = 0;
  terminalData->lastSrc = SRC_TERMINAL_COUNT;

  // restore default
  GUI_RestoreColorDefault();
}

void menuTerminal(void)
{
  KEYBOARD_DATA keybData = {{'\0'}, 0};
  TERMINAL_DATA termData = {{terminalBuf}, MAX_PAGE_COUNT, 0, 0, 0, 0, MAX_TERMINAL_BUF_SIZE, 0, SRC_TERMINAL_COUNT};

  if (isPrinting() || isPrintingFromOnboard())  // display only 1 page if printing
  {
    termData.bufSize = (LCD_WIDTH / BYTE_WIDTH * LCD_HEIGHT / BYTE_HEIGHT);
    termData.maxPageCount = 1;
  }

  char termBuf[termData.bufSize];

  for (uint8_t i = 0; i < termData.maxPageCount; i++)
  {
    termData.pageTable[i] = termBuf;
  }

  keyboardData = &keybData;
  terminalData = &termData;
  terminalBuf = termBuf;
  curView = KEYBOARD_VIEW;

  while (MENU_IS(menuTerminal))
  {
    (curView == KEYBOARD_VIEW) ?  menuKeyboardView() : menuTerminalView();
  }
}

// =============================================================================
// GCODE KEYBOARD 4x4 FOR menuGcode (SEPARATE SYSTEM)
// =============================================================================

// GCode keyboard definitions (4x4 compact layout)
#define GCODE_KB_COL_COUNT  4
#define GCODE_KB_ROW_COUNT  4
#define GCODE_KB_AREA_WIDTH ((LCD_WIDTH * 3) / 5)
#define GCODE_KB_KEY_COUNT (4 + (GCODE_KB_COL_COUNT * GCODE_KB_ROW_COUNT) + 4)  // control keys + keyboard keys + bottom control
#define GCODE_KB_KEY_WIDTH  (GCODE_KB_AREA_WIDTH / GCODE_KB_COL_COUNT)
#define GCODE_KB_KEY_HEIGHT ROW_HEIGHT
#define GCODE_POS_REFRESH_TIME 500
#define GCODE_XYZ_STATUS    "X:%.2f  Y:%.2f  Z:%.2f"
#define AUTO_MILLING_DEPTH_STEP 0.20f
#define MIN_MILLING_TOTAL_DEPTH 0.01f
#define DEFAULT_MILLING_TOTAL_DEPTH 0.30f
#define IC_TEMPLATE_FILE "ic_depth.cfg"
#define IC_TEMPLATE_MAX_COUNT 256
#define IC_TEMPLATE_FOLDER_LEN 16
#define IC_TEMPLATE_NAME_LEN 32
#define IC_TEMPLATE_PAGE_SIZE 5
#define IC_TEMPLATE_KEY_COUNT (IC_TEMPLATE_PAGE_SIZE + 3)
#define IC_TEMPLATE_ACTION_KEY_COUNT 4

#define GCODE_ACCENT_BG_COLOR        MAT_DARKSLATE
#define GCODE_ACCENT_BORDER_COLOR    MAT_SLATE
#define GCODE_STATUS_BG_COLOR        WHITE
#define GCODE_STATUS_FONT_COLOR      BLACK
#define GCODE_STATUS_BORDER_COLOR    DARKGRAY
#define GCODE_PREVIEW_BG_COLOR       WHITE
#define GCODE_PREVIEW_FONT_COLOR     DARKGRAY
#define GCODE_PREVIEW_BORDER_COLOR   WHITE
#define GCODE_PREVIEW_TITLE_BG_COLOR MAT_DARKSLATE
#define GCODE_PREVIEW_TITLE_FONT_COLOR WHITE
#define GCODE_PREVIEW_BOARD_COLOR    MAT_SLATE
#define GCODE_PREVIEW_BOARD_FONT_COLOR WHITE
#define GCODE_PREVIEW_CORNER_IDLE_COLOR MAT_GREEN
#define GCODE_PREVIEW_CORNER_WAIT_COLOR MAT_ORANGE
#define GCODE_PREVIEW_CORNER_DONE_COLOR MAT_RED
#define GCODE_PREVIEW_CORNER_AUTO_COLOR MAT_CYAN
#define GCODE_PREVIEW_READY_COLOR    MAT_GREEN
#define GCODE_PANEL_BG_COLOR         WHITE
#define GCODE_PARAM_BG_COLOR         GCODE_ACCENT_BG_COLOR
#define GCODE_PARAM_FONT_COLOR       WHITE
#define GCODE_BUTTON_RADIUS          6
#define GCODE_PREVIEW_BUTTON_RADIUS  10
#define GCODE_BUTTON_GAP_X           3
#define GCODE_BUTTON_GAP_Y           3

// GCode keyboard key IDs
typedef enum
{
  GKEY_G_CLEAR = 0,
  GKEY_G_SEND,
  GKEY_G_ABC_123,
  GKEY_G_BACK,
  // Special function keys (row 1)
  GKEY_G_X_AXIS = 4,
  GKEY_G_Y_AXIS,
  GKEY_G_Z_AXIS, 
  GKEY_G_STEP,
  // Workflow action row under X/Y/Z/Step
  GKEY_G_GO_HOME,
  GKEY_G_GO_HOME_FILL,
  GKEY_G_SET_IC_ORIGIN,
  GKEY_G_SET_IC_ORIGIN_FILL,
  GKEY_G_LIGHT = 19,
  GKEY_G_SPACE = GCODE_KB_KEY_COUNT - 4,
  GKEY_G_DEL,
  GKEY_G_ENTER,
  GKEY_G_EXIT,
  GKEY_G_IC_TOUCH,
  GKEY_G_CORNER_1_TOUCH,
  GKEY_G_CORNER_2_TOUCH,
  GKEY_G_CORNER_3_TOUCH,
  GKEY_G_CORNER_4_TOUCH,
} GCODE_KEY_VALUES;

// GCode keyboard rectangles
static const GUI_RECT gcodeKeyRect[GCODE_KB_KEY_COUNT] = {
  // Top control row (4 keys)
  {0 * (LCD_WIDTH / 4), 0, 1 * (LCD_WIDTH / 4), ROW_HEIGHT},                    // Clear
  {1 * (LCD_WIDTH / 4), 0, 2 * (LCD_WIDTH / 4), ROW_HEIGHT},                    // Send
  {2 * (LCD_WIDTH / 4), 0, 3 * (LCD_WIDTH / 4), ROW_HEIGHT},                    // ABC/123
  {3 * (LCD_WIDTH / 4), 0, 4 * (LCD_WIDTH / 4), ROW_HEIGHT},                    // Back
  
  // 4x4 keyboard matrix
  // Row 1
  {0 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 0 * GCODE_KB_KEY_HEIGHT, 1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT},
  {1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 0 * GCODE_KB_KEY_HEIGHT, 2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT},
  {2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 0 * GCODE_KB_KEY_HEIGHT, 3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT},
  {3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 0 * GCODE_KB_KEY_HEIGHT, 4 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT},
  
  // Row 2  
  {0 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT, 1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT},
  {1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT, 2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT},
  {2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT, 3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT},
  {3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 1 * GCODE_KB_KEY_HEIGHT, 4 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT},
  
  // Row 3
  {0 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT, 1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT},
  {1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT, 2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT},
  {2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT, 3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT},
  {3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 2 * GCODE_KB_KEY_HEIGHT, 4 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT},
  
  // Row 4
  {0 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT, 1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 4 * GCODE_KB_KEY_HEIGHT},
  {1 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT, 2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 4 * GCODE_KB_KEY_HEIGHT},
  {2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT, 3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 4 * GCODE_KB_KEY_HEIGHT},
  {3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 3 * GCODE_KB_KEY_HEIGHT, 4 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + 4 * GCODE_KB_KEY_HEIGHT},
  
  // Bottom control row (4 keys)
  {0 * (LCD_WIDTH / 4), LCD_HEIGHT - ROW_HEIGHT, 1 * (LCD_WIDTH / 4), LCD_HEIGHT},  // Space
  {1 * (LCD_WIDTH / 4), LCD_HEIGHT - ROW_HEIGHT, 2 * (LCD_WIDTH / 4), LCD_HEIGHT},  // Del
  {2 * (LCD_WIDTH / 4), LCD_HEIGHT - ROW_HEIGHT, 3 * (LCD_WIDTH / 4), LCD_HEIGHT},  // Enter
  {3 * (LCD_WIDTH / 4), LCD_HEIGHT - ROW_HEIGHT, 4 * (LCD_WIDTH / 4), LCD_HEIGHT},  // Exit
};

// GCode keyboard layouts (4x4)
static const char * const gcodeKeyLayout123[GCODE_KB_KEY_COUNT] = {
  "Bỏ chọn", "Bắt đầu", "Cân bàn", "Back",
  "X", "Y", "Z", "Step",
  "Về Gốc", "", "Gốc IC", "",
  "Tổng độ sâu", "", "", "",
  "-", "0", "+", "Đèn",
  "", "", "", "Tệp"
};

static const char * const gcodeKeyLayoutABC[GCODE_KB_KEY_COUNT] = {
  "Bỏ chọn", "Bắt đầu", "Cân bàn", "Back",
  "X", "Y", "Z", "Step",
  "Về Gốc", "", "Gốc IC", "",
  "Tổng độ sâu", "", "", "",
  "-", "0", "+", "Đèn",
  "", "", "", "Tệp"
};

// Struct to store current position from M114 response
typedef struct
{
  float X;
  float Y;
  float Z;
  uint32_t lastQueryTime;
  bool isValid;
} GCODE_POSITION;

typedef struct
{
  char folder[IC_TEMPLATE_FOLDER_LEN];
  char name[IC_TEMPLATE_NAME_LEN];
  float totalDepth;
} IC_DEPTH_TEMPLATE;

typedef enum
{
  IC_TEMPLATE_ITEM_NONE = 0,
  IC_TEMPLATE_ITEM_FOLDER,
  IC_TEMPLATE_ITEM_TEMPLATE,
} IC_TEMPLATE_ITEM_TYPE;

static const GUI_RECT icTemplateKeyRect[IC_TEMPLATE_KEY_COUNT] = {
  {0, 0 * ROW_HEIGHT, LCD_WIDTH, 1 * ROW_HEIGHT},
  {0, 1 * ROW_HEIGHT, LCD_WIDTH, 2 * ROW_HEIGHT},
  {0, 2 * ROW_HEIGHT, LCD_WIDTH, 3 * ROW_HEIGHT},
  {0, 3 * ROW_HEIGHT, LCD_WIDTH, 4 * ROW_HEIGHT},
  {0, 4 * ROW_HEIGHT, LCD_WIDTH, 5 * ROW_HEIGHT},
  {0 * (LCD_WIDTH / 3), LCD_HEIGHT - ROW_HEIGHT, 1 * (LCD_WIDTH / 3), LCD_HEIGHT},
  {1 * (LCD_WIDTH / 3), LCD_HEIGHT - ROW_HEIGHT, 2 * (LCD_WIDTH / 3), LCD_HEIGHT},
  {2 * (LCD_WIDTH / 3), LCD_HEIGHT - ROW_HEIGHT, 3 * (LCD_WIDTH / 3), LCD_HEIGHT},
};

static const GUI_RECT icTemplateActionKeyRect[IC_TEMPLATE_ACTION_KEY_COUNT] = {
  {0, 0 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT), LCD_WIDTH, 1 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT)},
  {0, 1 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT), LCD_WIDTH, 2 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT)},
  {0, 2 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT), LCD_WIDTH, 3 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT)},
  {0, 3 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT), LCD_WIDTH, 4 * (LCD_HEIGHT / IC_TEMPLATE_ACTION_KEY_COUNT)},
};

// GCode keyboard state
static CMD gcodeBuffer = {'\0'};
static uint8_t gcodeBufferIndex = 0;
static GCODE_POSITION gcodePosition = {0, 0, 0, 0, false};  // Current position from M114
static char gcodeLastPositionStatus[48] = {'\0'};
static bool gcodePositionStatusDirty = true;
static bool gcodeHomeReady = false;
static bool gcodeLightOn = false;
static bool m114_save_corner = false;  // Flag: save corner when M114 response is received
static IC_DEPTH_TEMPLATE icTemplates[IC_TEMPLATE_MAX_COUNT];
static uint8_t icTemplateCount = 0;
static uint8_t icTemplatePage = 0;
static uint8_t icTemplateActionIndex = 0;
static char icTemplateCurrentFolder[IC_TEMPLATE_FOLDER_LEN] = {'\0'};
static int8_t selectedIcTemplateIndex = -1;
static bool icTemplatesLoaded = false;
static float millingTotalDepth = DEFAULT_MILLING_TOTAL_DEPTH;  // Total milling depth (mm) - CONFIGURABLE
static float startDepth = -AUTO_MILLING_DEPTH_STEP;  // Automatic first-cut step kept for internal multi-pass generation

// Additional mesh compensation flags
static bool meshCompensationMode = true;      // Flag: apply mesh offsets?
static float meshOffsetAtCorners[4] = {0};    // Mesh offset at each corner

// G-code file creation and display
static char gcodeCommands[100][CMD_MAX_SIZE];  // Array to store individual G-code commands (chunk buffer - 100 max)
static uint8_t gcodeCommandCount = 0;        // Number of G-code commands in current chunk
static uint8_t gcodeDisplayStart = 0;        // Starting index for display scroll

// Streaming file variables
static FIL gcodeFile;                        // Currently open G-code file
static bool gcodeFileOpen = false;           // Is file currently open?
static uint32_t gcodeTotalCount = 0;         // Total commands written so far (across all chunks)
static char gcodeFilename[50] = {0};         // Current filename
static char gcodeSavedFilenames[16][50] = {{0}};
static uint8_t gcodeSavedFileCount = 0;
static GUI_RECT gcodeMenuRect[GCODE_KB_KEY_COUNT + 5];

#define GCODE_CHUNK_SIZE 100  // Write 100 commands per chunk to SD
#define GCODE_MILLING_FILE_LIMIT 16

static inline void addGcodeCommand(const char * command);
static inline bool flushGcodeChunk(void);  // Flush chunk to SD card
static float getMillingTotalDepth(void);   // Forward declaration for milling depth
static float getStartDepth(void);          // Forward declaration for start depth
static void setMillingTotalDepth(float depth);
static void gcodeSetCustomMillingDepth(float depth);
static const char * icTemplateGetSelectedName(void);
static void icTemplateLoadFile(void);
static bool icTemplateWriteFile(void);
static bool icTemplateSaveCurrentDepth(const char * name);
static bool icTemplateNameInputPopup(void);
static bool icTemplateListPopup(void);
static bool icTemplateActionPopup(uint8_t templateIndex);
static void icTemplateTrimLine(char * line);
static bool autoStartGeneratedGcodeFile(FILE_SOURCE source, const char * filename);
static void gcodeBuildMillingFilename(uint8_t passNumber, char * filename, uint16_t filenameSize);
static void gcodeTrackSavedFilename(const char * filename);
static void gcodeUntrackSavedFilename(const char * filename);
static bool gcodeDeleteSavedFiles(uint8_t * deletedCount, uint8_t * failedCount);
static bool gcodeDeleteSavedFile(const char * filename);
static void terminalCopyString(char * dest, const char * src, uint16_t size);
static inline void gcodeKeyboardDrawMenu(void);
static void gcodeNormalizeMoveStepIndex(void);
static void gcodeCycleMoveStepIndex(void);
static float gcodeGetCurrentMoveStep(void);

static void gcodeGetStatusRects(GUI_RECT * statusArea, GUI_RECT * previewRect, GUI_RECT * infoRect);
static void gcodeGetPreviewBoardRect(const GUI_RECT * area, GUI_RECT * boardRect);
static void gcodeGetPreviewCornerRects(const GUI_RECT * area, GUI_RECT * padRect);

// GCode keyboard areas
static const GUI_RECT gcodeKeyboardAreaRect[3] = {
  {0, 0, LCD_WIDTH, ROW_HEIGHT},                                                    // Top control area
  {0, ROW_HEIGHT, GCODE_KB_AREA_WIDTH, ROW_HEIGHT + (GCODE_KB_ROW_COUNT * GCODE_KB_KEY_HEIGHT)},  // Keyboard area (60% width)
  {0, LCD_HEIGHT - ROW_HEIGHT, LCD_WIDTH, LCD_HEIGHT}                              // Bottom control area
};

static const GUI_RECT gcodePositionRect = {
  0, LCD_HEIGHT - ROW_HEIGHT, 3 * (LCD_WIDTH / 4), LCD_HEIGHT};

static inline bool gcodeIsHiddenWorkflowButton(uint8_t index)
{
  return index == GKEY_G_GO_HOME_FILL ||
         index == GKEY_G_SET_IC_ORIGIN_FILL ||
         index == GKEY_G_SPACE ||
         index == GKEY_G_DEL ||
         index == GKEY_G_ENTER ||
         index == 13 || index == 14;
}

static inline GUI_RECT gcodeInsetRect(GUI_RECT rect, int16_t insetX, int16_t insetY)
{
  rect.x0 += insetX;
  rect.x1 -= insetX;
  rect.y0 += insetY;
  rect.y1 -= insetY;
  return rect;
}

static inline GUI_RECT gcodeGetButtonRect(uint8_t index)
{
  GUI_RECT rect;

  if (index == GKEY_G_CLEAR)
    rect = gcodeKeyRect[GKEY_G_EXIT];
  else if (index == GKEY_G_EXIT)
    rect = gcodeKeyRect[GKEY_G_CLEAR];
  else if (index == GKEY_G_GO_HOME)
    rect = (GUI_RECT){0, ROW_HEIGHT + GCODE_KB_KEY_HEIGHT, 2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + (2 * GCODE_KB_KEY_HEIGHT)};
  else if (index == GKEY_G_SET_IC_ORIGIN)
    rect = (GUI_RECT){2 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + GCODE_KB_KEY_HEIGHT, GCODE_KB_AREA_WIDTH, ROW_HEIGHT + (2 * GCODE_KB_KEY_HEIGHT)};
  else if (index == 12)
    rect = (GUI_RECT){0, ROW_HEIGHT + (2 * GCODE_KB_KEY_HEIGHT), 3 * GCODE_KB_KEY_WIDTH, ROW_HEIGHT + (3 * GCODE_KB_KEY_HEIGHT)};
  else
    rect = gcodeKeyRect[index];

  return gcodeInsetRect(rect, GCODE_BUTTON_GAP_X, GCODE_BUTTON_GAP_Y);
}

static void gcodeNormalizeMoveStepIndex(void)
{
  if (gcode_item_moveLen_index > 2)
    gcode_item_moveLen_index = 1;
}

static void gcodeCycleMoveStepIndex(void)
{
  gcodeNormalizeMoveStepIndex();
  gcode_item_moveLen_index = (gcode_item_moveLen_index + 1) % 3;
}

static float gcodeGetCurrentMoveStep(void)
{
  gcodeNormalizeMoveStepIndex();
  return moveLenSteps[gcode_item_moveLen_index];
}

static inline void gcodeGetButtonColors(uint8_t index, bool isPressed, bool isSpecialKey, uint16_t * fontColor, uint16_t * bgColor)
{
  *fontColor = WHITE;
  *bgColor = GCODE_ACCENT_BG_COLOR;

  switch (index)
  {
    case GKEY_G_CLEAR:
      *bgColor = GCODE_ACCENT_BG_COLOR;
      break;

    case GKEY_G_SEND:
      *bgColor = MAT_GREEN;
      break;

    case GKEY_G_ABC_123:
      *bgColor = GCODE_ACCENT_BG_COLOR;
      *fontColor = WHITE;
      break;

    case GKEY_G_BACK:
      *bgColor = GCODE_ACCENT_BG_COLOR;
      break;

    case GKEY_G_X_AXIS:
      *bgColor = isSpecialKey ? MAT_RED : GCODE_ACCENT_BG_COLOR;
      break;

    case GKEY_G_Y_AXIS:
      *bgColor = isSpecialKey ? MAT_RED : GCODE_ACCENT_BG_COLOR;
      break;

    case GKEY_G_Z_AXIS:
      *bgColor = isSpecialKey ? MAT_RED : GCODE_ACCENT_BG_COLOR;
      break;

    case GKEY_G_STEP:
    case GKEY_G_GO_HOME:
    case GKEY_G_SET_IC_ORIGIN:
      *bgColor = GCODE_ACCENT_BG_COLOR;
      *fontColor = WHITE;
      break;

    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
      *bgColor = GCODE_PARAM_BG_COLOR;
      *fontColor = GCODE_PARAM_FONT_COLOR;
      break;

    case GKEY_G_LIGHT:
      *bgColor = gcodeLightOn ? MAT_GREEN : MAT_DARKGRAY;
      *fontColor = WHITE;
      break;

    case GKEY_G_EXIT:
      *bgColor = GCODE_ACCENT_BG_COLOR;
      break;

    default:
      *bgColor = GCODE_ACCENT_BG_COLOR;
      *fontColor = WHITE;
      break;
  }

  if (isPressed)
  {
    uint16_t tmpColor = *fontColor;
    *fontColor = WHITE;
    if (*bgColor == WHITE || *bgColor == MAT_LOWWHITE || *bgColor == MAT_SILVER || *bgColor == MAT_CYAN || *bgColor == MAT_ORANGE)
      *fontColor = DARKGRAY;
    *bgColor = tmpColor == WHITE ? GCODE_ACCENT_BORDER_COLOR : tmpColor;
  }
}

static inline void gcodeDrawPositionStatus(void)
{
  char tempstr[48];

  sprintf(tempstr, GCODE_XYZ_STATUS,
          coordinateGetAxisActual(X_AXIS),
          coordinateGetAxisActual(Y_AXIS),
          coordinateGetAxisActual(Z_AXIS));

  if (!gcodePositionStatusDirty && strcmp(tempstr, gcodeLastPositionStatus) == 0)
    return;

  GUI_SetBkColor(GCODE_STATUS_BG_COLOR);
  GUI_ClearPrect(&gcodePositionRect);

  drawStandardValue(&gcodePositionRect, VALUE_STRING, tempstr, FONT_SIZE_NORMAL,
                    GCODE_STATUS_FONT_COLOR, GCODE_STATUS_BG_COLOR, 1, true);

  strcpy(gcodeLastPositionStatus, tempstr);
  gcodePositionStatusDirty = false;
}

static inline void gcodeDrawButton(uint8_t index, uint8_t isPressed)
{
  if (index >= GCODE_KB_KEY_COUNT)
    return;

  if (gcodeIsHiddenWorkflowButton(index))
    return;

  if (index >= GKEY_G_SPACE && index < GKEY_G_EXIT)
    return;

  uint16_t fontColor, bgColor;
  uint16_t pressedFontColor, pressedBgColor;
  const char * text = (gcodeNumpad) ? gcodeKeyLayout123[index] : gcodeKeyLayoutABC[index];
  
  // Handle special display for Step button and current axis
  char tempText[10];
  if (index == GKEY_G_STEP)
  {
    float stepValue = gcodeGetCurrentMoveStep();

    if (stepValue < 0.1f)
      sprintf(tempText, "X%.2f", stepValue);
    else if (stepValue < 1.0f)
      sprintf(tempText, "X%.1f", stepValue);
    else
      sprintf(tempText, "X%.0f", stepValue);

    text = tempText;
  }
  else if (index == GKEY_G_ABC_123)
  {
    text = "Mẫu IC";
  }
  else if (index == 12)
  {
    text = "Tổng độ sâu";
  }
  else if (index == 15)
  {
    text = "Lưu";
  }
  else if (index == 17)
  {
    sprintf(tempText, "%.2f", getMillingTotalDepth());
    text = tempText;
  }
  // Set colors based on button type and state
  bool isSpecialKey = (index == GKEY_G_X_AXIS && gcode_nowAxis == X_AXIS) ||
                      (index == GKEY_G_Y_AXIS && gcode_nowAxis == Y_AXIS) ||
                      (index == GKEY_G_Z_AXIS && gcode_nowAxis == Z_AXIS);

  gcodeGetButtonColors(index, false, isSpecialKey, &fontColor, &bgColor);
  gcodeGetButtonColors(index, true, isSpecialKey, &pressedFontColor, &pressedBgColor);

  setFontSize(FONT_SIZE_NORMAL);

  BUTTON btn = {
    .rect = gcodeGetButtonRect(index),
    .context = (const uint8_t *)text,
    .radius = GCODE_BUTTON_RADIUS,
    .lineWidth = 0,
    .lineColor = bgColor,
    .fontColor = fontColor,
    .backColor = bgColor,
    .pLineColor = pressedBgColor,
    .pFontColor = pressedFontColor,
    .pBackColor = pressedBgColor,
  };

  GUI_DrawButton(&btn, isPressed);
}

static inline void gcodeDrawKeyboard(void)
{
  for (uint8_t i = 0; i < GCODE_KB_KEY_COUNT; i++)
  {
    gcodeDrawButton(i, false);
  }

  gcodeDrawPositionStatus();
}

// ============================================================================
// PLANE LEVELING FUNCTIONS
// ============================================================================

static float getMinimumMillingTotalDepth(void)
{
  return MIN_MILLING_TOTAL_DEPTH;
}

// Set the total milling depth (configurable parameter)
// depth: Absolute final milling depth in mm from the original surface.
static void setMillingTotalDepth(float depth)
{
  float minDepth = getMinimumMillingTotalDepth();

  if (depth >= minDepth && depth <= 1.0f)
  {
    millingTotalDepth = depth;
  }
}

static void gcodeSetCustomMillingDepth(float depth)
{
  selectedIcTemplateIndex = -1;
  setMillingTotalDepth(depth);
}

static const char * icTemplateGetSelectedName(void)
{
  if (selectedIcTemplateIndex >= 0 && selectedIcTemplateIndex < icTemplateCount)
    return icTemplates[selectedIcTemplateIndex].name;

  return "Tùy chỉnh";
}

static bool icTemplateFolderExistsBefore(const char * folder, uint8_t beforeIndex)
{
  if (folder == NULL || folder[0] == '\0')
    return false;

  for (uint8_t i = 0; i < beforeIndex && i < icTemplateCount; i++)
  {
    if (strcmp(icTemplates[i].folder, folder) == 0)
      return true;
  }

  return false;
}

static bool icTemplateParsePath(const char * path, char * folder, uint16_t folderSize, char * name, uint16_t nameSize)
{
  const char * slash;

  if (folder == NULL || folderSize == 0 || name == NULL || nameSize == 0)
    return false;

  folder[0] = '\0';
  name[0] = '\0';

  if (path == NULL || path[0] == '\0')
    return false;

  slash = strrchr(path, '/');

  if (slash != NULL && slash > path && slash[1] != '\0')
  {
    uint16_t folderLen = slash - path;

    if (folderLen >= folderSize)
      folderLen = folderSize - 1;

    memcpy(folder, path, folderLen);
    folder[folderLen] = '\0';
    terminalCopyString(name, slash + 1, nameSize);
  }
  else
  {
    terminalCopyString(folder, icTemplateCurrentFolder, folderSize);
    terminalCopyString(name, path, nameSize);
  }

  icTemplateTrimLine(folder);
  icTemplateTrimLine(name);

  return name[0] != '\0';
}

static void icTemplateTrimLine(char * line)
{
  uint16_t len;

  if (line == NULL)
    return;

  len = strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t'))
    line[--len] = '\0';
}

static void icTemplateParseLine(char * line)
{
  char * separator;
  char folder[IC_TEMPLATE_FOLDER_LEN];
  char name[IC_TEMPLATE_NAME_LEN];
  float depth;

  if (icTemplateCount >= IC_TEMPLATE_MAX_COUNT)
    return;

  icTemplateTrimLine(line);
  if (line[0] == '\0' || line[0] == '#')
    return;

  separator = strchr(line, '=');
  if (separator == NULL)
    return;

  *separator = '\0';
  icTemplateTrimLine(line);
  depth = strtof(separator + 1, NULL);

  if (!icTemplateParsePath(line, folder, sizeof(folder), name, sizeof(name)) ||
      depth < MIN_MILLING_TOTAL_DEPTH || depth > 1.0f)
    return;

  terminalCopyString(icTemplates[icTemplateCount].folder, folder, sizeof(icTemplates[icTemplateCount].folder));
  terminalCopyString(icTemplates[icTemplateCount].name, name, sizeof(icTemplates[icTemplateCount].name));
  icTemplates[icTemplateCount].totalDepth = depth;
  icTemplateCount++;
}

static void icTemplateLoadFile(void)
{
  FIL file;
  char line[80];
  uint8_t lineIndex = 0;
  UINT br = 0;
  char ch;

  icTemplateCount = 0;
  selectedIcTemplateIndex = -1;
  icTemplatesLoaded = true;

  if (!mountFS())
    return;

  if (f_open(&file, IC_TEMPLATE_FILE, FA_READ) != FR_OK)
    return;

  while (icTemplateCount < IC_TEMPLATE_MAX_COUNT &&
         f_read(&file, &ch, 1, &br) == FR_OK &&
         br == 1)
  {
    if (ch == '\n')
    {
      line[lineIndex] = '\0';
      icTemplateParseLine(line);
      lineIndex = 0;
    }
    else if (lineIndex < sizeof(line) - 1)
    {
      line[lineIndex++] = ch;
    }
  }

  if (lineIndex > 0 && icTemplateCount < IC_TEMPLATE_MAX_COUNT)
  {
    line[lineIndex] = '\0';
    icTemplateParseLine(line);
  }

  f_close(&file);
}

static bool icTemplateWriteFile(void)
{
  FIL file;
  FRESULT result;

  if (!mountFS())
    return false;

  result = f_open(&file, IC_TEMPLATE_FILE, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK)
    return false;

  for (uint8_t i = 0; i < icTemplateCount; i++)
  {
    char line[80];
    UINT written = 0;
    uint16_t len;

    if (icTemplates[i].folder[0] != '\0')
      snprintf(line, sizeof(line), "%s/%s=%.2f\n", icTemplates[i].folder, icTemplates[i].name, icTemplates[i].totalDepth);
    else
      snprintf(line, sizeof(line), "%s=%.2f\n", icTemplates[i].name, icTemplates[i].totalDepth);

    len = strlen(line);

    if (f_write(&file, line, len, &written) != FR_OK || written != len)
    {
      f_close(&file);
      return false;
    }
  }

  return f_close(&file) == FR_OK;
}

static bool icTemplateSaveCurrentDepth(const char * name)
{
  char folder[IC_TEMPLATE_FOLDER_LEN];
  char itemName[IC_TEMPLATE_NAME_LEN];

  if (!icTemplateParsePath(name, folder, sizeof(folder), itemName, sizeof(itemName)))
    return false;

  if (!icTemplatesLoaded)
    icTemplateLoadFile();

  for (uint8_t i = 0; i < icTemplateCount; i++)
  {
    if (strcmp(icTemplates[i].folder, folder) == 0 && strcmp(icTemplates[i].name, itemName) == 0)
    {
      icTemplates[i].totalDepth = getMillingTotalDepth();
      selectedIcTemplateIndex = i;
      return icTemplateWriteFile();
    }
  }

  if (icTemplateCount >= IC_TEMPLATE_MAX_COUNT)
    return false;

  terminalCopyString(icTemplates[icTemplateCount].folder, folder, sizeof(icTemplates[icTemplateCount].folder));
  terminalCopyString(icTemplates[icTemplateCount].name, itemName, sizeof(icTemplates[icTemplateCount].name));
  icTemplates[icTemplateCount].totalDepth = getMillingTotalDepth();
  selectedIcTemplateIndex = icTemplateCount;
  icTemplateCount++;

  return icTemplateWriteFile();
}

static bool icTemplateDelete(uint8_t index)
{
  if (index >= icTemplateCount)
    return false;

  for (uint8_t i = index + 1; i < icTemplateCount; i++)
  {
    icTemplates[i - 1] = icTemplates[i];
  }

  if (icTemplateCount > 0)
    icTemplateCount--;

  if (selectedIcTemplateIndex == index)
    selectedIcTemplateIndex = -1;
  else if (selectedIcTemplateIndex > index)
    selectedIcTemplateIndex--;

  if (icTemplatePage > 0 && icTemplatePage * IC_TEMPLATE_PAGE_SIZE >= icTemplateCount)
    icTemplatePage--;

  return icTemplateWriteFile();
}

static bool icTemplateGetVisibleItem(uint8_t visibleIndex, IC_TEMPLATE_ITEM_TYPE * type, uint8_t * templateIndex, char * folder, uint16_t folderSize)
{
  uint8_t count = 0;

  if (type != NULL)
    *type = IC_TEMPLATE_ITEM_NONE;
  if (templateIndex != NULL)
    *templateIndex = 0;
  if (folder != NULL && folderSize > 0)
    folder[0] = '\0';

  if (icTemplateCurrentFolder[0] == '\0')
  {
    for (uint8_t i = 0; i < icTemplateCount; i++)
    {
      if (icTemplates[i].folder[0] == '\0' || icTemplateFolderExistsBefore(icTemplates[i].folder, i))
        continue;

      if (count == visibleIndex)
      {
        if (type != NULL)
          *type = IC_TEMPLATE_ITEM_FOLDER;
        if (folder != NULL)
          terminalCopyString(folder, icTemplates[i].folder, folderSize);
        return true;
      }

      count++;
    }
  }

  for (uint8_t i = 0; i < icTemplateCount; i++)
  {
    if (strcmp(icTemplates[i].folder, icTemplateCurrentFolder) != 0)
      continue;

    if (count == visibleIndex)
    {
      if (type != NULL)
        *type = IC_TEMPLATE_ITEM_TEMPLATE;
      if (templateIndex != NULL)
        *templateIndex = i;
      return true;
    }

    count++;
  }

  return false;
}

static uint8_t icTemplateGetVisibleCount(void)
{
  uint8_t count = 0;

  if (icTemplateCurrentFolder[0] == '\0')
  {
    for (uint8_t i = 0; i < icTemplateCount; i++)
    {
      if (icTemplates[i].folder[0] != '\0' && !icTemplateFolderExistsBefore(icTemplates[i].folder, i))
        count++;
    }
  }

  for (uint8_t i = 0; i < icTemplateCount; i++)
  {
    if (strcmp(icTemplates[i].folder, icTemplateCurrentFolder) == 0)
      count++;
  }

  return count;
}

// Get the current milling total depth
static float getMillingTotalDepth(void)
{
  return millingTotalDepth;
}

// Get the current start depth
static float getStartDepth(void)
{
  return startDepth;
}

// =========== INCREMENTAL MULTI-PASS MILLING FUNCTIONS (Mode 1) ===========

// Save last pass data to persistent storage
static void saveLastPassData(float finalDepth, const PLANE_CORNERS * corners)
{
  if (corners == NULL || corners->cornerCount < 4)
    return;
  
  // Save final depth and corners to persistent structure
  lastPassData.lastFinalDepth = finalDepth;
  lastPassData.passCount++;  // Increment pass counter
  
  for (int i = 0; i < 4; i++) {
    lastPassData.X_lastPass[i] = corners->X[i];
    lastPassData.Y_lastPass[i] = corners->Y[i];
  }
  
  lastPassData.isValid = true;
  lastPassData.savedTimestamp = OS_GetTimeMs();
  lastPassData.meshCompensationEnabled = meshCompensationMode;
  
  char msg[100];
  sprintf(msg, "; Saved Pass %d: Final Depth = %.2f mm", lastPassData.passCount, finalDepth);
  addGcodeCommand(msg);
}

// Load last pass data and restore corners
static void loadLastPassData(void)
{
  if (!lastPassData.isValid)
    return;
  
  // Restore corners to current working copy
  for (int i = 0; i < 4; i++) {
    planeCorners.X[i] = lastPassData.X_lastPass[i];
    planeCorners.Y[i] = lastPassData.Y_lastPass[i];
    planeCornerCaptured[i] = true;
  }
  planeCorners.cornerCount = 4;
  planeCornerReady = true;
  pendingPlaneCornerIndex = -1;
  useIncrementalMode = true;
}

static void syncMillingTotalDepthFromLastPass(void)
{
  if (!lastPassData.isValid)
    return;

  selectedIcTemplateIndex = -1;
  millingTotalDepth = fminf(fabsf(lastPassData.lastFinalDepth), 1.0f);
}

static void prepareNextPassPreview(void)
{
  if (!lastPassData.isValid)
    return;

  loadLastPassData();
  syncMillingTotalDepthFromLastPass();
  useIncrementalMode = true;
}

// Clear the persistent pass data
static void clearLastPassData(void)
{
  memset(&lastPassData, 0, sizeof(MILLING_PASS_DATA));
  useIncrementalMode = false;
  gcodeAblValidationPending = false;
  gcodeAblValidationFailed = false;
  gcodePendingAutoStartFilename[0] = '\0';
}

static void clearPlaneCornerState(void)
{
  memset(planeCorners.X, 0, sizeof(planeCorners.X));
  memset(planeCorners.Y, 0, sizeof(planeCorners.Y));
  memset(planeCornerCaptured, 0, sizeof(planeCornerCaptured));
  planeCorners.cornerCount = 0;
  planeCornerReady = false;
  m114_save_corner = false;
  pendingPlaneCornerIndex = -1;
}

static int8_t getNextMissingPlaneCornerIndex(void)
{
  if (!planeCornerCaptured[0])
    return 0;

  if (!planeCornerCaptured[2])
    return 2;

  return -1;
}

static void updateDerivedPlaneCornersFromDiagonal(void)
{
  if (!planeCornerCaptured[0] || !planeCornerCaptured[2])
  {
    planeCornerReady = false;
    return;
  }

  // Corner order on the UI:
  // 1 = bottom-left, 2 = bottom-right, 3 = top-right, 4 = top-left.
  // Use corners 1 and 3 as diagonal points, then infer corners 2 and 4.
  planeCorners.X[1] = planeCorners.X[2];
  planeCorners.Y[1] = planeCorners.Y[0];
  planeCorners.X[3] = planeCorners.X[0];
  planeCorners.Y[3] = planeCorners.Y[2];

  planeCornerCaptured[1] = true;
  planeCornerCaptured[3] = true;
  planeCorners.cornerCount = 4;
  planeCornerReady = true;
}

// Get mesh offset at position (x, y) using bilinear interpolation
// Returns Z offset in mm (positive = higher/thicker PCB, negative = lower/thinner PCB)
// ★ PLACEHOLDER: BUG FIX #2 - Call actual mesh editor function when available
static float getMeshOffsetAtPosition(float x, float y)
{
  if (!meshCompensationMode)
    return 0.0f;
  
  // TODO: IMPLEMENT when meshGetValueAtXY() is available from MeshEditor.h
  // This is CRITICAL for accurate multi-pass milling with uneven boards
  //
  // NOTE: Mesh offset = (measured Z at position) - (expected Z at position)
  // Example: if board surface is +0.05mm at (50,50), then meshOffset = +0.05
  // When added to depth: actualZ = commandZ + meshOffset
  //   = -0.40 + 0.05 = -0.35 (shallower cut at high point)
  // This automatically compensates for board unevenness
  //
  // Integration code:
  // extern float meshGetValueAtXY(float x, float y);  // From MeshEditor.h
  // return meshGetValueAtXY(x, y);
  
  // Placeholder: No mesh compensation until integrated
  return 0.0f;  // ★ Will be replaced with actual bilinear interpolation
}

// Calculate mesh offsets at all 4 corners
static void calculateMeshOffsetsAtCorners(const PLANE_CORNERS * corners)
{
  if (corners == NULL || corners->cornerCount < 4)
    return;
  
  for (int i = 0; i < 4; i++) {
    meshOffsetAtCorners[i] = getMeshOffsetAtPosition(corners->X[i], corners->Y[i]);
  }
}

// Generate spiral G-code for plane leveling/milling from 4 corner points (quadrilateral - any shape)
// Uses linear interpolation to shrink the shape inward
// totalDepth: Total milling depth in mm (configurable)
static void generatePlaneMillingGcode(const PLANE_CORNERS * corners, float totalDepth)
{
  if (corners->cornerCount < 4)
    return;  // Need exactly 4 corners

  const float depthTolerance = 0.0001f;
  
  // Store 4 corners
  float x[4], y[4];
  for (int i = 0; i < 4; i++)
  {
    x[i] = corners->X[i];
    y[i] = corners->Y[i];
  }
  
  // Calculate center point
  float centerX = (x[0] + x[1] + x[2] + x[3]) / 4.0f;
  float centerY = (y[0] + y[1] + y[2] + y[3]) / 4.0f;
  
  // Calculate average distance from center to corners (for inward step calculation)
  float avgDist = 0;
  for (int i = 0; i < 4; i++)
  {
    float dx = x[i] - centerX;
    float dy = y[i] - centerY;
    avgDist += sqrt(dx * dx + dy * dy);
  }
  avgDist /= 4.0f;
  
  // Milling parameters (configurable and fixed)
  float stepInward = 0.5f;      // Inward step per loop (mm) - increased to reduce command count
  float depthPerPass = 0.2f;    // Depth per pass (mm) - FIXED 0.20mm
  // totalDepth is now passed as parameter for flexibility
  float feedRate = 400.0f;      // Feed rate (mm/min) - slower speed
  float clearanceZ = 1.0f;      // Safe clearance height (mm)
  // startDepth is now a global configurable parameter
  
  // Add startup commands
  addGcodeCommand("; === PLANE MILLING START (Quadrilateral) ===");
  addGcodeCommand("M420 S1");    // Enable ABL / bed mesh compensation
  addGcodeCommand("M211 S0");    // Disable Z limit (allow cutting)
  // addGcodeCommand("G21");        // MM units
  // addGcodeCommand("G90");        // Absolute positioning
  
  char cmd[100];
  addGcodeCommand(MILLING_MOTOR_ON_CMD2);  // Start motor
  addGcodeCommand("G04 P1000");  // Pause 100ms
  addGcodeCommand(MILLING_MOTOR_ON_CMD1);  // Start motor
  addGcodeCommand("G04 P1000");  // Pause 100ms
  addGcodeCommand(MILLING_MOTOR_ON_CMD);  // Start motor
  // sprintf(cmd, "G0 Z%.1f F400", clearanceZ);
  // addGcodeCommand(cmd);          // Raise to safe height

  sprintf(cmd, "G0 X%.2f Y%.2f F1500", x[0], y[0]);
  addGcodeCommand(cmd);
  sprintf(cmd, "G0 Z7.0 F800");
  addGcodeCommand(cmd);
  // Pass 1 uses an automatic first-cut step and an absolute final depth.
  float baseDepth = 0.0f;
  float targetFinalDepth = -totalDepth;   // e.g. totalDepth=0.30 => final target -0.30mm
  float startingDepth = fmaxf(baseDepth + getStartDepth(), targetFinalDepth);

  if (fabsf(startingDepth - targetFinalDepth) <= depthTolerance)
    startingDepth = targetFinalDepth;
  
  // Milling loops at different depths
  float currentDepth = startingDepth;
  float lastMilledDepth = currentDepth;  // Track last depth that was milled
  bool milledAnyDepth = false;
  
  while (currentDepth < baseDepth - depthTolerance && currentDepth > targetFinalDepth + depthTolerance)
  {
    sprintf(cmd, "; === Depth: %.2f mm ===", currentDepth);
    addGcodeCommand(cmd);
    milledAnyDepth = true;
    lastMilledDepth = currentDepth;  // Update last milled depth
    
    // Spiral inward: shrink shape by moving each corner toward center
    float loopCount = 0;
    float currentShrink = 0;
    
    while (currentShrink < avgDist)
    {
      // Calculate shrink ratio (0 = original, 1 = center)
      float shrinkRatio = currentShrink / avgDist;
      // Continue closer to center - will break when ratio exceeds 1.0 (passed center)
      // This ensures we mill right up to the center point
      if (shrinkRatio > 0.99f)  // Stop slightly past center to ensure full coverage
        break;
      
      // Calculate shrunken corner positions by interpolating toward center
      float sx[4], sy[4];
      for (int i = 0; i < 4; i++)
      {
        sx[i] = x[i] + (centerX - x[i]) * shrinkRatio;
        sy[i] = y[i] + (centerY - y[i]) * shrinkRatio;
      }
      
      // Move to first corner of shrunken shape (fast travel F400)
      sprintf(cmd, "G0 X%.2f Y%.2f F400", sx[0], sy[0]);
      addGcodeCommand(cmd);

      // sprintf(cmd, "G1 Z%.2f F400", currentDepth + 2);
      // addGcodeCommand(cmd);
      // sprintf(cmd, "G0 Z%.1f F400", clearanceZ);
      // addGcodeCommand(cmd);          // Raise to safe height

      // Lower to cutting depth
      sprintf(cmd, "G1 Z%.2f F100", currentDepth);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P500");  // Pause after depth change
      
      // Trace quadrilateral path (visit all 4 corners in order)
      sprintf(cmd, "G1 X%.2f Y%.2f F%.0f", sx[1], sy[1], feedRate);  // To corner 1
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[2], sy[2]);  // To corner 2
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[3], sy[3]);  // To corner 3
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[0], sy[0]);  // Back to corner 0 (close loop)
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      // Move inward for next loop
      currentShrink += stepInward;
      loopCount++;
      
      // Safety: limit iterations to prevent infinite loops
      if (loopCount > 1000)
        break;
    }
    
    // Final pass: mill the center point directly to ensure no gaps
    // Move to center (fast travel F400)
    sprintf(cmd, "G0 X%.2f Y%.2f F400", centerX, centerY);
    addGcodeCommand(cmd);
    
    // Lower to cutting depth
    sprintf(cmd, "G1 Z%.2f F100", currentDepth);
    addGcodeCommand(cmd);
    addGcodeCommand("G04 P500");  // Pause after depth change
    
    // Return to center
    sprintf(cmd, "G1 X%.2f Y%.2f", centerX, centerY);
    addGcodeCommand(cmd);
    
    // Decrease depth for next pass
    currentDepth -= depthPerPass;
  }
  
  // Add a final pass at the exact target depth if the step size skipped over it.
  if ((!milledAnyDepth && targetFinalDepth < -depthTolerance) || lastMilledDepth > targetFinalDepth + depthTolerance)
  {
    float finalDepth = targetFinalDepth;
    sprintf(cmd, "; === Final Depth: %.2f mm ===", finalDepth);
    addGcodeCommand(cmd);
    
    // Spiral inward at final depth
    float loopCount = 0;
    float currentShrink = 0;
    
    while (currentShrink < avgDist)
    {
      float shrinkRatio = currentShrink / avgDist;
      if (shrinkRatio > 0.99f)
        break;
      
      float sx[4], sy[4];
      for (int i = 0; i < 4; i++)
      {
        sx[i] = x[i] + (centerX - x[i]) * shrinkRatio;
        sy[i] = y[i] + (centerY - y[i]) * shrinkRatio;
      }
      
      sprintf(cmd, "G0 X%.2f Y%.2f F400", sx[0], sy[0]);
      addGcodeCommand(cmd);
      
      sprintf(cmd, "G1 Z%.2f F100", finalDepth);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P500");  // Pause 100ms
      
      sprintf(cmd, "G1 X%.2f Y%.2f F%.0f", sx[1], sy[1], feedRate);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[2], sy[2]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[3], sy[3]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[0], sy[0]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");  // Pause 100ms
      
      currentShrink += stepInward;
      loopCount++;
      
      if (loopCount > 1000)
        break;
    }
    
    // Final center pass
    sprintf(cmd, "G0 X%.2f Y%.2f F400", centerX, centerY);
    addGcodeCommand(cmd);
    
    sprintf(cmd, "G1 Z%.2f F100", finalDepth);
    addGcodeCommand(cmd);
    addGcodeCommand("G04 P500");  // Pause after depth change
    
    sprintf(cmd, "G1 X%.2f Y%.2f", centerX, centerY);
    addGcodeCommand(cmd);

    sprintf(cmd, "G1 Z%.2f F300", finalDepth);
    addGcodeCommand(cmd);
    addGcodeCommand("G04 P100");  // Pause after depth change
  }
  
  // Save the exact final depth reached in standard mode for the next incremental pass.
  saveLastPassData(targetFinalDepth, corners);
}

// ★ INCREMENTAL MULTI-PASS GENERATOR (Mode 1)
// Generate spiral G-code starting from lastFinalDepth (continuing from previous pass)
// Supports mesh compensation for uneven milling
static void generatePlaneMillingGcode_Incremental(const PLANE_CORNERS * corners, float totalDepth, bool useLastPassDepth)
{
  if (corners->cornerCount < 4)
    return;  // Need exactly 4 corners

  const float sameDepthTolerance = 0.0001f;
  
  // Store 4 corners
  float x[4], y[4];
  for (int i = 0; i < 4; i++) {
    x[i] = corners->X[i];
    y[i] = corners->Y[i];
  }
  
  // Calculate center point
  float centerX = (x[0] + x[1] + x[2] + x[3]) / 4.0f;
  float centerY = (y[0] + y[1] + y[2] + y[3]) / 4.0f;
  
  // Calculate average distance from center to corners (for inward step calculation)
  float avgDist = 0;
  for (int i = 0; i < 4; i++) {
    float dx = x[i] - centerX;
    float dy = y[i] - centerY;
    avgDist += sqrt(dx * dx + dy * dy);
  }
  avgDist /= 4.0f;
  
  // Incremental mode uses the previous final depth as the new base surface.
  float baseDepth = useLastPassDepth && lastPassData.isValid ? lastPassData.lastFinalDepth : 0.0f;
  float targetFinalDepth = -totalDepth;
  float startingDepth = fmaxf(baseDepth + getStartDepth(), targetFinalDepth);
  bool sameDepthRecut = fabsf(targetFinalDepth - baseDepth) <= sameDepthTolerance;

  if (fabsf(startingDepth - targetFinalDepth) <= sameDepthTolerance)
    startingDepth = targetFinalDepth;
  
  // Milling parameters (fixed)
  float stepInward = 0.5f;      // Inward step per loop (mm)
  float depthPerPass = 0.2f;    // Depth per pass (mm) - FIXED 0.20mm
  float feedRate = 400.0f;      // Feed rate (mm/min)
  float clearanceZ = 1.0f;      // Safe clearance height (mm)

  // Add startup commands
  if (useLastPassDepth && lastPassData.isValid) {
    char msg[100];
    sprintf(msg, "; === INCREMENTAL PASS #%d (Mode 1) ===", lastPassData.passCount + 1);
    addGcodeCommand(msg);
    sprintf(msg, "; Previous total depth: %.2f mm", fabsf(baseDepth));
    addGcodeCommand(msg);
    sprintf(msg, "; New target total depth: %.2f mm", totalDepth);
    addGcodeCommand(msg);
    if (sameDepthRecut)
      sprintf(msg, "; Same-depth recut: regenerate full area at %.2f mm for mesh correction", totalDepth);
    else
      sprintf(msg, "; Auto first cut depth this pass: %.2f mm", startingDepth);
    addGcodeCommand(msg);
  } else {
    addGcodeCommand("; === PLANE MILLING START (Standard Mode) ===");
  }
  
  addGcodeCommand("M420 S1");    // Enable ABL / bed mesh compensation
  addGcodeCommand("M211 S0");    // Disable Z limit
  
  char cmd[100];
  addGcodeCommand(MILLING_MOTOR_ON_CMD2);  // Start motor
  addGcodeCommand("G04 P1000");  // Pause 100ms
  addGcodeCommand(MILLING_MOTOR_ON_CMD1);  // Start motor
  addGcodeCommand("G04 P1000");  // Pause 100ms
  addGcodeCommand(MILLING_MOTOR_ON_CMD);  // Start motor

  sprintf(cmd, "G0 X%.2f Y%.2f F1500", x[0], y[0]);
  addGcodeCommand(cmd);
  
  sprintf(cmd, "G0 Z7.0 F800");
  addGcodeCommand(cmd);
  
  // Calculate mesh offsets at corners (if compensation enabled)
  if (meshCompensationMode) {
    calculateMeshOffsetsAtCorners(corners);
  }
  
  // Milling loops at different depths
  float currentDepth = startingDepth;
  float lastMilledDepth = currentDepth;
  bool milledAnyDepth = false;
  
  // Continue deeper from the new first-cut depth toward this pass target depth.
  while (currentDepth < baseDepth - sameDepthTolerance && currentDepth > targetFinalDepth + sameDepthTolerance) {
    sprintf(cmd, "; === Depth: %.2f mm (Pass increment) ===", currentDepth);
    addGcodeCommand(cmd);
    milledAnyDepth = true;
    lastMilledDepth = currentDepth;
    
    // Spiral inward: shrink shape by moving each corner toward center
    float loopCount = 0;
    float currentShrink = 0;
    
    while (currentShrink < avgDist) {
      // Calculate shrink ratio (0 = original, 1 = center)
      float shrinkRatio = currentShrink / avgDist;
      if (shrinkRatio > 0.99f)
        break;
      
      // Calculate shrunken corner positions by interpolating toward center
      float sx[4], sy[4];
      for (int i = 0; i < 4; i++) {
        sx[i] = x[i] + (centerX - x[i]) * shrinkRatio;
        sy[i] = y[i] + (centerY - y[i]) * shrinkRatio;
      }
      
      // ★ MESH COMPENSATION: Add offset at this spiral loop position
      float loopCenterX = (sx[0] + sx[1] + sx[2] + sx[3]) / 4.0f;
      float loopCenterY = (sy[0] + sy[1] + sy[2] + sy[3]) / 4.0f;
      float meshOffset = getMeshOffsetAtPosition(loopCenterX, loopCenterY);
      float adaptedDepth = currentDepth + meshOffset;
      
      // Move to first corner of shrunken shape (fast travel F400)
      sprintf(cmd, "G0 X%.2f Y%.2f F400", sx[0], sy[0]);
      addGcodeCommand(cmd);
      
      // Lower to cutting depth (with mesh compensation)
      sprintf(cmd, "G1 Z%.2f F100", adaptedDepth);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P500");  // Pause after depth change
      
      // Trace quadrilateral path (visit all 4 corners in order)
      sprintf(cmd, "G1 X%.2f Y%.2f F%.0f", sx[1], sy[1], feedRate);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[2], sy[2]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[3], sy[3]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");
      
      sprintf(cmd, "G1 X%.2f Y%.2f", sx[0], sy[0]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");
      
      // Move inward for next loop
      currentShrink += stepInward;
      loopCount++;
      
      if (loopCount > 1000)
        break;
    }
    
    // Final pass: mill the center point directly
    float meshOffsetCenter = getMeshOffsetAtPosition(centerX, centerY);
    float adaptedCenterDepth = currentDepth + meshOffsetCenter;
    
    sprintf(cmd, "G0 X%.2f Y%.2f F400", centerX, centerY);
    addGcodeCommand(cmd);
    
    sprintf(cmd, "G1 Z%.2f F100", adaptedCenterDepth);
    addGcodeCommand(cmd);
    addGcodeCommand("G04 P500");
    
    sprintf(cmd, "G1 X%.2f Y%.2f", centerX, centerY);
    addGcodeCommand(cmd);
    
    // Decrease depth for next pass
    currentDepth -= depthPerPass;
  }
  
  // Final safety pass at exact target depth
  if (sameDepthRecut || (!milledAnyDepth && targetFinalDepth < baseDepth - sameDepthTolerance) || lastMilledDepth > targetFinalDepth + sameDepthTolerance) {
    float finalDepth = targetFinalDepth;
    if (sameDepthRecut)
      sprintf(cmd, "; === Recut Depth: %.2f mm (Mesh correction) ===", finalDepth);
    else
      sprintf(cmd, "; === Final Depth: %.2f mm (Target) ===", finalDepth);
    addGcodeCommand(cmd);

    // Full spiral at final depth so the whole area reaches the target depth.
    float loopCount = 0;
    float currentShrink = 0;

    while (currentShrink < avgDist) {
      float shrinkRatio = currentShrink / avgDist;
      if (shrinkRatio > 0.99f)
        break;

      float sx[4], sy[4];
      for (int i = 0; i < 4; i++) {
        sx[i] = x[i] + (centerX - x[i]) * shrinkRatio;
        sy[i] = y[i] + (centerY - y[i]) * shrinkRatio;
      }

      float loopCenterX = (sx[0] + sx[1] + sx[2] + sx[3]) / 4.0f;
      float loopCenterY = (sy[0] + sy[1] + sy[2] + sy[3]) / 4.0f;
      float meshOffset = getMeshOffsetAtPosition(loopCenterX, loopCenterY);
      float adaptedDepth = finalDepth + meshOffset;

      sprintf(cmd, "G0 X%.2f Y%.2f F400", sx[0], sy[0]);
      addGcodeCommand(cmd);

      sprintf(cmd, "G1 Z%.2f F100", adaptedDepth);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P500");

      sprintf(cmd, "G1 X%.2f Y%.2f F%.0f", sx[1], sy[1], feedRate);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");

      sprintf(cmd, "G1 X%.2f Y%.2f", sx[2], sy[2]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");

      sprintf(cmd, "G1 X%.2f Y%.2f", sx[3], sy[3]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");

      sprintf(cmd, "G1 X%.2f Y%.2f", sx[0], sy[0]);
      addGcodeCommand(cmd);
      addGcodeCommand("G04 P100");

      currentShrink += stepInward;
      loopCount++;

      if (loopCount > 1000)
        break;
    }

    float meshOffsetFinal = getMeshOffsetAtPosition(centerX, centerY);
    float adaptedFinalDepth = finalDepth + meshOffsetFinal;

    sprintf(cmd, "G0 X%.2f Y%.2f F400", centerX, centerY);
    addGcodeCommand(cmd);

    sprintf(cmd, "G1 Z%.2f F100", adaptedFinalDepth);
    addGcodeCommand(cmd);
    addGcodeCommand("G04 P500");

    sprintf(cmd, "G1 X%.2f Y%.2f", centerX, centerY);
    addGcodeCommand(cmd);
  }
  
  // Save the true final target depth reached in this incremental pass.
  saveLastPassData(targetFinalDepth, corners);
}

// Save corner from M114 response
static void savePlaneCorner(void)
{
  if (gcodePosition.isValid && pendingPlaneCornerIndex >= 0 && pendingPlaneCornerIndex < 4)
  {
    uint8_t cornerIndex = (uint8_t)pendingPlaneCornerIndex;
    bool wasCaptured = planeCornerCaptured[cornerIndex];

    planeCorners.X[cornerIndex] = gcodePosition.X;
    planeCorners.Y[cornerIndex] = gcodePosition.Y;
    planeCornerCaptured[cornerIndex] = true;

    if (!wasCaptured && planeCorners.cornerCount < 4)
      planeCorners.cornerCount++;

    pendingPlaneCornerIndex = -1;
    updateDerivedPlaneCornersFromDiagonal();
    
    // Display feedback
    char msg[50];
    sprintf(msg, "; Corner %d saved: X%.2f Y%.2f",
            cornerIndex + 1, gcodePosition.X, gcodePosition.Y);
    addGcodeCommand(msg);

    if (planeCornerReady)
      addGcodeCommand("; Corners 2 and 4 derived automatically from corners 3 and 1");
    
    // If all 4 corners are saved, set ready flag (wait for user to press SAVE)
    if (planeCornerReady)
    {
      addGcodeCommand("; Ready! Press SAVE to generate G-code");
    }
  }
}

// Generate and save G-code file when user presses SAVE button with 4 corners ready
static bool savePlaneCornersToFile(void)
{
  bool triggerLiveBedLeveling = false;

  if (!planeCornerReady || planeCorners.cornerCount != 4)
    return false;

  if (lastPassData.isValid)
  {
    float previousTotalDepth = fabsf(lastPassData.lastFinalDepth);

    if (millingTotalDepth < previousTotalDepth - 0.0001f)
    {
      char msg[100];
      sprintf(msg, "Tổng độ sâu phải >= %.2f mm. Nếu cần bù mesh, giữ nguyên %.2f mm để phay lại", previousTotalDepth, previousTotalDepth);
      popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, msg);
      return false;
    }
  }

  // Only the first pass should trigger live probing on the machine.
  if (!lastPassData.isValid)
  {
    triggerLiveBedLeveling = true;
    float minX = planeCorners.X[0];
    float maxX = planeCorners.X[0];
    float minY = planeCorners.Y[0];
    float maxY = planeCorners.Y[0];
    
    for (int i = 1; i < 4; i++)
    {
      if (planeCorners.X[i] < minX) minX = planeCorners.X[i];
      if (planeCorners.X[i] > maxX) maxX = planeCorners.X[i];
      if (planeCorners.Y[i] < minY) minY = planeCorners.Y[i];
      if (planeCorners.Y[i] > maxY) maxY = planeCorners.Y[i];
    }

    // mustStoreCmd("M42 P8 S255\n");
    // mustStoreCmd("G4 S0.1\n");
    // mustStoreCmd("M42 P8 S0\n");
    // mustStoreCmd("G4 S0.1\n");
    // mustStoreCmd("M42 P8 S255\n");
    // mustStoreCmd("G4 S0.1\n");

    char g29Cmd[100];
    // sprintf(g29Cmd, "G29 L%.2f F%.2f R%.2f B%.2f\n", minX, minY, maxX, maxY);
    // sprintf(g29Cmd, "G29 R%.2f F%.2f L%.2f B%.2f\n", minX, minY, maxX, maxY);
    sprintf(g29Cmd, "G29 R%.2f B%.2f L%.2f F%.2f V4\n", minX, minY, maxX, maxY);
    mustStoreCmd(g29Cmd);

    mustStoreCmd("G4 S0.1\n");
    mustStoreCmd("M500\n");
    mustStoreCmd("G4 S0.1\n");
    mustStoreCmd("M420 S1\n");
    mustStoreCmd("M42 P8 S0\n");
    mustStoreCmd("G4 S0.5\n");
  }

  gcodeAblValidationPending = triggerLiveBedLeveling;
  gcodeAblValidationFailed = false;
  gcodePendingAutoStartFilename[0] = '\0';

  if (!mountFS())
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "SD card mount failed");
    return false;
  }
  
  // Create and open file for this milling pass order.
  uint8_t passNumber = lastPassData.isValid ? (lastPassData.passCount + 1) : 1;
  gcodeBuildMillingFilename(passNumber, gcodeFilename, sizeof(gcodeFilename));

  if (f_open(&gcodeFile, gcodeFilename, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Failed to create file");
    return false;
  }
  
  gcodeFileOpen = true;
  
  // IMPORTANT: Flush any commands added to buffer BEFORE file was opened
  // (e.g., the "Corner X/4 saved" message, "Ready!" message)
  if (gcodeCommandCount > 0)
  {
    if (!flushGcodeChunk())
    {
      popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Pre-header flush failed");
      f_close(&gcodeFile);
      gcodeFileOpen = false;
      return false;
    }
  }
  
  // Now safe to reset count after flush
  gcodeCommandCount = 0;
  gcodeTotalCount = 0;
  
  // Write file header
  char header[100];
  sprintf(header, "; G-code file created by TFT\n; Pass: %u\n\n", passNumber);
  UINT headerWritten = 0;
  FRESULT headerResult = f_write(&gcodeFile, header, strlen(header), &headerWritten);
  if (headerResult != FR_OK || headerWritten != strlen(header))
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "File write header failed");
    f_close(&gcodeFile);
    gcodeFileOpen = false;
    return false;
  }
  
  // ★ CRITICAL FIX: Force load previous corners if Pass 2+
  // This PREVENTS re-probing at old milling locations (tool crash!)
  if (lastPassData.isValid) {
    loadLastPassData();
    useIncrementalMode = true;  // Force incremental mode
    char statusMsg[100];
    sprintf(statusMsg, "; Pass %d: Using %d corners from Pass 1 (probing locked)", lastPassData.passCount + 1, 4);
    addGcodeCommand(statusMsg);
  } else {
    useIncrementalMode = false;  // First pass: standard mode
  }
  
  // Generate milling G-code (will auto-flush chunks of 100 commands)
  addGcodeCommand("; === AUTO-GENERATE MILLING GCODE ===");
  
  // ★ Mode selection: Incremental or Standard
  if (useIncrementalMode && lastPassData.isValid) {
    char modeMsg[100];
    sprintf(modeMsg, "; Mode: INCREMENTAL PASS #%d", lastPassData.passCount + 1);
    addGcodeCommand(modeMsg);
    generatePlaneMillingGcode_Incremental(&planeCorners, millingTotalDepth, true);
  } else {
    addGcodeCommand("; Mode: STANDARD (First Pass)");
    generatePlaneMillingGcode(&planeCorners, millingTotalDepth);
  }
  
  // Flush final chunk
  if (gcodeCommandCount > 0)
  {
    if (!flushGcodeChunk())
    {
      popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Final flush failed");
      f_close(&gcodeFile);
      gcodeFileOpen = false;
      clearPlaneCornerState();
      gcodeCommandCount = 0;
      gcodeTotalCount = 0;
      return false;
    }
  }
  
  // Write shutdown commands (with error checking)
  UINT shutdownWritten = 0;
  FRESULT shutdownResult = FR_OK;
  
  shutdownResult = f_write(&gcodeFile, "G0 Z7.0\n" MILLING_MOTOR_OFF_CMD "\nM30\n", strlen("G0 Z7.0\n" MILLING_MOTOR_OFF_CMD "\nM30\n"), &shutdownWritten);
  if (shutdownResult != FR_OK || shutdownWritten != strlen("G0 Z7.0\n" MILLING_MOTOR_OFF_CMD "\nM30\n"))
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Shutdown write failed");
    f_close(&gcodeFile);
    gcodeFileOpen = false;
    clearPlaneCornerState();
    gcodeCommandCount = 0;
    gcodeTotalCount = 0;
    return false;
  }
  
  f_write(&gcodeFile, "; === MILLING COMPLETE ===\n", strlen("; === MILLING COMPLETE ===\n"), NULL);
  
  // Close file
  FRESULT closeResult = f_close(&gcodeFile);
  gcodeFileOpen = false;
  
  if (closeResult != FR_OK)
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "File close error");
    return false;
  }
  
  // Success message
  char successMsg[100];
  sprintf(successMsg, "Đã lưu %lu dòng", gcodeTotalCount);
  popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, successMsg);
  gcodeTrackSavedFilename(gcodeFilename);
  
  // Reset for next job
  clearPlaneCornerState();
  gcodeCommandCount = 0;
  gcodeTotalCount = 0;
  
  return true;
}

static bool autoStartGeneratedGcodeFile(FILE_SOURCE source, const char * filename)
{
  if (gcodeAblValidationPending)
  {
    gcodePendingAutoStartSource = source;
    terminalCopyString(gcodePendingAutoStartFilename, filename, sizeof(gcodePendingAutoStartFilename));
    popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, "Đã tự động cân bàn. Nếu Probing Failed, file vừa tạo sẽ bị xóa");
    return false;
  }

  if (filename == NULL || filename[0] == '\0')
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Generated file name missing");
    return false;
  }

  if (source >= FS_ONBOARD_MEDIA)
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Auto-run supports TFT media only");
    return false;
  }

  if (isPrinting())
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Printer is busy");
    return false;
  }

  infoFile.source = source;
  resetInfoFile();

  if (!enterFolder(filename) || !mountFS())
  {
    popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Auto-run mount failed");
    return false;
  }

  startPrinting();
  return true;
}

void terminalNotifyAblError(const char * errorText)
{
  if (!gcodeAblValidationPending)
    return;

  gcodeAblValidationPending = false;
  gcodeAblValidationFailed = true;

  if (gcodePendingAutoStartFilename[0] != '\0')
  {
    gcodeDeleteSavedFile(gcodePendingAutoStartFilename);
    gcodePendingAutoStartFilename[0] = '\0';
  }

  popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING,
                (errorText != NULL && errorText[0] != '\0') ? errorText : "Probing Failed\nĐã hũy chạy file và xóa file vừa tạo");
}

void terminalNotifyAblState(bool enabled)
{
  if (!gcodeAblValidationPending)
    return;

  if (!enabled || gcodeAblValidationFailed)
  {
    gcodeAblValidationPending = false;
    return;
  }

  gcodeAblValidationPending = false;

  if (gcodePendingAutoStartFilename[0] != '\0')
  {
    FILE_SOURCE source = gcodePendingAutoStartSource;
    char filename[sizeof(gcodePendingAutoStartFilename)];

    terminalCopyString(filename, gcodePendingAutoStartFilename, sizeof(filename));
    gcodePendingAutoStartFilename[0] = '\0';
    autoStartGeneratedGcodeFile(source, filename);
  }
}

static void terminalCopyString(char * dest, const char * src, uint16_t size)
{
  uint16_t index = 0;

  if (dest == NULL || size == 0)
    return;

  if (src != NULL)
  {
    while (index + 1 < size && src[index] != '\0')
    {
      dest[index] = src[index];
      index++;
    }
  }

  dest[index] = '\0';
}

static bool updateGcodePositionFromResponse(const char * response)
{
  float x = 0, y = 0, z = 0;
  bool foundX = false, foundY = false, foundZ = false;
  bool previewChanged = false;
  
  if (response == NULL || response[0] == '\0')
    return false;
  
  // Parse X:xx.xx
  const char * ptr = strchr(response, 'X');
  if (ptr != NULL && ptr[1] == ':')
  {
    x = strtof(ptr + 2, NULL);
    foundX = true;
  }
  
  // Parse Y:yy.yy
  ptr = strchr(response, 'Y');
  if (ptr != NULL && ptr[1] == ':')
  {
    y = strtof(ptr + 2, NULL);
    foundY = true;
  }
  
  // Parse Z:zz.zz
  ptr = strchr(response, 'Z');
  if (ptr != NULL && ptr[1] == ':')
  {
    z = strtof(ptr + 2, NULL);
    foundZ = true;
  }
  
  // Update if found all three coordinates
  if (foundX && foundY && foundZ)
  {
    gcodePosition.X = x;
    gcodePosition.Y = y;
    gcodePosition.Z = z;
    gcodePosition.lastQueryTime = OS_GetTimeMs();
    gcodePosition.isValid = true;
    
    // If M114 was sent to save corner, save it now (response received and parsed)
    if (m114_save_corner)
    {
      savePlaneCorner();
      m114_save_corner = false;  // Reset flag
      previewChanged = true;
    }
  }

  return previewChanged;
}

static void gcodeGetStatusRects(GUI_RECT * statusArea, GUI_RECT * previewRect, GUI_RECT * infoRect)
{
  if (statusArea != NULL)
    *statusArea = (GUI_RECT){GCODE_KB_AREA_WIDTH, ROW_HEIGHT, LCD_WIDTH, LCD_HEIGHT - ROW_HEIGHT};

  if (previewRect != NULL)
  {
    GUI_RECT area = {GCODE_KB_AREA_WIDTH, ROW_HEIGHT, LCD_WIDTH, LCD_HEIGHT - ROW_HEIGHT};
    int16_t infoHeight = ROW_HEIGHT;
    *previewRect = (GUI_RECT){area.x0 + 6, area.y0 + infoHeight + 4, area.x1 - 6, area.y1 - 6};
  }

  if (infoRect != NULL)
  {
    GUI_RECT area = {GCODE_KB_AREA_WIDTH, ROW_HEIGHT, LCD_WIDTH, LCD_HEIGHT - ROW_HEIGHT};
    *infoRect = (GUI_RECT){area.x0 + 6, area.y0 + 4, area.x1 - 6, area.y0 + ROW_HEIGHT - 4};
  }
}

static void gcodeGetPreviewBoardRect(const GUI_RECT * area, GUI_RECT * boardRect)
{
  int16_t areaWidth = area->x1 - area->x0;
  int16_t areaHeight = area->y1 - area->y0;
  int16_t boardWidth = (areaWidth * 52) / 100;
  int16_t boardHeight = (areaHeight * 36) / 100;
  int16_t boardCenterX = (area->x0 + area->x1) / 2;
  int16_t boardCenterY = area->y0 - 10 + ((areaHeight - 44) / 2);

  *boardRect = (GUI_RECT){boardCenterX - boardWidth / 2, boardCenterY - boardHeight / 2,
                          boardCenterX + boardWidth / 2, boardCenterY + boardHeight / 2};
}

static void gcodeGetPreviewCornerRects(const GUI_RECT * area, GUI_RECT * padRect)
{
  GUI_RECT boardRect;
  int16_t areaWidth = area->x1 - area->x0;
  int16_t padWidth = (areaWidth * 34) / 100;
  int16_t padHeight = (padWidth * 17) / 20;
  int16_t gapX;
  int16_t gapY;

  if (padWidth < 68)
    padWidth = 68;
  if (padHeight < 52)
    padHeight = 52;

  gapX = padWidth / 5;
  gapY = padHeight / 5;

  gcodeGetPreviewBoardRect(area, &boardRect);

  padRect[0] = (GUI_RECT){boardRect.x0 - gapX - padWidth / 2, boardRect.y1 + gapY - padHeight / 2,
                          boardRect.x0 - gapX + padWidth / 2, boardRect.y1 + gapY + padHeight / 2};
  padRect[1] = (GUI_RECT){boardRect.x1 + gapX - padWidth / 2, boardRect.y1 + gapY - padHeight / 2,
                          boardRect.x1 + gapX + padWidth / 2, boardRect.y1 + gapY + padHeight / 2};
  padRect[2] = (GUI_RECT){boardRect.x1 + gapX - padWidth / 2, boardRect.y0 - gapY - padHeight / 2,
                          boardRect.x1 + gapX + padWidth / 2, boardRect.y0 - gapY + padHeight / 2};
  padRect[3] = (GUI_RECT){boardRect.x0 - gapX - padWidth / 2, boardRect.y0 - gapY - padHeight / 2,
                          boardRect.x0 - gapX + padWidth / 2, boardRect.y0 - gapY + padHeight / 2};
}

static inline void gcodeInitMenuRects(void)
{
  GUI_RECT previewRect;
  GUI_RECT padRect[4];

  for (uint8_t i = 0; i < GCODE_KB_KEY_COUNT; i++)
  {
    gcodeMenuRect[i] = gcodeIsHiddenWorkflowButton(i) ? (GUI_RECT){0, 0, 0, 0} : gcodeGetButtonRect(i);
  }

  gcodeGetStatusRects(NULL, &previewRect, NULL);
  gcodeGetPreviewBoardRect(&previewRect, &gcodeMenuRect[GKEY_G_IC_TOUCH]);
  gcodeGetPreviewCornerRects(&previewRect, padRect);
  gcodeMenuRect[GKEY_G_CORNER_1_TOUCH] = padRect[0];
  gcodeMenuRect[GKEY_G_CORNER_2_TOUCH] = padRect[1];
  gcodeMenuRect[GKEY_G_CORNER_3_TOUCH] = padRect[2];
  gcodeMenuRect[GKEY_G_CORNER_4_TOUCH] = padRect[3];
}

static uint16_t gcodeGetPreviewCornerColor(uint8_t index)
{
  if (m114_save_corner && pendingPlaneCornerIndex == index)
    return GCODE_PREVIEW_CORNER_WAIT_COLOR;

  if ((index == 1 || index == 3) && planeCornerReady && planeCornerCaptured[index])
    return GCODE_PREVIEW_CORNER_AUTO_COLOR;

  if (planeCornerCaptured[index])
    return GCODE_PREVIEW_CORNER_DONE_COLOR;

  return GCODE_PREVIEW_CORNER_IDLE_COLOR;
}

static const char * gcodeGetPreviewCornerLabel(uint8_t index)
{
  static const char * const label[4] = {"1", "2", "3", "4"};
  static const char * const autoLabel[4] = {"1", "A2", "3", "A4"};

  if ((index == 1 || index == 3) && planeCornerReady && planeCornerCaptured[index])
    return autoLabel[index];

  return label[index];
}

static void gcodeBuildPreviewStatusText(char * statusText, uint16_t statusTextSize)
{
  if (!gcodeHomeReady)
  {
    snprintf(statusText, statusTextSize, "Về gốc trước");
  }
  else if (planeCornerReady)
  {
    if (lastPassData.isValid && useIncrementalMode)
      sprintf(statusText, "P%d sẵn sàng", lastPassData.passCount + 1);
    else if (lastPassData.isValid)
      sprintf(statusText, "P%d dùng góc cũ", lastPassData.passCount + 1);
    else
      strcpy(statusText, "Sẵn sàng phay");
  }
  else if (m114_save_corner)
  {
    sprintf(statusText, "Đang lấy góc %d", pendingPlaneCornerIndex + 1);
  }
  else if (lastPassData.isValid)
  {
    snprintf(statusText, statusTextSize, "Nhấn góc để nạp lại");
  }
  else
  {
    int8_t nextCornerIndex = getNextMissingPlaneCornerIndex();

    if (nextCornerIndex >= 0)
      snprintf(statusText, statusTextSize, "Đo góc %d", nextCornerIndex + 1);
    else
      snprintf(statusText, statusTextSize, "Đã tự suy góc 2 và 4");
  }
}

static inline void gcodeDrawPreviewCornerMap(const GUI_RECT * area)
{
  GUI_RECT boardRect;
  GUI_RECT padRect[4];
  GUI_RECT footerRect;
  char statusText[48];

  gcodeGetPreviewBoardRect(area, &boardRect);
  gcodeGetPreviewCornerRects(area, padRect);

  gcodeBuildPreviewStatusText(statusText, sizeof(statusText));

  GUI_SetColor(GCODE_PREVIEW_BOARD_COLOR);
  GUI_FillPrect(&boardRect);
  GUI_SetColor(GCODE_PREVIEW_BORDER_COLOR);
  GUI_DrawPrect(&boardRect);

  drawStandardValue(&boardRect, VALUE_STRING, "IC", FONT_SIZE_LARGE,
                    GCODE_PREVIEW_BOARD_FONT_COLOR, GCODE_PREVIEW_BOARD_COLOR, 0, true);

  for (uint8_t i = 0; i < 4; i++)
  {
    uint16_t cornerColor = gcodeGetPreviewCornerColor(i);
    BUTTON btn = {
      .rect = padRect[i],
      .context = (const uint8_t *)gcodeGetPreviewCornerLabel(i),
      .radius = GCODE_PREVIEW_BUTTON_RADIUS,
      .lineWidth = 1,
      .lineColor = GCODE_PREVIEW_BORDER_COLOR,
      .fontColor = WHITE,
      .backColor = cornerColor,
      .pLineColor = GCODE_PREVIEW_BORDER_COLOR,
      .pFontColor = WHITE,
      .pBackColor = cornerColor,
    };

    GUI_DrawButton(&btn, 0);
  }

  {
    int16_t lowerPadsBottom = (padRect[0].y1 > padRect[1].y1) ? padRect[0].y1 : padRect[1].y1;
    int16_t footerHeight = 24;
    int16_t footerBottom = area->y1 - 12;
    int16_t footerTop = lowerPadsBottom + 10;
    uint16_t footerBg = planeCornerReady ? GCODE_PREVIEW_READY_COLOR : GCODE_PREVIEW_BG_COLOR;
    uint16_t footerFont = planeCornerReady ? WHITE : GCODE_PREVIEW_FONT_COLOR;

    if (footerTop + footerHeight > footerBottom)
      footerTop = footerBottom - footerHeight;

    footerRect = (GUI_RECT){area->x0 + 12, footerTop, area->x1 - 12, footerBottom};

    GUI_SetColor(footerBg);
    GUI_FillPrect(&footerRect);
    GUI_SetColor(GCODE_PREVIEW_BORDER_COLOR);
    GUI_DrawPrect(&footerRect);

    drawStandardValue(&footerRect, VALUE_STRING, statusText, FONT_SIZE_NORMAL,
                      footerFont, footerBg, 0, true);
  }

}

static inline void gcodeDrawStatus(void)
{
  GUI_RECT statusArea;
  GUI_RECT previewRect;
  GUI_RECT infoRect;
  char infoText[80];

  gcodeGetStatusRects(&statusArea, &previewRect, &infoRect);

  GUI_SetBkColor(GCODE_PREVIEW_BG_COLOR);
  GUI_ClearPrect(&statusArea);
  GUI_SetColor(GCODE_PREVIEW_BORDER_COLOR);
  GUI_DrawPrect(&statusArea);

  GUI_SetColor(GCODE_PARAM_BG_COLOR);
  GUI_FillPrect(&infoRect);
  GUI_SetColor(GCODE_PREVIEW_BORDER_COLOR);
  GUI_DrawPrect(&infoRect);

  sprintf(infoText, "Mau: %s  |  Tong sau %.2f", icTemplateGetSelectedName(), getMillingTotalDepth());
  drawStandardValue(&infoRect, VALUE_STRING, infoText, FONT_SIZE_NORMAL,
                    GCODE_PARAM_FONT_COLOR, GCODE_PARAM_BG_COLOR, 0, true);

  gcodeDrawPreviewCornerMap(&previewRect);
}

static inline float gcodeGetEncoderMoveAmount(float amount)
{
  if (gcode_nowAxis == X_AXIS || gcode_nowAxis == Y_AXIS)
    return -amount;

  return amount;
}

static void gcodeBuildMillingFilename(uint8_t passNumber, char * filename, uint16_t filenameSize)
{
  if (filename == NULL || filenameSize == 0)
    return;

  snprintf(filename, filenameSize, "phay_%02u.gcode", passNumber);
}

static void gcodeTrackSavedFilename(const char * filename)
{
  if (filename == NULL || filename[0] == '\0')
    return;

  for (uint8_t i = 0; i < gcodeSavedFileCount; i++)
  {
    if (strcmp(gcodeSavedFilenames[i], filename) == 0)
      return;
  }

  if (gcodeSavedFileCount >= COUNT(gcodeSavedFilenames))
  {
    for (uint8_t i = 1; i < COUNT(gcodeSavedFilenames); i++)
    {
      strcpy(gcodeSavedFilenames[i - 1], gcodeSavedFilenames[i]);
    }

    gcodeSavedFileCount = COUNT(gcodeSavedFilenames) - 1;
  }

  strcpy(gcodeSavedFilenames[gcodeSavedFileCount++], filename);
}

static void gcodeUntrackSavedFilename(const char * filename)
{
  uint8_t remaining = 0;

  if (filename == NULL || filename[0] == '\0')
    return;

  for (uint8_t i = 0; i < gcodeSavedFileCount; i++)
  {
    if (strcmp(gcodeSavedFilenames[i], filename) == 0)
      continue;

    if (remaining != i)
      strcpy(gcodeSavedFilenames[remaining], gcodeSavedFilenames[i]);

    remaining++;
  }

  for (uint8_t i = remaining; i < gcodeSavedFileCount; i++)
  {
    gcodeSavedFilenames[i][0] = '\0';
  }

  gcodeSavedFileCount = remaining;
}

static bool gcodeDeleteSavedFile(const char * filename)
{
  if (filename == NULL || filename[0] == '\0')
    return false;

  if (!mountFS())
    return false;

  if (f_unlink(filename) != FR_OK)
    return false;

  gcodeUntrackSavedFilename(filename);
  return true;
}

static bool gcodeDeleteSavedFiles(uint8_t * deletedCount, uint8_t * failedCount)
{
  uint8_t deleted = 0;
  uint8_t failed = 0;
  uint8_t remaining = 0;
  char filename[50];

  if (deletedCount != NULL)
    *deletedCount = 0;

  if (failedCount != NULL)
    *failedCount = 0;

  if (!mountFS())
    return false;

  for (uint8_t passNumber = 1; passNumber <= GCODE_MILLING_FILE_LIMIT; passNumber++)
  {
    gcodeBuildMillingFilename(passNumber, filename, sizeof(filename));

    if (f_unlink(filename) == FR_OK)
      deleted++;
  }

  for (uint8_t i = 0; i < gcodeSavedFileCount; i++)
  {
    if (gcodeSavedFilenames[i][0] == '\0')
      continue;

    FRESULT unlinkResult = f_unlink(gcodeSavedFilenames[i]);

    if (unlinkResult == FR_OK || unlinkResult == FR_NO_FILE)
    {
      if (unlinkResult == FR_OK)
        deleted++;
    }
    else
    {
      if (remaining != i)
        strcpy(gcodeSavedFilenames[remaining], gcodeSavedFilenames[i]);

      remaining++;
      failed++;
    }
  }

  for (uint8_t i = remaining; i < gcodeSavedFileCount; i++)
  {
    gcodeSavedFilenames[i][0] = '\0';
  }

  gcodeSavedFileCount = remaining;

  if (deletedCount != NULL)
    *deletedCount = deleted;

  if (failedCount != NULL)
    *failedCount = failed;

  return true;
}

// Flush current chunk of G-code commands to SD card
// Ensures each line is written completely before moving to next
static inline bool flushGcodeChunk(void)
{
  if (!gcodeFileOpen || gcodeCommandCount == 0)
    return true;  // Nothing to flush
  
  uint8_t successCount = 0;  // Track successfully written commands
  FRESULT writeResult = FR_OK;
  
  // Write all commands in current chunk to file
  for (int i = 0; i < gcodeCommandCount; i++)
  {
    // Get command and REMOVE trailing space (added for display only)
    char cleanCommand[CMD_MAX_SIZE];
    strcpy(cleanCommand, gcodeCommands[i]);
    

    // Find and remove trailing space
    uint16_t cleanLen = strlen(cleanCommand);
    if (cleanLen > 0 && cleanCommand[cleanLen - 1] == ' ')
    {
      cleanCommand[cleanLen - 1] = '\0';  // Remove space
      cleanLen--;
    }
    
    // Safety check: ensure command is not empty and fits in buffer
    if (cleanLen == 0 || cleanLen > CMD_MAX_SIZE)
    {
      // Skip empty or malformed commands
      continue;
    }
    
    // Write CLEAN command to file (without trailing space)
    UINT bytesWritten = 0;
    writeResult = f_write(&gcodeFile, cleanCommand, cleanLen, &bytesWritten);
    
    // Verify write success - CRITICAL CHECK
    if (writeResult != FR_OK || bytesWritten != cleanLen)
    {
      // Write error detected
      return false;  // Stop on first error
    }
    
    // Write newline after command
    UINT newlineWritten = 0;
    writeResult = f_write(&gcodeFile, "\n", 1, &newlineWritten);
    
    if (writeResult != FR_OK || newlineWritten != 1)
    {
      // Failed to write newline - data integrity issue
      return false;
    }
    
    successCount++;  // Count successfully written command
  }
  
  // Verify all commands were written before clearing buffer
  // (important safety check)
  if (successCount != gcodeCommandCount)
  {
    // Mismatch between attempted and successful writes
    return false;
  }
  
  // Update total count only after all commands successfully written
  gcodeTotalCount += gcodeCommandCount;
  
  // Clear chunk buffer for next round (only after successful flush)
  gcodeCommandCount = 0;
  
  return true;
}

// Add G-code command to the commands array (with auto-flush when chunk full)
static inline void addGcodeCommand(const char * command)
{
  // Safety check: input validation
  if (command == NULL || strlen(command) == 0)
    return;  // Reject empty/null commands
  
  // If chunk is full (100 commands), flush to SD before adding more
  if (gcodeCommandCount >= GCODE_CHUNK_SIZE)
  {
    bool flushSuccess = flushGcodeChunk();
    if (!flushSuccess)
    {
      // Flush failed - stop processing
      // Note: Data already in SD, just current chunk lost
      gcodeCommandCount = 0;  // Reset to allow retry
      return;
    }
  }
  
  // Safety: ensure we have space in chunk buffer
  if (gcodeCommandCount >= GCODE_CHUNK_SIZE)
    return;  // Should not happen, but safety check
  
  // Copy command safely with length validation
  uint16_t cmdLen = strlen(command);
  if (cmdLen > CMD_MAX_SIZE - 1)  // -1 for null terminator
    cmdLen = CMD_MAX_SIZE - 1;  // Truncate if too long
  
  // Copy and ensure null termination (strncpy_no_pad needs n >= len+1)
  strncpy_no_pad(gcodeCommands[gcodeCommandCount], command, cmdLen + 1);
  
  // Remove trailing newline if present (clean format)
  char * newline = strchr(gcodeCommands[gcodeCommandCount], '\n');
  if (newline) *newline = '\0';
  
  // Increment count
  gcodeCommandCount++;
  
  // Auto-scroll to show newly added command on the last visible page
  uint8_t maxVisibleLines = (LCD_HEIGHT - 2 * ROW_HEIGHT) / 18;
  uint8_t lastPageStart = (gcodeCommandCount > maxVisibleLines) ? (gcodeCommandCount - maxVisibleLines) : 0;
  gcodeDisplayStart = lastPageStart;  // Always show the last page with new command
}

// Save G-code commands to file (called from SAVE button - for manual keyboard input)
static inline bool saveGcodeToFile(void)
{
  // For streaming mode: If file is open, just close it and flush remaining
  if (gcodeFileOpen)
  {
    // Flush any remaining commands
    if (gcodeCommandCount > 0)
    {
      flushGcodeChunk();
    }
    // Write shutdown commands and close
    f_write(&gcodeFile, "G0 Z7.0\n", 9, NULL);      // Raise tool
    f_write(&gcodeFile, MILLING_MOTOR_OFF_CMD "\n", strlen(MILLING_MOTOR_OFF_CMD "\n"), NULL);  // Stop motor
    // f_write(&gcodeFile, "G0 X0 Y0\n", 10, NULL);     // Return to origin
    f_write(&gcodeFile, "M30\n", 4, NULL);            // End program
    
    f_close(&gcodeFile);
    gcodeFileOpen = false;
    gcodeTrackSavedFilename(gcodeFilename);
    
    // Show completion message
    char msg[100];
    sprintf(msg, "File saved: %lu commands", gcodeTotalCount);
    popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, msg);
    
    gcodeCommandCount = 0;
    gcodeTotalCount = 0;
    return true;
  }
  
  // For non-streaming mode: Write buffer to new file (legacy support for manual input)
  if (gcodeCommandCount == 0)
    return false;
  
  // Create filename with timestamp
  char filename[50];
  uint32_t timestamp = OS_GetTimeMs();
  sprintf(filename, "gcode_%lu.gcode", timestamp);
  
  // Mount SD card and create file
  if (mountFS())
  {
    FIL file;
    UINT bytesWritten;
    
    if (f_open(&file, filename, FA_CREATE_NEW | FA_WRITE) == FR_OK)
    {
      // Write header comment
      char header[100];
      sprintf(header, "; G-code file created by TFT\n; Commands: %d\n; Timestamp: %lu\n\n", 
              gcodeCommandCount, timestamp);
      f_write(&file, header, strlen(header), &bytesWritten);
      
      // Write all commands
      for (int i = 0; i < gcodeCommandCount; i++)
      {
        f_write(&file, gcodeCommands[i], strlen(gcodeCommands[i]), &bytesWritten);
        f_write(&file, "\n", 1, &bytesWritten);  // single newline after each command
      }
      
      f_close(&file);
      gcodeTrackSavedFilename(filename);
      gcodeCommandCount = 0;
      return true;
    }
  }
  
  return false;
}

// Draw G-code commands table (simplified - just list commands)
static inline void gcodeDrawTable(void)
{
  // Note: Table display merged with gcodeDrawStatus() for simplicity
  // This function is kept for compatibility but display is in gcodeDrawStatus()
}

static inline void gcodeKeyboardDrawMenu(void)
{
  GUI_RECT textArea = {0, ROW_HEIGHT + (GCODE_KB_ROW_COUNT * GCODE_KB_KEY_HEIGHT), 
                       GCODE_KB_AREA_WIDTH, LCD_HEIGHT - ROW_HEIGHT};

  gcodeInitMenuRects();
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(gcodeMenuRect), gcodeMenuRect, gcodeDrawButton, NULL);

  // Clear all areas
  GUI_SetBkColor(GCODE_ACCENT_BG_COLOR);
  GUI_ClearPrect(&gcodeKeyboardAreaRect[0]);  // Top control
  GUI_ClearPrect(&gcodeKeyboardAreaRect[2]);  // Bottom control
  gcodePositionStatusDirty = true;
  
  GUI_SetBkColor(GCODE_PANEL_BG_COLOR);
  GUI_ClearPrect(&gcodeKeyboardAreaRect[1]);  // Keyboard area
  
  // Clear text area
  GUI_SetBkColor(TEXTBOX_BG_COLOR);
  GUI_ClearPrect(&textArea);

  GUI_SetColor(GCODE_ACCENT_BORDER_COLOR);
  GUI_HLine(gcodeKeyboardAreaRect[0].x0, gcodeKeyboardAreaRect[0].y1 - 1, gcodeKeyboardAreaRect[0].x1);
  GUI_HLine(gcodeKeyboardAreaRect[2].x0, gcodeKeyboardAreaRect[2].y0, gcodeKeyboardAreaRect[2].x1);
  GUI_DrawPrect(&textArea);

  GUI_SetTextMode(GUI_TEXTMODE_TRANS);
  gcodeDrawKeyboard();
  // gcodeDrawText();
  gcodeDrawStatus();  // Display commands list in right area
}

static inline void icTemplateNameKeyboardDrawButton(uint8_t index, uint8_t isPressed)
{
  if (index >= COUNT(editorKeyRect) || index == GKEY_PREV || index == GKEY_NEXT)
    return;

  const char * text = (numpad) ? gcodeKey123[index] : gcodeKeyABC[index];
  if (index == GKEY_SEND)
    text = "OK";

  drawStandardValue(&editorKeyRect[index], VALUE_STRING, text,
                    (index > GKEY_SEND) ? FONT_SIZE_LARGE : FONT_SIZE_NORMAL,
                    isPressed ? KEY_BG_COLOR : KEY_FONT_COLOR,
                    isPressed ? KEY_FONT_COLOR : KEY_BG_COLOR, 1, true);
}

static void icTemplateNameKeyboardDraw(void)
{
  for (uint8_t i = GKEY_CLEAR; i < COUNT(gcodeKey123); i++)
  {
    icTemplateNameKeyboardDrawButton(i, false);
  }
}

static bool icTemplateNameInputPopup(void)
{
  KEY_VALUES key_num = KEY_IDLE;
  uint8_t nowIndex = gcodeBufferIndex;
  uint8_t lastIndex = 0xFF;
  bool origNumpad = numpad;

  numpad = false;
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(editorKeyRect), editorKeyRect, icTemplateNameKeyboardDrawButton, NULL);

  GUI_SetBkColor(KB_BG_COLOR);
  GUI_ClearPrect(&editorAreaRect[1]);
  GUI_SetBkColor(BAR_BG_COLOR);
  GUI_ClearPrect(&editorAreaRect[0]);
  GUI_ClearPrect(&editorAreaRect[2]);
  GUI_SetTextMode(GUI_TEXTMODE_TRANS);
  icTemplateNameKeyboardDraw();

  while (MENU_IS(menuGcode))
  {
    key_num = menuKeyGetValue();

    if (key_num == KEY_IDLE)
    {
      loopBackEnd();
      continue;
    }

    switch (key_num)
    {
      case GKEY_PREV:
      case GKEY_NEXT:
        break;

      case GKEY_CLEAR:
        nowIndex = 0;
        break;

      case GKEY_SEND:
        gcodeBuffer[nowIndex] = '\0';
        gcodeBufferIndex = nowIndex;
        numpad = origNumpad;
        gcodeKeyboardDrawMenu();
        return nowIndex > 0;

      case GKEY_BACK:
        gcodeBuffer[nowIndex] = '\0';
        gcodeBufferIndex = nowIndex;
        numpad = origNumpad;
        gcodeKeyboardDrawMenu();
        return false;

      case GKEY_ABC_123:
        TOGGLE_BIT(numpad, 0);
        icTemplateNameKeyboardDraw();
        break;

      case GKEY_SPACE:
        if (nowIndex > 0 && nowIndex < IC_TEMPLATE_NAME_LEN - 1)
          gcodeBuffer[nowIndex++] = '_';
        break;

      case GKEY_DEL:
        nowIndex -= !(!nowIndex);
        break;

      default:
        if (key_num < COUNT(gcodeKey123) && nowIndex < IC_TEMPLATE_NAME_LEN - 1)
          gcodeBuffer[nowIndex++] = (numpad) ? gcodeKey123[key_num][0] : gcodeKeyABC[key_num][0];
        break;
    }

    if (lastIndex != nowIndex)
    {
      GUI_RECT expandedTextBox = {
        TEXTBOX_INSET,
        (COMMAND_START_ROW * CTRL_HEIGHT) + TEXTBOX_INSET,
        (3 * CTRL_WIDTH) - TEXTBOX_INSET,
        ((COMMAND_START_ROW + 1) * CTRL_HEIGHT) - TEXTBOX_INSET
      };

      lastIndex = nowIndex;
      gcodeBuffer[nowIndex] = '\0';
      drawStandardValue(&expandedTextBox, VALUE_STRING, gcodeBuffer, FONT_SIZE_NORMAL,
                        TEXTBOX_FONT_COLOR, TEXTBOX_BG_COLOR, 1, true);
    }

    loopBackEnd();
  }

  numpad = origNumpad;
  return false;
}

static void icTemplateCloseListPopup(void)
{
  gcodeKeyboardDrawMenu();
  gcodeDrawKeyboard();
  gcodeDrawStatus();
  gcodePositionStatusDirty = true;
}

static void icTemplateDrawListButton(uint8_t index, uint8_t isPressed)
{
  char text[64];
  uint16_t bgColor = isPressed ? GCODE_ACCENT_BORDER_COLOR : GCODE_PANEL_BG_COLOR;
  uint16_t fontColor = isPressed ? WHITE : BLACK;

  if (index < IC_TEMPLATE_PAGE_SIZE)
  {
    uint8_t visibleIndex = icTemplatePage * IC_TEMPLATE_PAGE_SIZE + index;
    uint8_t templateIndex = 0;
    char folder[IC_TEMPLATE_FOLDER_LEN];
    IC_TEMPLATE_ITEM_TYPE itemType = IC_TEMPLATE_ITEM_NONE;

    if (!icTemplateGetVisibleItem(visibleIndex, &itemType, &templateIndex, folder, sizeof(folder)))
    {
      text[0] = '\0';
    }
    else if (itemType == IC_TEMPLATE_ITEM_FOLDER)
    {
      snprintf(text, sizeof(text), "[%s]", folder);
      bgColor = isPressed ? GCODE_ACCENT_BORDER_COLOR : MAT_BLUE;
      fontColor = WHITE;
    }
    else
    {
      snprintf(text, sizeof(text), "%s  %.2f", icTemplates[templateIndex].name, icTemplates[templateIndex].totalDepth);
    }
  }
  else if (index == IC_TEMPLATE_PAGE_SIZE)
  {
    strcpy(text, "Trước");
    bgColor = GCODE_ACCENT_BG_COLOR;
    fontColor = WHITE;
  }
  else if (index == IC_TEMPLATE_PAGE_SIZE + 1)
  {
    strcpy(text, "Sau");
    bgColor = GCODE_ACCENT_BG_COLOR;
    fontColor = WHITE;
  }
  else
  {
    strcpy(text, "Back");
    bgColor = GCODE_ACCENT_BG_COLOR;
    fontColor = WHITE;
  }

  drawStandardValue(&icTemplateKeyRect[index], VALUE_STRING, text, FONT_SIZE_NORMAL, fontColor, bgColor, 1, true);
}

static void icTemplateDrawList(void)
{
  GUI_SetBkColor(GCODE_PANEL_BG_COLOR);
  GUI_ClearRect(0, 0, LCD_WIDTH, LCD_HEIGHT);

  for (uint8_t i = 0; i < IC_TEMPLATE_KEY_COUNT; i++)
  {
    icTemplateDrawListButton(i, false);
  }
}

static void icTemplateOpenListMenu(void)
{
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(icTemplateKeyRect), icTemplateKeyRect, icTemplateDrawListButton, NULL);
  icTemplateDrawList();
}

static void icTemplateDrawActionButton(uint8_t index, uint8_t isPressed)
{
  char text[64];
  uint16_t bgColor = isPressed ? GCODE_ACCENT_BORDER_COLOR : GCODE_ACCENT_BG_COLOR;
  uint16_t fontColor = WHITE;

  if (icTemplateActionIndex >= icTemplateCount)
    return;

  switch (index)
  {
    case 0:
      snprintf(text, sizeof(text), "Chọn: %s %.2f", icTemplates[icTemplateActionIndex].name,
               icTemplates[icTemplateActionIndex].totalDepth);
      bgColor = isPressed ? GCODE_ACCENT_BORDER_COLOR : MAT_GREEN;
      break;

    case 1:
      strcpy(text, "Sửa độ sâu");
      break;

    case 2:
      strcpy(text, "Xóa mẫu");
      bgColor = isPressed ? GCODE_ACCENT_BORDER_COLOR : MAT_RED;
      break;

    default:
      strcpy(text, "Back");
      break;
  }

  drawStandardValue(&icTemplateActionKeyRect[index], VALUE_STRING, text, FONT_SIZE_NORMAL, fontColor, bgColor, 1, true);
}

static void icTemplateDrawActionMenu(void)
{
  GUI_SetBkColor(GCODE_PANEL_BG_COLOR);
  GUI_ClearRect(0, 0, LCD_WIDTH, LCD_HEIGHT);

  for (uint8_t i = 0; i < IC_TEMPLATE_ACTION_KEY_COUNT; i++)
  {
    icTemplateDrawActionButton(i, false);
  }
}

static bool icTemplateActionPopup(uint8_t templateIndex)
{
  KEY_VALUES key_num = KEY_IDLE;

  if (templateIndex >= icTemplateCount)
    return false;

  icTemplateActionIndex = templateIndex;
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(icTemplateActionKeyRect), icTemplateActionKeyRect, icTemplateDrawActionButton, NULL);
  icTemplateDrawActionMenu();

  while (MENU_IS(menuGcode))
  {
    key_num = menuKeyGetValue();

    if (key_num == KEY_IDLE)
    {
      loopBackEnd();
      continue;
    }

    switch (key_num)
    {
      case 0:
        selectedIcTemplateIndex = templateIndex;
        setMillingTotalDepth(icTemplates[templateIndex].totalDepth);
        icTemplateCloseListPopup();
        return true;

      case 1:
      {
        float newDepth = editFloatValue(MIN_MILLING_TOTAL_DEPTH, 1.0f,
                                        icTemplates[templateIndex].totalDepth,
                                        icTemplates[templateIndex].totalDepth);

        if (newDepth >= MIN_MILLING_TOTAL_DEPTH && newDepth <= 1.0f)
        {
          icTemplates[templateIndex].totalDepth = newDepth;
          selectedIcTemplateIndex = templateIndex;
          setMillingTotalDepth(newDepth);

          if (icTemplateWriteFile())
          {
            popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, "Đã sửa độ sâu mẫu");
          }
          else
          {
            popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Không ghi được file mẫu");
          }
        }

        icTemplateOpenListMenu();
        return false;
      }

      case 2:
        if (icTemplateDelete(templateIndex))
        {
          popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, "Đã xóa mẫu");
        }
        else
        {
          popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Không xóa được mẫu");
        }

        icTemplateOpenListMenu();
        return false;

      default:
        icTemplateOpenListMenu();
        return false;
    }

    loopBackEnd();
  }

  icTemplateOpenListMenu();
  return false;
}

static bool icTemplateListPopup(void)
{
  KEY_VALUES key_num = KEY_IDLE;

  if (!icTemplatesLoaded)
    icTemplateLoadFile();

  icTemplateCurrentFolder[0] = '\0';
  icTemplatePage = 0;
  icTemplateOpenListMenu();

  while (MENU_IS(menuGcode))
  {
    key_num = menuKeyGetValue();

    if (key_num == KEY_IDLE)
    {
      loopBackEnd();
      continue;
    }

    if (key_num < IC_TEMPLATE_PAGE_SIZE)
    {
      uint8_t visibleIndex = icTemplatePage * IC_TEMPLATE_PAGE_SIZE + key_num;
      uint8_t templateIndex = 0;
      char folder[IC_TEMPLATE_FOLDER_LEN];
      IC_TEMPLATE_ITEM_TYPE itemType = IC_TEMPLATE_ITEM_NONE;

      if (icTemplateGetVisibleItem(visibleIndex, &itemType, &templateIndex, folder, sizeof(folder)))
      {
        if (itemType == IC_TEMPLATE_ITEM_FOLDER)
        {
          terminalCopyString(icTemplateCurrentFolder, folder, sizeof(icTemplateCurrentFolder));
          icTemplatePage = 0;
          icTemplateOpenListMenu();
        }
        else if (icTemplateActionPopup(templateIndex))
        {
          return true;
        }
      }
    }
    else if (key_num == IC_TEMPLATE_PAGE_SIZE)
    {
      if (icTemplatePage > 0)
      {
        icTemplatePage--;
        icTemplateDrawList();
      }
    }
    else if (key_num == IC_TEMPLATE_PAGE_SIZE + 1)
    {
      if ((icTemplatePage + 1) * IC_TEMPLATE_PAGE_SIZE < icTemplateGetVisibleCount())
      {
        icTemplatePage++;
        icTemplateDrawList();
      }
    }
    else
    {
      if (icTemplateCurrentFolder[0] != '\0')
      {
        icTemplateCurrentFolder[0] = '\0';
        icTemplatePage = 0;
        icTemplateOpenListMenu();
      }
      else
      {
        icTemplateCloseListPopup();
        return false;
      }
    }

    loopBackEnd();
  }

  icTemplateCloseListPopup();
  return false;
}

// Public function to update G-code position from M114 response (called from Mainboard_AckHandler)
void updateGcodeKeyboardPosition(const char * response)
{
  // Only update if we're in the gcode keyboard menu
  if (MENU_IS(menuGcode))
  {
    bool previewChanged = updateGcodePositionFromResponse(response);

    gcodeDrawPositionStatus();

    if (previewChanged)
      gcodeDrawStatus();
  }
}

// Main function for GCode 4x4 keyboard (can be called from menuGcode)
void menuGcodeKeyboard(void)
{
  KEY_VALUES key_num = KEY_IDLE;
  uint8_t lastBufferIndex = 0xFF;  // Trigger initial text draw
  
  if (!icTemplatesLoaded)
    icTemplateLoadFile();

  // Initialize
  gcodeBufferIndex = 0;
  gcodeBuffer[0] = '\0';
  gcodeNumpad = true;
  gcodeNormalizeMoveStepIndex();
  gcodeLastPositionStatus[0] = '\0';
  gcodePositionStatusDirty = true;

  // If a previous milling pass exists, preload its 4 corners so Pass 2+
  // can continue directly without sending M114 back to the machine.
  if (lastPassData.isValid && planeCorners.cornerCount < 4)
  {
    prepareNextPassPreview();
  }
  
  // Set to relative positioning mode for movement
  mustStoreCmd("G91\n");
  // ← BỎ: mustStoreCmd("M114\n"); - Để user nhấn M114 button để lấy tọa độ

  #if LCD_ENCODER_SUPPORT
    encoderPosition = 0;  // Reset encoder position on menu entry
  #endif
  
  gcodeKeyboardDrawMenu();

  while (MENU_IS(menuGcode))  // Stay until user exits
  {
    #if LCD_ENCODER_SUPPORT
      LCD_Enc_CheckSteps();  // Process encoder steps FIRST
    #endif
    
    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_IDLE:
        break;

      case (KEY_VALUES)GKEY_G_CLEAR:
      {
        uint8_t deletedFileCount = 0;
        uint8_t failedFileCount = 0;
        bool deleteReady = gcodeDeleteSavedFiles(&deletedFileCount, &failedFileCount);

        gcodeBufferIndex = 0;
        // Also clear commands array
        gcodeCommandCount = 0;
        gcodeDisplayStart = 0;  // Reset scroll position
        gcodeBuffer[0] = '\0';
        gcodeFilename[0] = '\0';
        // Reset the entire milling state so the next run is a true Pass 1
        clearLastPassData();
        clearPlaneCornerState();
        gcodePosition.isValid = false;
        selectedIcTemplateIndex = -1;
        setMillingTotalDepth(DEFAULT_MILLING_TOTAL_DEPTH);

        if (!deleteReady)
        {
          popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Đã bỏ chọn nhưng chưa mount được thẻ để xóa file G-code");
        }
        else if (failedFileCount > 0)
        {
          char msg[100];
          sprintf(msg, "Đã xóa %d file, còn %d file chưa xóa", deletedFileCount, failedFileCount);
          popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, msg);
        }
        else if (deletedFileCount > 0)
        {
          char msg[100];
          sprintf(msg, "Đã xóa %d file G-code", deletedFileCount);
          popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, msg);
        }
        else
        {
          popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, "Đã bỏ chọn và xóa dữ liệu pass");
        }

        gcodeDrawStatus(); // Refresh status preview
        break;
      }

      case (KEY_VALUES)GKEY_G_SEND:  // Save function
      {
        FILE_SOURCE generatedFileSource = infoFile.source;

        // Pass 2+: always restore previous corners before saving so the user
        // does not need to press M114 and the machine never reprobes old cuts.
        if (lastPassData.isValid && (!planeCornerReady || planeCorners.cornerCount != 4)) {
          prepareNextPassPreview();
        }

        // When 4 corners ready AND previous pass data exists → Automatically use incremental mode
        if (planeCornerReady && planeCorners.cornerCount == 4) {
          if (lastPassData.isValid) {
            // ★ AUTOMATIC INCREMENTAL MODE: Previous pass found, use it
            useIncrementalMode = true;
            char msg[100];
            sprintf(msg, "Pass %d: Incremental from %.2fmm", lastPassData.passCount + 1, lastPassData.lastFinalDepth);
            popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, msg);
          } else {
            // ★ STANDARD MODE: First pass or data cleared
            useIncrementalMode = false;
          }
          
          // Now proceed with saving
          if (savePlaneCornersToFile()) {
            if (autoStartGeneratedGcodeFile(generatedFileSource, gcodeFilename)) {
              break;
            }

            gcodeCommandCount = 0;
            gcodeDisplayStart = 0;
            gcodeBufferIndex = 0;
            gcodeBuffer[0] = '\0';
            prepareNextPassPreview();
            gcodeDrawKeyboard();
            gcodeDrawStatus();
          }
        }
        // Standard case: Save plane corners to file if 4 corners are ready
        else if (planeCornerReady && planeCorners.cornerCount == 4) {
          if (savePlaneCornersToFile()) {
            if (autoStartGeneratedGcodeFile(generatedFileSource, gcodeFilename)) {
              break;
            }

            // Clear after saving
            gcodeCommandCount = 0;
            gcodeDisplayStart = 0;
            gcodeBufferIndex = 0;
            gcodeBuffer[0] = '\0';
            prepareNextPassPreview();
            gcodeDrawKeyboard();
            gcodeDrawStatus();
          }
        }
        // Priority 2: Save manual G-code commands if any exist (and no corners ready)
        else if (gcodeCommandCount > 0) {
          // Save to file
          if (saveGcodeToFile()) {
            // Show success message briefly
            popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, LABEL_PROCESS_COMPLETED);
            
            // Clear after saving
            gcodeCommandCount = 0;
            gcodeDisplayStart = 0;
            gcodeBufferIndex = 0;
            gcodeBuffer[0] = '\0';
            gcodeDrawStatus();
          }
          else {
            // Show error message
            popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, LABEL_PROCESS_ABORTED);
          }
        }
        break;
      }

      case (KEY_VALUES)GKEY_G_ABC_123:
        icTemplateListPopup();
        break;

      case (KEY_VALUES)GKEY_G_X_AXIS:
        gcode_nowAxis = X_AXIS;
        gcodeDrawKeyboard();  // Redraw to highlight selected axis
        break;
        
      case (KEY_VALUES)GKEY_G_Y_AXIS:
        gcode_nowAxis = Y_AXIS;
        gcodeDrawKeyboard();  // Redraw to highlight selected axis
        break;
        
      case (KEY_VALUES)GKEY_G_Z_AXIS:
        gcode_nowAxis = Z_AXIS;
        gcodeDrawKeyboard();  // Redraw to highlight selected axis
        break;
        
      case (KEY_VALUES)GKEY_G_STEP:
        gcodeCycleMoveStepIndex();
        gcodeDrawKeyboard();  // Redraw to show new step value
        break;

      case (KEY_VALUES)GKEY_G_GO_HOME:
        mustStoreCmd("M211 S1\n");
        mustStoreCmd("G28\n");
        // mustStoreCmd("G90\n");
        // mustStoreCmd("G1 Y100.00 F1500\n");
        // mustStoreCmd("G91\n");
        gcodeHomeReady = true;
        gcodeDrawStatus();
        break;

      case (KEY_VALUES)GKEY_G_SET_IC_ORIGIN:
        if (!gcodeHomeReady)
        {
          popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, "Về gốc trước");
          gcodeDrawStatus();
          break;
        }

        mustStoreCmd("G90\n");
        mustStoreCmd("G1 Z19 F1500\n");
        mustStoreCmd("G1 X35.00 Y40.00 F1500\n");
        mustStoreCmd("G91\n");
        gcodeDrawStatus();
        break;

      case (KEY_VALUES)GKEY_G_BACK:
        if (infoMenu.cur == 0)
          REPLACE_MENU(menuStatus);
        else
          CLOSE_MENU();
        break;

      default:
        // Handle special key cases that don't fit in KEY_VALUES enum
        if (gcodeIsHiddenWorkflowButton((uint8_t)key_num))
        {
          break;
        }
        else if (key_num == (KEY_VALUES)GKEY_G_IC_TOUCH)
        {
          mustStoreCmd("G90\n");
          OPEN_MENU(menuBedLeveling);
        }
        else if (key_num >= (KEY_VALUES)GKEY_G_CORNER_1_TOUCH && key_num <= (KEY_VALUES)GKEY_G_CORNER_4_TOUCH)
        {
          if (!gcodeHomeReady)
          {
            popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, "Về gốc trước");
            gcodeDrawStatus();
            break;
          }

          if (lastPassData.isValid)
          {
            if (planeCorners.cornerCount < 4)
              loadLastPassData();

            popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, "Pass tiep theo dung lai 4 goc da luu\nNhan CLEAR neu muon do lai");
          }
          else
          {
            pendingPlaneCornerIndex = key_num - (KEY_VALUES)GKEY_G_CORNER_1_TOUCH;

            if (pendingPlaneCornerIndex == 1 || pendingPlaneCornerIndex == 3)
            {
              pendingPlaneCornerIndex = -1;
              popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, "Chi can do goc 1 va goc 3\nGoc 2 va 4 se tu suy ra");
              gcodeDrawStatus();
              break;
            }

            m114_save_corner = true;
            mustStoreCmd("M114\n");
            mustStoreCmd("G4 S0.1\n");

            {
              char msg[80];
              sprintf(msg, "Dang lay toa do goc %d...", pendingPlaneCornerIndex + 1);
              popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, msg);
            }
          }

          gcodeDrawStatus();
        }
        else if (key_num == (KEY_VALUES)GKEY_G_SPACE)
        {
          if (gcodeBufferIndex > 0 && gcodeBufferIndex < CMD_MAX_SIZE - 2)
          {
            gcodeBuffer[gcodeBufferIndex++] = ' ';
            gcodeDrawStatus(); // Update status preview
          }
        }
        else if (key_num == (KEY_VALUES)GKEY_G_DEL)
        {
          // Delete last command from array
          if (gcodeCommandCount > 0)
          {
            gcodeCommandCount--;  // Remove last command
            
            // Adjust display scroll if needed
            uint8_t maxVisibleLines = (LCD_HEIGHT - 2 * ROW_HEIGHT) / 18;
            uint8_t lastPageStart = (gcodeCommandCount > maxVisibleLines) ? (gcodeCommandCount - maxVisibleLines) : 0;
            if (gcodeDisplayStart > lastPageStart)
              gcodeDisplayStart = lastPageStart;
            
            gcodeDrawStatus();  // Refresh display
          }
        }
        else if (key_num == (KEY_VALUES)GKEY_G_ENTER)  // Add command
        {
          if (gcodeBufferIndex > 0)
          {
            // Add current buffer to commands array (auto-scrolls to show new command)
            addGcodeCommand(gcodeBuffer);
            
            // Clear buffer for next command
            gcodeBufferIndex = 0;
            gcodeBuffer[0] = '\0';
            
            // gcodeDrawText();   // Clear input area
            gcodeDrawStatus(); // Refresh status preview
          }
        }
          else if (key_num == (KEY_VALUES)GKEY_G_EXIT)  // Open Print menu
        {
            menuPrintTftSdFromGcode();
        }
        // Handle keyboard character keys (skip special function keys)
        else if (key_num > (KEY_VALUES)GKEY_G_STEP && key_num < (KEY_VALUES)GKEY_G_SPACE && gcodeBufferIndex < CMD_MAX_SIZE - 2)
        {
          if (key_num == (KEY_VALUES)12)
          {
            popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, "Tổng độ sâu hiện tại");
          }
          else if (key_num == (KEY_VALUES)13)
          {
            char depthMsg[50];
            sprintf(depthMsg, "AUTO %.2f", fabsf(getStartDepth()));
            popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, depthMsg);
          }
          else if (key_num == (KEY_VALUES)14)
          {
            popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, "Lop cat dau tien dang de AUTO");
          }
          else if (key_num == (KEY_VALUES)15)
          {
            gcodeBufferIndex = 0;
            gcodeBuffer[0] = '\0';

            if (icTemplateNameInputPopup())
            {
              if (icTemplateSaveCurrentDepth(gcodeBuffer))
              {
                char msg[80];
                sprintf(msg, "Đã lưu mẫu\n%s = %.2f", icTemplateGetSelectedName(), getMillingTotalDepth());
                popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, msg);
              }
              else
              {
                popupReminder(DIALOG_TYPE_ERROR, LABEL_WARNING, "Không lưu được mẫu");
              }

              gcodeDrawKeyboard();
              gcodeDrawStatus();
            }
          }
          // Handle total milling depth control buttons (Row 4, indices 16-19)
          else if (key_num == (KEY_VALUES)16)  // "-" for milling total depth
          {
            // Decrease milling total depth - decrement by 0.01mm
            float currentDepth = getMillingTotalDepth();
            float minDepth = getMinimumMillingTotalDepth();
            if (currentDepth > minDepth)
            {
              gcodeSetCustomMillingDepth(currentDepth - 0.01f);
              // Display update: show current depth in status
              char depthMsg[50];
              sprintf(depthMsg, "%.2f", getMillingTotalDepth());
              popupReminder(DIALOG_TYPE_INFO, LABEL_WARNING, depthMsg);
              gcodeDrawKeyboard();
              gcodeDrawStatus();
            }
          }
          else if (key_num == (KEY_VALUES)17)  // Center key cycles milling depth templates
          {
            icTemplateListPopup();
            gcodeDrawKeyboard();
            gcodeDrawStatus();
          }
          else if (key_num == (KEY_VALUES)18)  // "+" for milling total depth
          {
            // Increase milling total depth - increment by 0.01mm
            float currentDepth = getMillingTotalDepth();
            if (currentDepth < 1.0f)  // Maximum 1.00mm
            {
              gcodeSetCustomMillingDepth(currentDepth + 0.01f);
              // Display update: show current depth in status
              char depthMsg[50];
              sprintf(depthMsg, "%.2f", getMillingTotalDepth());
              popupReminder(DIALOG_TYPE_INFO, LABEL_WARNING, depthMsg);
              gcodeDrawKeyboard();
              gcodeDrawStatus();
            }
          }
          else if (key_num == (KEY_VALUES)GKEY_G_LIGHT)
          {
            gcodeLightOn = !gcodeLightOn;
            mustStoreCmd(gcodeLightOn ? "M106 P1 S255\n" : "M106 P1 S0\n");
            gcodeDrawKeyboard();
            gcodeDrawStatus();
          }
          else
          {
            // Other character keys - get text from keyboard layout
            const char * text = (gcodeNumpad) ? gcodeKeyLayout123[key_num] : gcodeKeyLayoutABC[key_num];
          
            // Special handling for G-code command keys
            if (strcmp(text, "G28") == 0)
          {
            mustStoreCmd("M211 S1\n");  
            mustStoreCmd("G28\n");

            gcodeDrawStatus();  // Update status preview
          }
          else if (strcmp(text, "G92") == 0)
          {
            // G92 - Set current position as origin (add to array only)
            mustStoreCmd("G92 X0 Y0 Z0\n");
            // addGcodeCommand("G92 X0 Y0 Z0");
            gcodeDrawStatus();  // Update status preview
          }
          
          else if (strcmp(text, "M114") == 0)
          {
            // ★ M114 - PASS 1: Capture | PASS 2+: Load previous (BLOCK retro-probing!)
            if (lastPassData.isValid) {
              // PASS 2+: BLOCK NEW PROBING - Force load previous corners
              if (planeCorners.cornerCount < 4) {
                // Load previous corners - don't probe at old milling locations!
                loadLastPassData();
                char msg[100];
                sprintf(msg, "[PASS %d] Loaded 4 previous corners!\nProbing locked to prevent tool crash!", lastPassData.passCount + 1);
                popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, msg);
              } else {
                // Already loaded
                char msg[100];
                sprintf(msg, "[PASS %d] Corners loaded from Pass 1\nUse CLEAR to reprobe", lastPassData.passCount + 1);
                popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, msg);
              }
            } else {
              // PASS 1: NEW CAPTURE - First time probing
              int8_t nextCornerIndex = getNextMissingPlaneCornerIndex();
              if (nextCornerIndex >= 0) {
                // Capture new corner positions
                pendingPlaneCornerIndex = nextCornerIndex;
                mustStoreCmd("M114\n");
                mustStoreCmd("G4 S0.1\n");
                m114_save_corner = true;
                char msg[100];
                sprintf(msg, "[PASS 1] Capturing corner %d...", pendingPlaneCornerIndex + 1);
                popupReminder(DIALOG_TYPE_INFO, LABEL_INFO, msg);
              } else {
                // Corners 2 and 4 were derived automatically after capturing 1 and 3.
                popupReminder(DIALOG_TYPE_SUCCESS, LABEL_INFO, "[PASS 1] Du mat phang tu goc 1 va 3!\nPress G to mill");
              }
            }
            
            gcodeDrawStatus();  // Update status preview
          }
          else if (strcmp(text, "ABL") == 0)
          {
            // ABL - Probe bed with LED control sequence (with timing)
            // Gửi lệnh LED ON
            mustStoreCmd("M42 P8 S255\n");  // LED ON
            
            // Gửi lệnh LED OFF với delay (dùng G4 - dwell command)
            mustStoreCmd("G4 S0.1\n");      // Delay 100ms
            mustStoreCmd("M42 P8 S0\n");    // LED OFF
            
            // Gửi lệnh LED ON lại với delay
            mustStoreCmd("G4 S0.1\n");      // Delay 100ms
            mustStoreCmd("M42 P8 S255\n");  // LED ON
            
            // // Gửi lệnh Probe với delay
            // mustStoreCmd("G4 S0.1\n");      // Delay 100ms
            // mustStoreCmd("G30\n");          // Probe

            // mustStoreCmd("G4 S0.1\n");      // Delay 100ms
            // mustStoreCmd("M42 P8 S255\n");  // LED ON
            
            gcodeDrawStatus();  // Update status preview
          }
          else if (strcmp(text, "-") == 0 || strcmp(text, "+") == 0 || strcmp(text, "0") == 0)
          {
            // These are handled by index-based handlers above
            // Do nothing - deprecated strcmp handlers removed
          }
          else
          {
            // Regular character input
            gcodeBuffer[gcodeBufferIndex++] = text[0];
            gcodeDrawStatus(); // Update status preview
          }
          }  // Close the else block for non-special keys
        }
        break;

      case KEY_INCREASE:
        // Encoder clockwise rotation - move positive direction (execute immediately)
        {
          float amount = gcodeGetEncoderMoveAmount(gcodeGetCurrentMoveStep());
          storeGcodeMoveCmd(gcode_nowAxis, -amount);  // Send to machine directly
          gcodeDrawPositionStatus();
        }
        break;

      case KEY_DECREASE:
        // Encoder counter-clockwise rotation - move negative direction (execute immediately)
        {
          float amount = gcodeGetEncoderMoveAmount(gcodeGetCurrentMoveStep());
          storeGcodeMoveCmd(gcode_nowAxis, amount);  // Send to machine directly
          gcodeDrawPositionStatus();
        }
        break;
    }

    #if LCD_ENCODER_SUPPORT
      // Handle encoder button press - simplified approach
      static bool buttonPressed = false;
      static uint32_t buttonPressStart = 0;
      static uint32_t lastShortPressTime = 0;
      
      bool currentButtonState = LCD_Enc_ReadBtn(0);  // Read current button state
      uint32_t currentTime = OS_GetTimeMs();
      
      if (currentButtonState && !buttonPressed)
      {
        // Button just pressed
        buttonPressed = true;
        buttonPressStart = currentTime;
      }
      else if (!currentButtonState && buttonPressed)
      {
        // Button just released
        uint32_t pressDuration = currentTime - buttonPressStart;
        
        if (pressDuration >= 1000)  // Long press (1 second)
        {
          gcodeStepMode = !gcodeStepMode;  // Toggle mode
          gcodeDrawKeyboard();  // Redraw to show mode change
        }
        else if (pressDuration >= 50 && (currentTime - lastShortPressTime > 300))  // Short press with debounce
        {
          if (!gcodeStepMode)
          {
            // Cycle through axes: X -> Y -> Z -> back to X
            gcode_nowAxis = (gcode_nowAxis + 1) % 3;  // 0,1,2 for X,Y,Z
          }
          else
          {
            // Cycle through step sizes
            gcodeCycleMoveStepIndex();
          }
          
          gcodeDrawKeyboard();  // Redraw to show changes
          lastShortPressTime = currentTime;
        }
        
        buttonPressed = false;
      }
    #endif
    // Update text display and status if buffer changed
    if (lastBufferIndex != gcodeBufferIndex)
    {
      lastBufferIndex = gcodeBufferIndex;
      gcodeBuffer[gcodeBufferIndex] = '\0';
      // gcodeDrawText();
      gcodeDrawStatus(); // Keep status preview in sync
    }

    if (nextScreenUpdate(GCODE_POS_REFRESH_TIME))
    {
      coordinateQuery(0);
    }

    loopBackEnd();
  }
  // Cleanup
  mustStoreCmd("G90\n");  // Restore absolute positioning mode
  GUI_RestoreColorDefault();
}
