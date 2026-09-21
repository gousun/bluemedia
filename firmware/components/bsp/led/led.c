#include "led.h"

void led_init(void)
{
    gpio_config_t gpio_init_structure;
    gpio_init_structure.pin_bit_mask = (1ULL << led_gpio_pin);
    gpio_init_structure.mode = GPIO_MODE_INPUT_OUTPUT;
    gpio_init_structure.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_init_structure.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_structure.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&gpio_init_structure);
}