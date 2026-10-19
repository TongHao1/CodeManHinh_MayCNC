#ifndef _PRINT_H_
#define _PRINT_H_

#ifdef __cplusplus
extern "C" {
#endif

void menuPrintFromSource(void);
void menuPrint(void);
void menuPrintTftSdFromGcode(void);
void setPrintReturnToGcode(int enabled);
int returnToGcodeFromPrintMenu(void);

#ifdef __cplusplus
}
#endif

#endif
