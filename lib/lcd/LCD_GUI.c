/*****************************************************************************
* | File        : LCD_GUI.c
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
#include "LCD_GUI.h"

extern LCD_DIS sLCD_DIS;
extern uint8_t id;
/*****************************************************************************/
// function: Coordinate conversion
/*****************************************************************************/
void GUI_Swop(POINT Point1, POINT Point2)
{
    POINT Temp;
    Temp = Point1;
    Point1 = Point2;
    Point2 = Temp;
}

/*****************************************************************************/
// function: Draw Point(Xpoint, Ypoint) Fill the color
/*****************************************************************************/
void GUI_DrawPoint(POINT Xpoint, POINT Ypoint, COLOR Color,
                   DOT_PIXEL Dot_Pixel, DOT_STYLE DOT_STYLE)
{
    if (Xpoint > sLCD_DIS.LCD_Dis_Column || Ypoint > sLCD_DIS.LCD_Dis_Page) {
        return;
    }

    uint16_t XDir_Num, YDir_Num;
    if (DOT_STYLE == DOT_STYLE_DFT) {
        for (XDir_Num = 0; XDir_Num < 2 * Dot_Pixel - 1; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num < 2 * Dot_Pixel - 1; YDir_Num++) {
                LCD_SetPointlColor(Xpoint + XDir_Num - Dot_Pixel,
                                   Ypoint + YDir_Num - Dot_Pixel, Color);
            }
        }
    } else {
        for (XDir_Num = 0; XDir_Num < Dot_Pixel; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num < Dot_Pixel; YDir_Num++) {
                LCD_SetPointlColor(Xpoint + XDir_Num - 1,
                                   Ypoint + YDir_Num - 1, Color);
            }
        }
    }
}

/*****************************************************************************/
// function: Draw a line of arbitrary slope
/*****************************************************************************/
void GUI_DrawLine(POINT Xstart, POINT Ystart, POINT Xend, POINT Yend,
                  COLOR Color, LINE_STYLE Line_Style, DOT_PIXEL Dot_Pixel)
{
    if (Xstart > sLCD_DIS.LCD_Dis_Column || Ystart > sLCD_DIS.LCD_Dis_Page ||
        Xend > sLCD_DIS.LCD_Dis_Column || Yend > sLCD_DIS.LCD_Dis_Page) {
        return;
    }

    if (Xstart > Xend)
        GUI_Swop(Xstart, Xend);
    if (Ystart > Yend)
        GUI_Swop(Ystart, Yend);

    POINT Xpoint = Xstart;
    POINT Ypoint = Ystart;
    int32_t dx = (Xend >= Xstart) ? (Xend - Xstart) : (Xstart - Xend);
    int32_t dy = (Yend <= Ystart) ? (Yend - Ystart) : (Ystart - Yend);

    int32_t XAddway = (Xstart < Xend) ? 1 : -1;
    int32_t YAddway = (Ystart < Yend) ? 1 : -1;

    int32_t Esp = dx + dy;
    int8_t Line_Style_Temp = 0;

    for (;;) {
        Line_Style_Temp++;
        if (Line_Style == LINE_DOTTED && Line_Style_Temp % 3 == 0) {
            GUI_DrawPoint(Xpoint, Ypoint, LCD_BACKGROUND,
                          Dot_Pixel, DOT_STYLE_DFT);
            Line_Style_Temp = 0;
        } else {
            GUI_DrawPoint(Xpoint, Ypoint, Color, Dot_Pixel, DOT_STYLE_DFT);
        }
        if (2 * Esp >= dy) {
            if (Xpoint == Xend) break;
            Esp += dy;
            Xpoint += XAddway;
        }
        if (2 * Esp <= dx) {
            if (Ypoint == Yend) break;
            Esp += dx;
            Ypoint += YAddway;
        }
    }
}

/*****************************************************************************/
// function: Draw a rectangle
/*****************************************************************************/
void GUI_DrawRectangle(POINT Xstart, POINT Ystart, POINT Xend, POINT Yend,
                       COLOR Color, DRAW_FILL Filled, DOT_PIXEL Dot_Pixel)
{
    if (Xstart > sLCD_DIS.LCD_Dis_Column || Ystart > sLCD_DIS.LCD_Dis_Page ||
        Xend > sLCD_DIS.LCD_Dis_Column || Yend > sLCD_DIS.LCD_Dis_Page) {
        return;
    }

    if (Xstart > Xend)
        GUI_Swop(Xstart, Xend);
    if (Ystart > Yend)
        GUI_Swop(Ystart, Yend);

    if (Filled) {
        LCD_SetArealColor(Xstart, Ystart, Xend, Yend, Color);
    } else {
        GUI_DrawLine(Xstart, Ystart, Xend, Ystart,
                     Color, LINE_SOLID, Dot_Pixel);
        GUI_DrawLine(Xstart, Ystart, Xstart, Yend,
                     Color, LINE_SOLID, Dot_Pixel);
        GUI_DrawLine(Xend, Yend, Xend, Ystart,
                     Color, LINE_SOLID, Dot_Pixel);
        GUI_DrawLine(Xend, Yend, Xstart, Yend,
                     Color, LINE_SOLID, Dot_Pixel);
    }
}

/*****************************************************************************/
// function: Show English characters
/*****************************************************************************/
void GUI_DisChar(POINT Xpoint, POINT Ypoint, const char Acsii_Char,
                 sFONT* Font, COLOR Color_Background, COLOR Color_Foreground)
{
    POINT Page, Column;

    if (Xpoint > sLCD_DIS.LCD_Dis_Column || Ypoint > sLCD_DIS.LCD_Dis_Page) {
        return;
    }

    uint32_t bpl = Font->Width / 8 + (Font->Width % 8 ? 1 : 0);
    uint32_t Char_Offset = (Acsii_Char - ' ') * Font->Height * bpl;
    const unsigned char *ptr = &Font->table[Char_Offset];

    for (Page = 0; Page < Font->Height; Page++) {
        for (Column = 0; Column < Font->Width; Column++) {
            if (*ptr & (0x80 >> (Column % 8))) {
                GUI_DrawPoint(Xpoint + Column, Ypoint + Page,
                              Color_Foreground,
                              DOT_PIXEL_DFT, DOT_STYLE_DFT);
            } else {
                GUI_DrawPoint(Xpoint + Column, Ypoint + Page,
                              Color_Background,
                              DOT_PIXEL_DFT, DOT_STYLE_DFT);
            }
            if (Column % 8 == 7)
                ptr++;
        }
        if (Font->Width % 8 != 0)
            ptr++;
    }
}

/*****************************************************************************/
// function: Display the string
/*****************************************************************************/
void GUI_DisString_EN(POINT Xstart, POINT Ystart, const char *pString,
                      sFONT* Font, COLOR Color_Background,
                      COLOR Color_Foreground)
{
    POINT Xpoint = Xstart;
    POINT Ypoint = Ystart;

    if (Xstart > sLCD_DIS.LCD_Dis_Column || Ystart > sLCD_DIS.LCD_Dis_Page) {
        return;
    }

    while (*pString != '\0') {
        if ((Xpoint + Font->Width) > sLCD_DIS.LCD_Dis_Column) {
            Xpoint = Xstart;
            Ypoint += Font->Height;
        }

        if ((Ypoint + Font->Height) > sLCD_DIS.LCD_Dis_Page) {
            Xpoint = Xstart;
            Ypoint = Ystart;
        }
        GUI_DisChar(Xpoint, Ypoint, *pString, Font,
                    Color_Background, Color_Foreground);

        pString++;
        Xpoint += Font->Width;
    }
}

