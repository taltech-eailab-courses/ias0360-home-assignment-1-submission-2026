#include "LCD_Driver.h"
#include "LCD_Touch.h"
#include "LCD_GUI.h"
#include "DEV_Config.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "recognizer.h"

#ifndef BOX_X0
#define BOX_X0 100
#endif
#ifndef BOX_Y0
#define BOX_Y0 50
#endif
#ifndef BOX_X1
#define BOX_X1 380
#endif
#ifndef BOX_Y1
#define BOX_Y1 290
#endif

#define BOX_W (BOX_X1 - BOX_X0)
#define BOX_H (BOX_Y1 - BOX_Y0)


#define RECOGNIZE_BUTTON_PIN 2

static TP_DATA tp_data = {0};
static mutex_t tp_mutex;
static LCD_SCAN_DIR lcd_scan_dir = SCAN_DIR_DFT;

static void show_result(char c, float score)
{
    char line[40];
    LCD_SCAN_DIR dir = SCAN_DIR_DFT;
    (void)dir;

    GUI_DrawRectangle(8, 5, 470, 44, WHITE, DRAW_FULL, DOT_PIXEL_1X1);

    snprintf(line, sizeof(line), "Detected: %c   score=%.2f", c, score);
    GUI_DisString_EN(12, 10, line, &Font16, WHITE, BLACK);
}

int main(void)
{
    stdio_init_all();

    System_Init();

    LCD_Init(lcd_scan_dir, 1000);
    TP_Init(lcd_scan_dir, &tp_data, &tp_mutex);
    TP_GetAdFac();
    TP_Dialog();

    gpio_init(RECOGNIZE_BUTTON_PIN);
    gpio_set_dir(RECOGNIZE_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(RECOGNIZE_BUTTON_PIN);

    LCD_SetBackLight(1000);

    printf("Start recognised numbers 0-9\n");
    printf("Draw one character in the black box and save it by the button.\n");
    printf("Capture: %d x %d = %d bytes\n", BOX_W, BOX_H, BOX_W * BOX_H);

    bool last_button = true;

    while (1) {

        TP_DrawBoard();

        bool button = gpio_get(RECOGNIZE_BUTTON_PIN);

        if (last_button && !button) {
            sleep_ms(25);
            if (!gpio_get(RECOGNIZE_BUTTON_PIN)) {
                if (tp_data.data != NULL && tp_data.data_len >= (size_t)BOX_W * BOX_H) {
                    float score = 0.0f;
                    char result = recognize_character(tp_data.data,
                                                      tp_data.data_len,
                                                      BOX_W, BOX_H,
                                                      &score);

                    printf("Recognized: %c   distance=%.3f   data_len=%zu\n",
                           result, score, tp_data.data_len);
                    show_result(result, score);
                } else {
                    printf("No drawing data available: data=%p len=%zu\n",
                           (void *)tp_data.data, tp_data.data_len);
                    show_result('?', 999.0f);
                }
                while (!gpio_get(RECOGNIZE_BUTTON_PIN)) {
                    TP_DrawBoard();
                    sleep_ms(10);
                }
            }
        }

        last_button = button;
        tight_loop_contents();
    }

    return 0;
}
