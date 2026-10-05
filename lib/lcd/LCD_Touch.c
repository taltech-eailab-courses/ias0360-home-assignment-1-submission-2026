/*****************************************************************************
* | File        : LCD_Touch.c
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : LCD Touch Pad Driver and Drawing Canvas
* | Info        : 280x240 drawing canvas with continuous Bresenham strokes
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Full double-buffering canvas with adaptive 28x28 downsampling
******************************************************************************/

#include "LCD_Touch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern LCD_DIS sLCD_DIS;
extern uint8_t id;
static TP_DEV sTP_DEV;
static TP_DRAW sTP_Draw;

static TP_DATA *tp_data = NULL;
static mutex_t *p_mutex = NULL;

// Live shadow buffer of what has been drawn (0 = empty, 1 = drawn)
// Statically allocated: 240 * 280 = 67,200 bytes
static uint8_t sDrawShadow[BOX_H][BOX_W];

// Helper to record a pixel into the shadow buffer
static inline void Capture_SetPixel(uint16_t x, uint16_t y)
{
    if (x >= BOX_X0 && x < BOX_X1 && y >= BOX_Y0 && y < BOX_Y1) {
        sDrawShadow[y - BOX_Y0][x - BOX_X0] = 1;
    }
}

#define TP_CMD_X              0xD0  // differential X position
#define TP_CMD_Y              0x90  // differential Y position
#define TP_CMD_Z1             0xB0  // differential Z1 touch pressure
#define TP_CMD_Z2             0xC0  // differential Z2 touch pressure

#define TP_TOUCH_SPI_BAUDRATE 1000000                  // SAR ADC SPI baud rate
#define TP_LCD_SPI_BAUDRATE   (125 * 1000 * 1000 / 6)  // LCD SPI baud rate

#define TP_ADC_VALID_MIN      80
#define TP_ADC_VALID_MAX      4015
#define TP_SAMPLES_COUNT      5
#define TP_ERR_RANGE          30    // cluster tolerance in ADC counts
#define TP_Z1_MIN             90    // minimum Z1 ADC count for contact
#define TP_X_PLATE_OHMS       400   // Waveshare panel sheet resistance (Ohms)
#define TP_R_TOUCH_MAX        1500  // maximum contact resistance (Ohms)
#define TP_SCREEN_MARGIN      20
#define TP_POINT_JITTER       2
#define TP_POINT_SMOOTH_RANGE 30
#define TP_MAX_STROKE_STEP    35    // maximum plausible pixel movement
#define TP_MAX_STROKE_STEP_SQ (TP_MAX_STROKE_STEP * TP_MAX_STROKE_STEP)

static bool sTP_DrawValid = false;
static POINT sTP_LastXpoint = 0;
static POINT sTP_LastYpoint = 0;
static bool sTP_StrokeValid = false;
static POINT sTP_StrokeXpoint = 0;
static POINT sTP_StrokeYpoint = 0;
static uint8_t sTP_PenDownCount = 0;
static uint8_t sTP_OutlierCount = 0;
static POINT sTP_CandidateX = 0;
static POINT sTP_CandidateY = 0;

static uint16_t TP_AbsDiff(uint16_t Value1, uint16_t Value2)
{
    return (Value1 > Value2) ? (Value1 - Value2) : (Value2 - Value1);
}

static void TP_Sort_ADC(uint16_t *pData, uint8_t Count)
{
    uint8_t i, j;
    uint16_t Temp;

    if (Count < 2)
        return;

    for (i = 0; i < Count - 1; i++) {
        for (j = i + 1; j < Count; j++) {
            if (pData[i] > pData[j]) {
                Temp = pData[i];
                pData[i] = pData[j];
                pData[j] = Temp;
            }
        }
    }
}

static bool TP_Is_ADC_Valid(uint16_t Xpoint, uint16_t Ypoint)
{
    return Xpoint > TP_ADC_VALID_MIN && Xpoint < TP_ADC_VALID_MAX &&
           Ypoint > TP_ADC_VALID_MIN && Ypoint < TP_ADC_VALID_MAX;
}

static void TP_Reset_Stroke(void)
{
    sTP_StrokeValid = false;
    sTP_OutlierCount = 0;
}

static void TP_Reset_Draw_Filter(void)
{
    sTP_DrawValid = false;
    sTP_PenDownCount = 0;
    TP_Reset_Stroke();
}

static POINT TP_Clamp_To_Screen(int32_t Value, POINT Max)
{
    if (Value <= 0)
        return 0;
    if (Max == 0)
        return 0;
    if (Value >= Max)
        return Max - 1;
    return (POINT)Value;
}

static int32_t TP_Round_Float(float Value)
{
    if (Value >= 0)
        return (int32_t)(Value + 0.5f);
    return (int32_t)(Value - 0.5f);
}

static void TP_Filter_Draw_Point(void)
{
    uint16_t Dx, Dy;

    if (!sTP_DrawValid || 0 == (sTP_DEV.chStatus & TP_PRESS_DOWN)) {
        sTP_DrawValid = true;
        sTP_LastXpoint = sTP_Draw.Xpoint;
        sTP_LastYpoint = sTP_Draw.Ypoint;
        return;
    }

    Dx = TP_AbsDiff(sTP_Draw.Xpoint, sTP_LastXpoint);
    Dy = TP_AbsDiff(sTP_Draw.Ypoint, sTP_LastYpoint);

    if (Dx <= TP_POINT_JITTER && Dy <= TP_POINT_JITTER) {
        sTP_Draw.Xpoint = sTP_LastXpoint;
        sTP_Draw.Ypoint = sTP_LastYpoint;
    } else if (Dx <= TP_POINT_SMOOTH_RANGE && Dy <= TP_POINT_SMOOTH_RANGE) {
        uint32_t nx = (uint32_t)sTP_LastXpoint + (uint32_t)sTP_Draw.Xpoint * 2;
        uint32_t ny = (uint32_t)sTP_LastYpoint + (uint32_t)sTP_Draw.Ypoint * 2;
        sTP_Draw.Xpoint = (POINT)(nx / 3);
        sTP_Draw.Ypoint = (POINT)(ny / 3);
    }

    sTP_LastXpoint = sTP_Draw.Xpoint;
    sTP_LastYpoint = sTP_Draw.Ypoint;
}

static bool TP_Update_Draw_Point(int32_t Xpoint, int32_t Ypoint)
{
    if (Xpoint < -TP_SCREEN_MARGIN || Ypoint < -TP_SCREEN_MARGIN ||
        Xpoint > sLCD_DIS.LCD_Dis_Column + TP_SCREEN_MARGIN ||
        Ypoint > sLCD_DIS.LCD_Dis_Page + TP_SCREEN_MARGIN) {
        return false;
    }

    POINT clamped_x = TP_Clamp_To_Screen(Xpoint, sLCD_DIS.LCD_Dis_Column);
    POINT clamped_y = TP_Clamp_To_Screen(Ypoint, sLCD_DIS.LCD_Dis_Page);

    sTP_Draw.Xpoint = clamped_x;
    sTP_Draw.Ypoint = clamped_y;
    TP_Filter_Draw_Point();

    return true;
}

static void Capture_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t dx = abs(x1 - x0);
    int16_t dy = -abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    while (1) {
        Capture_SetPixel(x0, y0);
        Capture_SetPixel(x0 + 1, y0);
        Capture_SetPixel(x0, y0 + 1);
        Capture_SetPixel(x0 + 1, y0 + 1);

        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void TP_DrawStroke(POINT Xpoint, POINT Ypoint)
{
    if (sTP_StrokeValid) {
        int32_t dx = (int32_t)Xpoint - (int32_t)sTP_StrokeXpoint;
        int32_t dy = (int32_t)Ypoint - (int32_t)sTP_StrokeYpoint;
        uint32_t dist_sq = (uint32_t)(dx * dx + dy * dy);

        if (dist_sq > TP_MAX_STROKE_STEP_SQ) {
            if (sTP_OutlierCount == 0) {
                sTP_CandidateX = Xpoint;
                sTP_CandidateY = Ypoint;
                sTP_OutlierCount = 1;
                return;
            } else {
                int32_t cdx = (int32_t)Xpoint - (int32_t)sTP_CandidateX;
                int32_t cdy = (int32_t)Ypoint - (int32_t)sTP_CandidateY;
                uint32_t cdist_sq = (uint32_t)(cdx * cdx + cdy * cdy);

                if (cdist_sq <= TP_MAX_STROKE_STEP_SQ) {
                    sTP_StrokeValid = false;
                    sTP_OutlierCount = 0;
                } else {
                    sTP_CandidateX = Xpoint;
                    sTP_CandidateY = Ypoint;
                    return;
                }
            }
        } else {
            sTP_OutlierCount = 0;
        }
    }

    if (sTP_StrokeValid) {
        GUI_DrawLine(sTP_StrokeXpoint, sTP_StrokeYpoint,
                     Xpoint, Ypoint,
                     sTP_Draw.Color, LINE_SOLID, DOT_PIXEL_2X2);
        Capture_DrawLine(sTP_StrokeXpoint, sTP_StrokeYpoint, Xpoint, Ypoint);
    } else {
        GUI_DrawPoint(Xpoint, Ypoint,
                      sTP_Draw.Color, DOT_PIXEL_2X2, DOT_FILL_RIGHTUP);
        Capture_SetPixel(Xpoint, Ypoint);
        Capture_SetPixel(Xpoint + 1, Ypoint);
        Capture_SetPixel(Xpoint, Ypoint + 1);
        Capture_SetPixel(Xpoint + 1, Ypoint + 1);
    }

    sTP_StrokeXpoint = Xpoint;
    sTP_StrokeYpoint = Ypoint;
    sTP_StrokeValid = true;
}

static uint16_t TP_Read_ADC(uint8_t CMD)
{
    uint8_t tx[3] = {CMD, 0x00, 0x00};
    uint8_t rx[3] = {0, 0, 0};

    DEV_Digital_Write(TP_CS_PIN, 0);
    spi_write_read_blocking(SPI_PORT, tx, rx, 3);
    DEV_Digital_Write(TP_CS_PIN, 1);

    uint16_t Data = ((uint16_t)rx[1] << 8) | rx[2];
    Data >>= 3;
    return Data;
}

static bool TP_Read_TwiceADC(uint16_t *pXCh_Adc, uint16_t *pYCh_Adc)
{
    uint16_t x_buf[TP_SAMPLES_COUNT];
    uint16_t y_buf[TP_SAMPLES_COUNT];

    if (DEV_Digital_Read(TP_IRQ_PIN))
        return false;

    spi_set_baudrate(SPI_PORT, TP_TOUCH_SPI_BAUDRATE);

    (void)TP_Read_ADC(TP_CMD_Z1);
    Driver_Delay_us(30);
    uint16_t z1 = TP_Read_ADC(TP_CMD_Z1);

    (void)TP_Read_ADC(TP_CMD_Z2);
    Driver_Delay_us(30);
    uint16_t z2 = TP_Read_ADC(TP_CMD_Z2);

    if (z1 < TP_Z1_MIN || z2 <= z1) {
        (void)TP_Read_ADC(TP_CMD_X);
        spi_set_baudrate(SPI_PORT, TP_LCD_SPI_BAUDRATE);
        return false;
    }

    (void)TP_Read_ADC(TP_CMD_X);
    Driver_Delay_us(40);
    for (uint8_t i = 0; i < TP_SAMPLES_COUNT; i++) {
        x_buf[i] = TP_Read_ADC(TP_CMD_X);
        Driver_Delay_us(20);
    }

    (void)TP_Read_ADC(TP_CMD_Y);
    Driver_Delay_us(40);
    for (uint8_t i = 0; i < TP_SAMPLES_COUNT; i++) {
        y_buf[i] = TP_Read_ADC(TP_CMD_Y);
        Driver_Delay_us(20);
    }

    (void)TP_Read_ADC(TP_CMD_Z1);
    Driver_Delay_us(20);
    uint16_t z1_post = TP_Read_ADC(TP_CMD_Z1);

    (void)TP_Read_ADC(TP_CMD_X);
    spi_set_baudrate(SPI_PORT, TP_LCD_SPI_BAUDRATE);

    if (DEV_Digital_Read(TP_IRQ_PIN) || z1_post < TP_Z1_MIN)
        return false;

    TP_Sort_ADC(x_buf, TP_SAMPLES_COUNT);
    if ((x_buf[3] - x_buf[1]) > TP_ERR_RANGE)
        return false;
    uint16_t x_avg = (uint16_t)(((uint32_t)x_buf[1] + x_buf[2] + x_buf[3]) / 3);

    TP_Sort_ADC(y_buf, TP_SAMPLES_COUNT);
    if ((y_buf[3] - y_buf[1]) > TP_ERR_RANGE)
        return false;
    uint16_t y_avg = (uint16_t)(((uint32_t)y_buf[1] + y_buf[2] + y_buf[3]) / 3);

    if (!TP_Is_ADC_Valid(x_avg, y_avg))
        return false;

    uint32_t r_touch = (uint32_t)(((uint64_t)(z2 - z1) *
                                   (uint64_t)x_avg * TP_X_PLATE_OHMS) /
                                  ((uint64_t)z1 * 4096));
    sTP_DEV.Z1 = (r_touch > 65535) ? 65535 : (uint16_t)r_touch;

    if (r_touch > TP_R_TOUCH_MAX)
        return false;

    *pXCh_Adc = x_avg;
    *pYCh_Adc = y_avg;
    return true;
}

static uint8_t TP_Scan(uint8_t chCoordType)
{
    bool CoordOk = false;

    if (!DEV_Digital_Read(TP_IRQ_PIN)) {
        if (TP_Read_TwiceADC(&sTP_DEV.Xpoint, &sTP_DEV.Ypoint)) {
            if (chCoordType) {
                CoordOk = true;
            } else {
                int32_t Draw_Xpoint;
                int32_t Draw_Ypoint;

                if (LCD_2_8 == id) {
                    float fx = sTP_DEV.fXfac * sTP_DEV.Xpoint;
                    float fy = sTP_DEV.fYfac * sTP_DEV.Ypoint;
                    Draw_Xpoint = TP_Round_Float(sLCD_DIS.LCD_Dis_Column -
                                                 fx - sTP_DEV.iXoff);
                    Draw_Ypoint = TP_Round_Float(sLCD_DIS.LCD_Dis_Page -
                                                 fy - sTP_DEV.iYoff);
                } else {
                    if (sTP_DEV.TP_Scan_Dir == R2L_D2U) {
                        float fx = sTP_DEV.fXfac * sTP_DEV.Xpoint;
                        float fy = sTP_DEV.fYfac * sTP_DEV.Ypoint;
                        Draw_Xpoint = TP_Round_Float(fx + sTP_DEV.iXoff);
                        Draw_Ypoint = TP_Round_Float(fy + sTP_DEV.iYoff);
                    } else if (sTP_DEV.TP_Scan_Dir == L2R_U2D) {
                        float fx = sTP_DEV.fXfac * sTP_DEV.Xpoint;
                        float fy = sTP_DEV.fYfac * sTP_DEV.Ypoint;
                        Draw_Xpoint = TP_Round_Float(sLCD_DIS.LCD_Dis_Column -
                                                     fx - sTP_DEV.iXoff);
                        Draw_Ypoint = TP_Round_Float(sLCD_DIS.LCD_Dis_Page -
                                                     fy - sTP_DEV.iYoff);
                    } else if (sTP_DEV.TP_Scan_Dir == U2D_R2L) {
                        float fx = sTP_DEV.fXfac * sTP_DEV.Ypoint;
                        float fy = sTP_DEV.fYfac * sTP_DEV.Xpoint;
                        Draw_Xpoint = TP_Round_Float(fx + sTP_DEV.iXoff);
                        Draw_Ypoint = TP_Round_Float(fy + sTP_DEV.iYoff);
                    } else {
                        float fx = sTP_DEV.fXfac * sTP_DEV.Ypoint;
                        float fy = sTP_DEV.fYfac * sTP_DEV.Xpoint;
                        Draw_Xpoint = TP_Round_Float(sLCD_DIS.LCD_Dis_Column -
                                                     fx - sTP_DEV.iXoff);
                        Draw_Ypoint = TP_Round_Float(sLCD_DIS.LCD_Dis_Page -
                                                     fy - sTP_DEV.iYoff);
                    }
                }

                CoordOk = TP_Update_Draw_Point(Draw_Xpoint, Draw_Ypoint);
            }
        }
        if (CoordOk) {
            if (0 == (sTP_DEV.chStatus & TP_PRESS_DOWN)) {
                sTP_DEV.chStatus = TP_PRESS_DOWN | TP_PRESSED;
                sTP_DEV.Xpoint0 = sTP_Draw.Xpoint;
                sTP_DEV.Ypoint0 = sTP_Draw.Ypoint;
            }
        } else {
            TP_Reset_Draw_Filter();
            sTP_DEV.chStatus = 0;
        }
    } else {
        if (sTP_DEV.chStatus & TP_PRESS_DOWN) {
            sTP_DEV.chStatus &= ~(1 << 7);
        } else {
            sTP_DEV.Xpoint0 = 0;
            sTP_DEV.Ypoint0 = 0;
            sTP_DEV.Xpoint = 0xffff;
            sTP_DEV.Ypoint = 0xffff;
        }
        TP_Reset_Draw_Filter();
    }
    return (sTP_DEV.chStatus & TP_PRESS_DOWN);
}

void TP_GetAdFac(void)
{
    if (LCD_3_5 == id) {
        if (sTP_DEV.TP_Scan_Dir == D2U_L2R) {
            sTP_DEV.fXfac = -0.132443f;
            sTP_DEV.fYfac = 0.089997f;
            sTP_DEV.iXoff = 516;
            sTP_DEV.iYoff = -22;
        } else if (sTP_DEV.TP_Scan_Dir == L2R_U2D) {
            sTP_DEV.fXfac = 0.089697f;
            sTP_DEV.fYfac = 0.134792f;
            sTP_DEV.iXoff = -21;
            sTP_DEV.iYoff = -39;
        } else if (sTP_DEV.TP_Scan_Dir == R2L_D2U) {
            sTP_DEV.fXfac = 0.089915f;
            sTP_DEV.fYfac = 0.133178f;
            sTP_DEV.iXoff = -22;
            sTP_DEV.iYoff = -38;
        } else if (sTP_DEV.TP_Scan_Dir == U2D_R2L) {
            sTP_DEV.fXfac = -0.132906f;
            sTP_DEV.fYfac = 0.087964f;
            sTP_DEV.iXoff = 517;
            sTP_DEV.iYoff = -20;
        } else {
            LCD_Clear(LCD_BACKGROUND);
            GUI_DisString_EN(0, 60, "Unsupported touch orientation",
                             &Font16, BLACK, RED);
        }
    }
}

static bool s_status_needs_reset = false;

void TP_ShowStatus(const char *text, COLOR color)
{
    LCD_SetArealColor(0, 0, 300, 32, LCD_BACKGROUND);
    GUI_DisString_EN(12, 8, text, &Font16, LCD_BACKGROUND, color);
}

void TP_ShowTelemetry(const char *text)
{
    LCD_SetArealColor(0, 292, 479, 318, LCD_BACKGROUND);
    GUI_DisString_EN(12, 298, text, &Font12, LCD_BACKGROUND, BLACK);
}

void TP_Dialog(void)
{
    LCD_Clear(LCD_BACKGROUND);

    // Top Row: Clean text status on left, solid black buttons on right
    TP_ShowStatus("Status: ready", BLACK);
    GUI_DisString_EN(320, 8, "PROCESS", &Font16, BLACK, WHITE);
    GUI_DisString_EN(413, 8, "CLEAR", &Font16, BLACK, WHITE);

    // Canvas border (280 x 240)
    GUI_DrawRectangle(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1,
                      BLACK, DRAW_EMPTY, DOT_PIXEL_2X2);

    // Bottom Telemetry Bar: Clean text metrics & DSP benchmark (65 chars)
    TP_ShowTelemetry("Mean: -.-   | Edge: -.--- | Asp: -.-- | "
                     "L1: ---us   | L2: ---us  ");

    // Clear live shadow buffer
    memset(sDrawShadow, 0, sizeof(sDrawShadow));

    // Reset touch filters
    TP_Reset_Draw_Filter();
    sTP_Draw.Xpoint = 0;
    sTP_Draw.Ypoint = 0;
    sTP_DEV.chStatus = 0;
    s_status_needs_reset = false;
}

static volatile bool s_tp_save_busy = false;
static uint32_t s_tp_save_start_ms = 0;

void TP_SetSaveBusy(bool busy)
{
    s_tp_save_busy = busy;
    if (busy) {
        s_tp_save_start_ms = to_ms_since_boot(get_absolute_time());
    } else {
        s_status_needs_reset = true;
    }
}

bool TP_GetSaveBusy(void)
{
    if (s_tp_save_busy &&
        (to_ms_since_boot(get_absolute_time()) - s_tp_save_start_ms > 5000)) {
        printf("TP: save operation timed out after 5000 ms\r\n");
        TP_ShowStatus("Status: timeout", RED);
        s_tp_save_busy = false;
    }
    return s_tp_save_busy;
}

void TP_Save(void)
{
    TP_ShowStatus("Status: processing", BLACK);

    if (!tp_data || !p_mutex) {
        printf("TP: tp_data or mutex not initialized\r\n");
        s_tp_save_busy = false;
        return;
    }

    mutex_enter_blocking(p_mutex);
    tp_data->data_len = BOX_W * BOX_H;
    if (tp_data->data != NULL) {
        memcpy(tp_data->data, sDrawShadow, tp_data->data_len);
    }
    mutex_exit(p_mutex);

    if (!multicore_fifo_push_timeout_us(DATA_READY_FLAG, 500 * 1000)) {
        printf("TP: Core 1 FIFO push timed out\r\n");
        TP_ShowStatus("Status: fifo error", RED);
        s_tp_save_busy = false;
    }
}

void TP_DrawBoard(void)
{
    TP_Scan(0);
    if (sTP_DEV.chStatus & TP_PRESS_DOWN) {
        // [CLEAR] button clicked: X >= 405, Y <= 32
        if (sTP_Draw.Xpoint >= 405 && sTP_Draw.Ypoint <= 32) {
            uint16_t release_timeout = 0;
            while (!DEV_Digital_Read(TP_IRQ_PIN) && release_timeout++ < 15) {
                Driver_Delay_ms(5);
            }
            TP_Reset_Draw_Filter();
            TP_Dialog();
            sTP_Draw.Xpoint = 0;
            sTP_Draw.Ypoint = 0;
            sTP_DEV.chStatus = 0;
        }
        // [PROCESS] button clicked: X in [310, 404], Y <= 32
        else if (sTP_Draw.Xpoint >= 310 && sTP_Draw.Xpoint < 405 &&
                 sTP_Draw.Ypoint <= 32) {
            if (!TP_GetSaveBusy()) {
                TP_SetSaveBusy(true);
                TP_Reset_Draw_Filter();
                TP_Save();
            }
            uint16_t release_timeout = 0;
            while (!DEV_Digital_Read(TP_IRQ_PIN) && release_timeout++ < 15) {
                Driver_Delay_ms(5);
            }
            sTP_Draw.Xpoint = 0;
            sTP_Draw.Ypoint = 0;
            sTP_DEV.chStatus = 0;
        }

        sTP_Draw.Color = BLACK;

        // Drawing inside 280x240 canvas [100, 40] to [380, 280]
        if (sTP_Draw.Xpoint > BOX_X0 && sTP_Draw.Xpoint < (BOX_X1 - 1) &&
            sTP_Draw.Ypoint > BOX_Y0 && sTP_Draw.Ypoint < (BOX_Y1 - 1)) {
            if (s_status_needs_reset) {
                TP_ShowStatus("Status: ready", BLACK);
                s_status_needs_reset = false;
            }
            TP_DrawStroke(sTP_Draw.Xpoint, sTP_Draw.Ypoint);
        } else {
            TP_Reset_Stroke();
        }
    } else {
        TP_Reset_Stroke();
    }
}

void TP_Init(LCD_SCAN_DIR Lcd_ScanDir, TP_DATA *tp_data_ptr, mutex_t *mutex)
{
    DEV_Digital_Write(TP_CS_PIN, 1);

    sTP_DEV.TP_Scan_Dir = Lcd_ScanDir;
    tp_data = tp_data_ptr;
    p_mutex = mutex;

    sTP_DEV.Xpoint = 0xffff;
    sTP_DEV.Ypoint = 0xffff;
    sTP_DEV.Z1 = 0;
    sTP_DEV.chStatus = 0;
    sTP_Draw.Xpoint = 0;
    sTP_Draw.Ypoint = 0;
    sTP_PenDownCount = 0;
    TP_Reset_Draw_Filter();
}

