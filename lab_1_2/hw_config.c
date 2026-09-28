#include <assert.h>
#include "hw_config.h"

// the lcd board connects its card slot through four-bit sdio
// слот карты на lcd использует четырёхбитный интерфейс sdio
static sd_sdio_if_t sdio_ifs[]={
    {
        .CMD_gpio=18,
        .D0_gpio=19,
        .CLK_gpio_drive_strength=GPIO_DRIVE_STRENGTH_12MA,
        .CMD_gpio_drive_strength=GPIO_DRIVE_STRENGTH_4MA,
        .D0_gpio_drive_strength=GPIO_DRIVE_STRENGTH_4MA,
        .D1_gpio_drive_strength=GPIO_DRIVE_STRENGTH_4MA,
        .D2_gpio_drive_strength=GPIO_DRIVE_STRENGTH_4MA,
        .D3_gpio_drive_strength=GPIO_DRIVE_STRENGTH_4MA,
        .SDIO_PIO=pio0,
        .DMA_IRQ_num=DMA_IRQ_1,
        .baud_rate=125*1000*1000/7
    }
};

static sd_card_t sd_cards[]={
    {
        .type=SD_IF_SDIO,
        .sdio_if_p=&sdio_ifs[0],
        .use_card_detect=false
    }
};

size_t sd_get_num(void) {
    // the current hardware has one sd card slot
    // на этой плате используется один слот sd-карты
    return count_of(sd_cards);
}

sd_card_t *sd_get_by_num(size_t num) {
    assert(num<sd_get_num());
    return num<sd_get_num()?&sd_cards[num]:NULL;
}
