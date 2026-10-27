#include "led.h"
#include "pico/cyw43_arch.h"

#define LED_INTERVAL_MOVING_US   1000000
#define LED_INTERVAL_WALKING_US  400000
#define LED_INTERVAL_RUNNING_US  150000

static absolute_time_t next_toggle;
static bool led_on = false;
static motion_state_t last_state = MOTION_STILL;

void led_init(void) {
    // TODO: call cyw43_arch_init(), check its return value (0 == success,
    //       same pattern as sensor_init's sensorType check)
   bool LEDValue = cyw43_arch_init();
   if(LEDValue == 0){
    next_toggle = get_absolute_time();
   }
    // TODO: set next_toggle = get_absolute_time()
}

void led_update(motion_state_t state) {
    // TODO: if state != last_state, this is a transition — reset next_toggle
    //       to get_absolute_time() and update last_state, so a new stat
    //       starts its blink cadence fresh instead of mid-phase
    if(state != last_state){
    next_toggle = get_absolute_time();
    led_on = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    
    }
    // TODO: if state == MOTION_STILL: just hold the LED on (cyw43_arch_
    //       with true), don't toggle
    if(state == MOTION_STILL){  
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            last_state = state;
        
    } else if(state == MOTION_MOVING){
        if (time_reached(next_toggle)){
            led_on = !led_on;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
            
            last_state = state;
            next_toggle = delayed_by_us(next_toggle, LED_INTERVAL_MOVING_US);
        }
    } else if(state == MOTION_WALKING){
        if (time_reached(next_toggle)){
            led_on = !led_on;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
            
            last_state = state;
            next_toggle = delayed_by_us(next_toggle, LED_INTERVAL_WALKING_US);
        }
    } else if(state == MOTION_RUNNING){
        if (time_reached(next_toggle)){
            led_on = !led_on;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
            last_state = state;
            next_toggle = delayed_by_us(next_toggle, LED_INTERVAL_RUNNING_US);
        }
    }
    // TODO: otherwise, pick an interval for the current state (if/else
    //       chain on the three non-STILL states), and: if time_reached(next_toggle),
    //       flip led_on, call cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN
    //       and advance next_toggle by that interval (delayed_by_us)
}