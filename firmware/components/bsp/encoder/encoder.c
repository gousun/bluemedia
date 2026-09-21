#include "encoder.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ENC_A_GPIO     GPIO_NUM_32
#define ENC_B_GPIO     GPIO_NUM_33
#define ENC_SW_GPIO    GPIO_NUM_13

#define ENC_ROT_DEBOUNCE_MS     10   //AB线软件消抖
#define ENC_SW_DEBOUNCE_MS      40   //按键软件消抖（过短会把按住中的接触噪声误判为释放沿）

/*正交解码表，index = (prev_ab << 2) | curr_ab，ab = (A << 1) | B*/
static const int8_t enc_table[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

static uint8_t enc_stable;          //消抖后的稳定状态
static uint8_t enc_cand;            //候选状态
static TickType_t enc_cand_since;   //候选状态首次出现时刻
static int8_t enc_step_acc;         //跳变累计，满2个=一格

static bool sw_candidate;
static bool sw_stable;              //true=未按下(高), false=按下(低)
static TickType_t sw_last_change;

static inline uint8_t enc_read_ab(void)
{
    return (uint8_t)((gpio_get_level(ENC_A_GPIO) << 1) | gpio_get_level(ENC_B_GPIO));
}

void encoder_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ENC_A_GPIO) | (1ULL << ENC_B_GPIO) | (1ULL << ENC_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    enc_stable = enc_read_ab();
    enc_cand = enc_stable;
    enc_cand_since = xTaskGetTickCount();
    enc_step_acc = 0;

    sw_stable = (bool)gpio_get_level(ENC_SW_GPIO);
    sw_candidate = sw_stable;
    sw_last_change = xTaskGetTickCount();
}

int16_t encoder_read(void)
{
    uint8_t raw = enc_read_ab();
    TickType_t now = xTaskGetTickCount();

    if (raw != enc_cand)
    {
        enc_cand = raw;
        enc_cand_since = now;
    }
    else if (raw != enc_stable &&
             (now - enc_cand_since) >= pdMS_TO_TICKS(ENC_ROT_DEBOUNCE_MS))
    {
        int8_t step = enc_table[(uint8_t)((enc_stable << 2) | enc_cand)];
        enc_stable = enc_cand;

        if (step != 0)
        {
            enc_step_acc += step;
            if (enc_step_acc >= 2 || enc_step_acc <= -2)   //满2个跳变=一格
            {
                enc_step_acc = 0;
                return (step > 0) ? 1 : -1;
            }
        }
    }
    return 0;
}

/*按键按下沿（低电平有效）。只处理"按下"方向的翻转；
  松开方向的消抖与翻转由 encoder_released() 负责，避免共享stable时互相偷沿*/
bool encoder_pressed(void)
{
    if (gpio_get_level(ENC_SW_GPIO)) return false;    //高=未按下，本函数不参与
    bool raw = false;
    TickType_t now = xTaskGetTickCount();

    if (raw != sw_candidate)
    {
        sw_candidate = raw;
        sw_last_change = now;
        return false;
    }
    if (sw_stable && (now - sw_last_change) >= pdMS_TO_TICKS(ENC_SW_DEBOUNCE_MS))
    {
        sw_stable = false;
        return true;    //按下沿
    }
    return false;
}

/*按键释放沿（高电平）。只处理"释放"方向的翻转*/
bool encoder_released(void)
{
    if (!gpio_get_level(ENC_SW_GPIO)) return false;   //低=按住中，本函数不参与
    bool raw = true;
    TickType_t now = xTaskGetTickCount();

    if (raw != sw_candidate)
    {
        sw_candidate = raw;
        sw_last_change = now;
        return false;
    }
    if (!sw_stable && (now - sw_last_change) >= pdMS_TO_TICKS(ENC_SW_DEBOUNCE_MS))
    {
        sw_stable = true;
        return true;    //释放沿
    }
    return false;
}
