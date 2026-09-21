#ifndef LED_H
#define LED_H

#include "driver/gpio.h"

#define led_gpio_pin GPIO_NUM_2

enum gpio_output
{
    pin_reset = 0,
    pin_set = 1,
};

#define led(x) do{ x ? gpio_set_level(led_gpio_pin, pin_set):\
                       gpio_set_level(led_gpio_pin, pin_reset);\
                 } while(0)

#define led_toggle() do{gpio_set_level(led_gpio_pin, !gpio_get_level(led_gpio_pin));\
                        } while(0)



void led_init(void);

#endif