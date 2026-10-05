/*****************************************************************************
* | File        : hw_config.c
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Hardware configuration for SDIO on Waveshare Pico-Eval-Board
* | Info        : Adapted from Carl John Kugler III for Home Assignment 1
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Waveshare Pico-Eval-Board with Pico W (SDIO via PIO0)
******************************************************************************/

#include <assert.h>
#include "hw_config.h"

#ifndef USE_SPI
#define USE_SPI 0
#endif

#if USE_SPI
static spi_t spis[] = {
    {
        .hw_inst = spi1,
        .sck_gpio = 10,
        .mosi_gpio = 11,
        .miso_gpio = 12,
        .set_drive_strength = true,
        .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .sck_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_12MA,
        .no_miso_gpio_pull_up = true,
        .baud_rate = 125 * 1000 * 1000 / 6
    }
};

static sd_spi_if_t spi_ifs[] = {
    {
        .spi = &spis[0],
        .ss_gpio = 22,
        .set_drive_strength = true,
        .ss_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA
    }
};
#else
/* SDIO Interfaces for Waveshare Pico-Eval-Board:
 * SD_CLK: GP5, SD_CMD: GP18, SD_D0: GP19, SD_D1: GP20, SD_D2: GP21, SD_D3: GP22
 */
static sd_sdio_if_t sdio_ifs[] = {
    {
        .CMD_gpio = 18,
        .D0_gpio = 19,
        .set_drive_strength = true,
        .CLK_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
        .CMD_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D0_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D1_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D2_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D3_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .SDIO_PIO = pio0,
        .DMA_IRQ_num = DMA_IRQ_1,
        .baud_rate = 125 * 1000 * 1000 / 7
    }
};
#endif

static sd_card_t sd_cards[] = {
    {
#if USE_SPI
        .type = SD_IF_SPI,
        .spi_if_p = &spi_ifs[0],
#else
        .type = SD_IF_SDIO,
        .sdio_if_p = &sdio_ifs[0],
#endif
        .use_card_detect = false,
        .card_detect_gpio = 0,
        .card_detected_true = 0,
        .card_detect_use_pull = false,
        .card_detect_pull_hi = false
    }
};

size_t sd_get_num() { return count_of(sd_cards); }

sd_card_t *sd_get_by_num(size_t num) {
    assert(num < sd_get_num());
    return (num < sd_get_num()) ? &sd_cards[num] : NULL;
}
