/*****************************************************************************
* | File        : LCD_GUI.h
* | Author      : Waveshare team
* | Function    : Achieve drawing: draw points, lines, boxes, circles and
*                 their size, solid dotted line, solid rectangle hollow
*                 rectangle, solid circle hollow circle
* | Info        : Achieve display characters: Display a single character,
*                 string, number
*                 Achieve time display: adaptive size display time minutes
*                 and seconds
*----------------
* | This version: V1.0
* | Date        : 2017-08-16
* | Info        : Basic version
******************************************************************************/

/****************************Upper application layer**************************/
#ifndef TFLITE_INFERENCE_TEST_LCD_GUI_H_
#define TFLITE_INFERENCE_TEST_LCD_GUI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "LCD_Driver.h"
#include "fonts.h"

/*****************************************************************************/
// function: dot pixel
/*****************************************************************************/
typedef enum {
    DOT_PIXEL_1X1 = 1,  // dot pixel 1 x 1
    DOT_PIXEL_2X2,      // dot pixel 2 X 2
    DOT_PIXEL_3X3,      // dot pixel 3 X 3
    DOT_PIXEL_4X4,      // dot pixel 4 X 4
    DOT_PIXEL_5X5,      // dot pixel 5 X 5
    DOT_PIXEL_6X6,      // dot pixel 6 X 6
    DOT_PIXEL_7X7,      // dot pixel 7 X 7
    DOT_PIXEL_8X8,      // dot pixel 8 X 8
} DOT_PIXEL;
#define DOT_PIXEL_DFT DOT_PIXEL_1X1  // default dot pilex

/*****************************************************************************/
// function: dot Fill style
/*****************************************************************************/
typedef enum {
    DOT_FILL_AROUND = 1,  // dot pixel 1 x 1
    DOT_FILL_RIGHTUP,     // dot pixel 2 X 2
} DOT_STYLE;
#define DOT_STYLE_DFT DOT_FILL_AROUND  // default dot pilex

/*****************************************************************************/
// function: solid line and dotted line
/*****************************************************************************/
typedef enum {
    LINE_SOLID = 0,
    LINE_DOTTED,
} LINE_STYLE;

/*****************************************************************************/
// function: DRAW Internal fill
/*****************************************************************************/
typedef enum {
    DRAW_EMPTY = 0,
    DRAW_FULL,
} DRAW_FILL;

// Display color definitions
#define WHITE          0xFFFF
#define BLACK          0x0000
#define BLUE           0x001F
#define BRED           0xF81F
#define GRED           0xFFE0
#define GBLUE          0x07FF
#define RED            0xF800
#define MAGENTA        0xF81F
#define GREEN          0x07E0
#define CYAN           0x7FFF
#define YELLOW         0xFFE0
#define BROWN          0xBC40
#define BRRED          0xFC07
#define GRAY           0x8430

#define LCD_BACKGROUND  WHITE
#define FONT_BACKGROUND WHITE

// Drawing
void GUI_DrawPoint(POINT Xpoint, POINT Ypoint, COLOR Color,
                   DOT_PIXEL Dot_Pixel, DOT_STYLE Dot_FillWay);
void GUI_DrawLine(POINT Xstart, POINT Ystart, POINT Xend, POINT Yend,
                  COLOR Color, LINE_STYLE Line_Style, DOT_PIXEL Dot_Pixel);
void GUI_DrawRectangle(POINT Xstart, POINT Ystart, POINT Xend, POINT Yend,
                       COLOR Color, DRAW_FILL Filled, DOT_PIXEL Dot_Pixel);

// Display string
void GUI_DisChar(POINT Xstart, POINT Ystart, const char Acsii_Char,
                 sFONT* Font, COLOR Color_Background, COLOR Color_Foreground);
void GUI_DisString_EN(POINT Xstart, POINT Ystart, const char *pString,
                      sFONT* Font, COLOR Color_Background,
                      COLOR Color_Foreground);

#ifdef __cplusplus
}
#endif

#endif  // TFLITE_INFERENCE_TEST_LCD_GUI_H_
