#include "LCD_Touch.h"
#include <stdlib.h>
#include <string.h>  
#include <stdio.h>
#include "recognizer.h"
#define BRUSH_RADIUS 6


extern LCD_DIS sLCD_DIS;
extern uint8_t id;
static TP_DEV sTP_DEV;
static TP_DRAW sTP_Draw;

static TP_DATA *tp_data = NULL;
static mutex_t *p_mutex = NULL;
static uint8_t sDrawShadow[BOX_H][BOX_W];

uint16_t TP_SavedWidth(void)  { return BOX_W; }
uint16_t TP_SavedHeight(void) { return BOX_H; }


static int last_x = -1;
static int last_y = -1;

static inline void Capture_SetPixel(uint16_t x, uint16_t y)
{
    for (int dy = -BRUSH_RADIUS; dy <= BRUSH_RADIUS; ++dy) {
        for (int dx = -BRUSH_RADIUS; dx <= BRUSH_RADIUS; ++dx) {
            if ((dx * dx + dy * dy) > (BRUSH_RADIUS * BRUSH_RADIUS))
                continue;

            int px = (int)x + dx;
            int py = (int)y + dy;

            if (px >= BOX_X0 && px < BOX_X1 &&
                py >= BOX_Y0 && py < BOX_Y1) {
                sDrawShadow[py - BOX_Y0][px - BOX_X0] = 1;
            }
        }
    }
}

static void Capture_Line(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);

    if (steps == 0) {
        Capture_SetPixel((uint16_t)x1, (uint16_t)y1);
        return;
    }

    for (int i = 0; i <= steps; ++i) {
        int x = x0 + (dx * i) / steps;
        int y = y0 + (dy * i) / steps;
        Capture_SetPixel((uint16_t)x, (uint16_t)y);
    }
}

static void TP_DumpBitmapToSerial(const uint8_t bmp[BOX_H][BOX_W])
{
    char line[BOX_W + 2];
    line[BOX_W]   = '\n';
    line[BOX_W+1] = '\0';

    for (uint16_t y = 0; y < BOX_H; ++y) {
        for (uint16_t x = 0; x < BOX_W; ++x) {
            line[x] = bmp[y][x] ? '1' : '0';
        }
        printf("%s", line);
    }
}

static uint16_t TP_Read_ADC(uint8_t CMD)
{
    uint16_t Data = 0;

    //A cycle of at least 400ns.
    DEV_Digital_Write(TP_CS_PIN, 0);

    SPI4W_Write_Byte(CMD);
    Driver_Delay_us(200);

    //Data = SPI4W_Read_Byte(0Xff);
    Data = SPI4W_Read_Byte(0X00);
    Data <<= 8; //7bit
    Data |= SPI4W_Read_Byte(0X00);
    //Data = SPI4W_Read_Byte(0Xff);
    Data >>= 3; //5bit
    DEV_Digital_Write(TP_CS_PIN, 1);
    return Data;
}

#define READ_TIMES 5 
#define LOST_NUM 1 
static uint16_t 
TP_Read_ADC_Average(uint8_t Channel_Cmd)
{
    uint8_t i, j;
    uint16_t Read_Buff[READ_TIMES];
    uint16_t Read_Sum = 0, Read_Temp = 0;
    //LCD SPI speed = 3 MHz
    spi_set_baudrate(SPI_PORT, 3000000);
   
    for (i = 0; i < READ_TIMES; i++)
    {
        Read_Buff[i] = TP_Read_ADC(Channel_Cmd);
        Driver_Delay_us(200);
    }
    //LCD SPI speed = 18 MHz
    spi_set_baudrate(SPI_PORT, 18000000);

    for (i = 0; i < READ_TIMES - 1; i++)
    {
        for (j = i + 1; j < READ_TIMES; j++)
        {
            if (Read_Buff[i] > Read_Buff[j])
            {
                Read_Temp = Read_Buff[i];
                Read_Buff[i] = Read_Buff[j];
                Read_Buff[j] = Read_Temp;
            }
        }
    }

    for (i = LOST_NUM; i < READ_TIMES - LOST_NUM; i++)
        Read_Sum += Read_Buff[i];

    Read_Temp = Read_Sum / (READ_TIMES - 2 * LOST_NUM);

    return Read_Temp;
}

static void TP_Read_ADC_XY(uint16_t *pXCh_Adc, uint16_t *pYCh_Adc)
{
    *pXCh_Adc = TP_Read_ADC_Average(0xD0);
    *pYCh_Adc = TP_Read_ADC_Average(0x90);
}

#define ERR_RANGE 50
static bool TP_Read_TwiceADC(uint16_t *pXCh_Adc, uint16_t *pYCh_Adc)
{
    uint16_t XCh_Adc1, YCh_Adc1, XCh_Adc2, YCh_Adc2;

    TP_Read_ADC_XY(&XCh_Adc1, &YCh_Adc1);
    Driver_Delay_us(10);
    TP_Read_ADC_XY(&XCh_Adc2, &YCh_Adc2);
    Driver_Delay_us(10);

    if (((XCh_Adc2 <= XCh_Adc1 && XCh_Adc1 < XCh_Adc2 + ERR_RANGE) ||
         (XCh_Adc1 <= XCh_Adc2 && XCh_Adc2 < XCh_Adc1 + ERR_RANGE)) &&
        ((YCh_Adc2 <= YCh_Adc1 && YCh_Adc1 < YCh_Adc2 + ERR_RANGE) ||
         (YCh_Adc1 <= YCh_Adc2 && YCh_Adc2 < YCh_Adc1 + ERR_RANGE)))
    {
        *pXCh_Adc = (XCh_Adc1 + XCh_Adc2) / 2;
        *pYCh_Adc = (YCh_Adc1 + YCh_Adc2) / 2;
        return true;
    }

    return false;
}

static uint8_t TP_Scan(uint8_t chCoordType)
{
   
    if (!DEV_Digital_Read(TP_IRQ_PIN))
    {

        if (chCoordType)
        {
            TP_Read_TwiceADC(&sTP_DEV.Xpoint, &sTP_DEV.Ypoint);
            //Read the screen coordinates
        }
        else if (TP_Read_TwiceADC(&sTP_DEV.Xpoint, &sTP_DEV.Ypoint))
        {

            if (LCD_2_8 == id)
            {
                sTP_Draw.Xpoint = sLCD_DIS.LCD_Dis_Column -
                                  sTP_DEV.fXfac * sTP_DEV.Xpoint -
                                  sTP_DEV.iXoff;
                sTP_Draw.Ypoint = sLCD_DIS.LCD_Dis_Page -
                                  sTP_DEV.fYfac * sTP_DEV.Ypoint -
                                  sTP_DEV.iYoff;
            }
            else
            {

                if (sTP_DEV.TP_Scan_Dir == R2L_D2U)
                { 
                    sTP_Draw.Xpoint = sTP_DEV.fXfac * sTP_DEV.Xpoint +
                                      sTP_DEV.iXoff;
                    sTP_Draw.Ypoint = sTP_DEV.fYfac * sTP_DEV.Ypoint +
                                      sTP_DEV.iYoff;
                }
                else if (sTP_DEV.TP_Scan_Dir == L2R_U2D)
                {
                    sTP_Draw.Xpoint = sLCD_DIS.LCD_Dis_Column -
                                      sTP_DEV.fXfac * sTP_DEV.Xpoint -
                                      sTP_DEV.iXoff;
                    sTP_Draw.Ypoint = sLCD_DIS.LCD_Dis_Page -
                                      sTP_DEV.fYfac * sTP_DEV.Ypoint -
                                      sTP_DEV.iYoff;
                }
                else if (sTP_DEV.TP_Scan_Dir == U2D_R2L)
                {
                    sTP_Draw.Xpoint = sTP_DEV.fXfac * sTP_DEV.Ypoint +
                                      sTP_DEV.iXoff;
                    sTP_Draw.Ypoint = sTP_DEV.fYfac * sTP_DEV.Xpoint +
                                      sTP_DEV.iYoff;
                }
                else
                {
                    sTP_Draw.Xpoint = sLCD_DIS.LCD_Dis_Column -
                                      sTP_DEV.fXfac * sTP_DEV.Ypoint -
                                      sTP_DEV.iXoff;
                    sTP_Draw.Ypoint = sLCD_DIS.LCD_Dis_Page -
                                      sTP_DEV.fYfac * sTP_DEV.Xpoint -
                                      sTP_DEV.iYoff;
                }
  
            }
        }
        if (0 == (sTP_DEV.chStatus & TP_PRESS_DOWN))
        {
            sTP_DEV.chStatus = TP_PRESS_DOWN | TP_PRESSED;
            sTP_DEV.Xpoint0 = sTP_DEV.Xpoint;
            sTP_DEV.Ypoint0 = sTP_DEV.Ypoint;
        }
    }
    else
    {
        if (sTP_DEV.chStatus & TP_PRESS_DOWN)
        {                                  
            sTP_DEV.chStatus &= ~(1 << 7); //0x00
        }
        else
        {
            sTP_DEV.Xpoint0 = 0;
            sTP_DEV.Ypoint0 = 0;
            sTP_DEV.Xpoint = 0xffff;
            sTP_DEV.Ypoint = 0xffff;
        }
    }

    return (sTP_DEV.chStatus & TP_PRESS_DOWN);
}

void TP_GetAdFac(void)
{
    if (LCD_2_8 == id)
    {
        sTP_DEV.fXfac = 0.066626;
        sTP_DEV.fYfac = 0.089779;
        sTP_DEV.iXoff = -20;
        sTP_DEV.iYoff = -34;
    }
    else
    {
        if (sTP_DEV.TP_Scan_Dir == D2U_L2R)
        {
            sTP_DEV.fXfac = -0.132443;
            sTP_DEV.fYfac = 0.089997;
            sTP_DEV.iXoff = 516;
            sTP_DEV.iYoff = -22;
        }
        else if (sTP_DEV.TP_Scan_Dir == L2R_U2D)
        {
            sTP_DEV.fXfac = 0.089697;
            sTP_DEV.fYfac = 0.134792;
            sTP_DEV.iXoff = -21;
            sTP_DEV.iYoff = -39;
        }
        else if (sTP_DEV.TP_Scan_Dir == R2L_D2U)
        {
            sTP_DEV.fXfac = 0.089915;
            sTP_DEV.fYfac = 0.133178;
            sTP_DEV.iXoff = -22;
            sTP_DEV.iYoff = -38;
        }
        else if (sTP_DEV.TP_Scan_Dir == U2D_R2L)
        {
            sTP_DEV.fXfac = -0.132906;
            sTP_DEV.fYfac = 0.087964;
            sTP_DEV.iXoff = 517;
            sTP_DEV.iYoff = -20;
        }
        else
        {
            LCD_Clear(LCD_BACKGROUND);
            GUI_DisString_EN(0, 60, "Does not support touch-screen \
							calibration in this direction",
                             &Font16, FONT_BACKGROUND, RED);
        }
    }
}


void TP_Dialog(void)
{
    last_x = -1;
    last_y = -1;
    LCD_Clear(LCD_BACKGROUND);

    GUI_DisString_EN(340, 5,
                     "SAVE", &Font16, BLACK, WHITE);
    GUI_DisString_EN(415, 5,
                     "CLEAR", &Font16, BLACK, WHITE);


    GUI_DrawRectangle(BOX_X0 , BOX_Y0 ,
                      BOX_X1, BOX_Y1,
                      BLACK, DRAW_EMPTY, DOT_PIXEL_2X2);


    memset(sDrawShadow, 0, sizeof(sDrawShadow));
}

void TP_Save(void)
{
    printf("\n====================================\n");
    printf("          TP_SAVE CALLED\n");
    printf("====================================\n");

    GUI_DisString_EN(
        300,
        40,
        "CHECK",
        &Font16,
        BLACK,
        WHITE
    );

    printf("TP_Save: captured %ux%u pixels from box [%u,%u]-[%u,%u]\r\n",
           (unsigned)BOX_W, (unsigned)BOX_H,
           (unsigned)BOX_X0, (unsigned)BOX_Y0,
           (unsigned)BOX_X1, (unsigned)BOX_Y1);

    if (!tp_data) {
        printf("ERROR: tp_data == NULL\n");
        return;
    }

    tp_data->data_len = BOX_W * BOX_H;

    if (tp_data->data) {
        free(tp_data->data);
        tp_data->data = NULL;
    }

    tp_data->data = malloc(tp_data->data_len);
    if (!tp_data->data) {
        printf("ERROR: malloc(%u) failed\n",
               (unsigned)tp_data->data_len);
        tp_data->data_len = 0;
        return;
    }

    memcpy(tp_data->data, sDrawShadow, tp_data->data_len);
    printf("TP_Save: bitmap copied successfully\n");

    printf("TP_Save: calling recognizer...\n");

    float score = 0.0f;
    char result = recognize_character(
        tp_data->data,
        tp_data->data_len,
        BOX_W,
        BOX_H,
        &score
    );

    printf("TP_Save: recognizer returned '%c', score=%.3f\n",
           result, score);

    char result_text[32];
    snprintf(result_text, sizeof(result_text),
             "Detected: %c", result);

    GUI_DisString_EN(
        250,
        40,
        result_text,
        &Font16,
        BLACK,
        WHITE
    );

    printf("====================================\n");

}


void TP_DrawBoard(void)
{
    TP_Scan(0);
    if (sTP_DEV.chStatus & TP_PRESS_DOWN)
    { 
        spi_init(SPI_PORT, 10000000);

        printf("horizontal x:%d,y:%d\n", sTP_Draw.Xpoint, sTP_Draw.Ypoint);

        if (sTP_Draw.Xpoint > (sLCD_DIS.LCD_Dis_Column - 60) &&
            sTP_Draw.Ypoint < 16)
        { 
            TP_Dialog();
        }
        else if (sTP_Draw.Xpoint >= 340 &&
                 sTP_Draw.Xpoint <= 430 &&
                 sTP_Draw.Ypoint >= 0 &&
                 sTP_Draw.Ypoint <= 50)
        {
            printf("SAVE BUTTON DETECTED: x=%d y=%d\n",
                   sTP_Draw.Xpoint, sTP_Draw.Ypoint);
            TP_Save();
        }

        sTP_Draw.Color = BLACK;


        if (sTP_Draw.Xpoint > 100 && sTP_Draw.Xpoint < 380 &&
            sTP_Draw.Ypoint > 50  && sTP_Draw.Ypoint < 290)
        {
            if (last_x >= 0 && last_y >= 0) {
                Capture_Line(last_x, last_y,
                             sTP_Draw.Xpoint, sTP_Draw.Ypoint);
            } else {
                Capture_SetPixel(sTP_Draw.Xpoint, sTP_Draw.Ypoint);
            }

            GUI_DrawPoint(sTP_Draw.Xpoint, sTP_Draw.Ypoint,
                          sTP_Draw.Color, DOT_PIXEL_4X4, DOT_FILL_RIGHTUP);

            last_x = sTP_Draw.Xpoint;
            last_y = sTP_Draw.Ypoint;
        }

        
        spi_init(SPI_PORT, 5000000);
    }
    else {
        last_x = -1;
        last_y = -1;
    }
    SPI4W_Write_Byte(0xFF);
}

void TP_Init(LCD_SCAN_DIR Lcd_ScanDir, TP_DATA *tp_data_ptr, mutex_t *mutex)
{
    DEV_Digital_Write(TP_CS_PIN, 1);

    sTP_DEV.TP_Scan_Dir = Lcd_ScanDir;

    tp_data = tp_data_ptr;
    p_mutex = mutex;
    
    TP_Read_ADC_XY(&sTP_DEV.Xpoint, &sTP_DEV.Ypoint);
}
