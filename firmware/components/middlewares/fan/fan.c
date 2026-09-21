#include "fan.h"
#include "driver/dac_oneshot.h"
#include "esp_log.h"

static dac_oneshot_handle_t s_dac;

void fan_init(void)
{
    dac_oneshot_config_t cfg = {
        .chan_id = DAC_CHANNEL_1,    //GPIO25
    };
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&cfg, &s_dac));
    fan_set_raw(0);
    ESP_LOGI("fan", "DAC1(GPIO25) init ok");
}

void fan_set_raw(uint8_t dac_val)
{
    if (s_dac != NULL)
    {
        dac_oneshot_output_voltage(s_dac, dac_val);
    }
}

uint8_t fan_speed_to_dac(uint8_t pct)
{
    if (pct > 100) pct = 100;
    return (uint8_t)(FAN_DAC_MIN + (FAN_DAC_MAX - FAN_DAC_MIN) * pct / 100);
}
