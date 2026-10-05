/*****************************************************************************
* | File        : LCD_Touch.h
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : LCD Touch Pad Driver and Drawing Canvas
* | Info        : 280x240 drawing canvas with continuous Bresenham strokes
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Full double-buffering canvas with adaptive 28x28 downsampling
******************************************************************************/

#ifndef __LCD_TOUCH_H_
#define __LCD_TOUCH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "pico/float.h"
#include "pico/multicore.h"
#include "pico/sync.h"

#define TP_PRESS_DOWN           0x80
#define TP_PRESSED              0x40

#define DATA_READY_FLAG         0xABCDEF01
#define WRITE_FAILED_FLAG       0x12345678
#define SD_UNAVAILABLE_FLAG     0xBADCA4D0
#define WRITE_SUCCESS_FLAG      0xABCDEF02

// ---- Capture area (280x240 canvas from lcd_sd_card_example) ----------------
#define BOX_X0 100
#define BOX_Y0 40
#define BOX_X1 380  // right edge (exclusive)
#define BOX_Y1 280  // bottom edge (exclusive)

#define BOX_W (BOX_X1 - BOX_X0)  // 280
#define BOX_H (BOX_Y1 - BOX_Y0)  // 240

#define DOWNSAMPLED_SIZE 28

// Touch screen structure
typedef struct {
    POINT Xpoint0;
    POINT Ypoint0;
    POINT Xpoint;
    POINT Ypoint;
    uint16_t Z1;
    uint8_t chStatus;
    uint8_t chType;
    int16_t iXoff;
    int16_t iYoff;
    float fXfac;
    float fYfac;
    LCD_SCAN_DIR TP_Scan_Dir;
} TP_DEV;

// Brush structure
typedef struct {
    POINT Xpoint;
    POINT Ypoint;
    COLOR Color;
    DOT_PIXEL DotPixel;
} TP_DRAW;

// Shared data transfer structure for multicore double-buffering
typedef struct {
    size_t data_len;
    uint8_t *data;
} TP_DATA;

void TP_GetAdFac(void);
void TP_Dialog(void);
void TP_Save(void);
void TP_DrawBoard(void);
void TP_Init(LCD_SCAN_DIR Lcd_ScanDir, TP_DATA *tp_data_ptr, mutex_t *mutex);
void TP_SetSaveBusy(bool busy);
bool TP_GetSaveBusy(void);
void TP_ShowStatus(const char *text, COLOR color);
void TP_ShowTelemetry(const char *text);

#ifdef __cplusplus
}
#endif

#endif  // __LCD_TOUCH_H_
