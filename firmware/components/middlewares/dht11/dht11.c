#include "dht11.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

/*DHT11 单总线时序（1MHz RMT 分辨率下，时长单位=us）：
  主机拉低>=18ms → 释放 → 传感器响应 80us低+80us高 → 40bit（每bit 低~50us + 高26/70us）
  起始信号：DATA 引脚配为开漏输出（io_loop_back 让 GPIO 与 RMT 共用引脚），
  强拉低对任何上拉（内部45k/外部10k）都有效；释放后由上拉拉回高电平。*/

static const char *TAG = "dht11";

#define DHT_FRAME_TIMEOUT_MS 100

static rmt_channel_handle_t s_rx_chan = NULL;
static QueueHandle_t s_rx_queue = NULL;
static rmt_symbol_word_t s_symbols[64];
static uint8_t s_rmt_buf[sizeof(s_symbols)];
static bool s_last_fail = true;    /*成败状态翻转时才打印日志，避免刷屏*/

/*RMT 接收完成回调（ISR上下文）：拷贝符号并通知任务*/
static bool rx_done_cb(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    int n = (int)edata->num_symbols;
    if (n > (int)(sizeof(s_symbols) / sizeof(s_symbols[0]))) n = sizeof(s_symbols) / sizeof(s_symbols[0]);
    memcpy(s_symbols, edata->received_symbols, (size_t)n * sizeof(rmt_symbol_word_t));
    xQueueSendFromISR(s_rx_queue, &n, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

esp_err_t dht11_init(void)
{
    /*RX通道 + io_loop_back：引脚同时可被GPIO驱动（用于产生起始信号）*/
    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = DHT11_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000 * 1000,        //1MHz，1 tick = 1us
        .mem_block_symbols = 64,
        .flags.io_loop_back = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx_cfg, &s_rx_chan), TAG, "new rx chan");

    /*开漏双向+上拉：拉低=强驱动，高=释放由上拉拉高（传感器随后接管总线）*/
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << DHT11_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio config");
    gpio_set_level(DHT11_GPIO, 1);           //空闲：释放总线

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rx_done_cb,
    };
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, NULL), TAG, "reg cb");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rx_chan), TAG, "rmt enable");

    s_rx_queue = xQueueCreate(1, sizeof(int));
    if (s_rx_queue == NULL) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "DHT11 on GPIO%d (RMT RX, OD drive) init ok", (int)DHT11_GPIO);
    return ESP_OK;
}

/*脉冲流状态机解码40bit：跳过响应帧(80us)，按高位宽度判0/1*/
static int decode_frame(int n, uint8_t out[5])
{
    memset(out, 0, 5);
    int bit = 0;
    bool expect_low = true;    //true=等待位起始低电平 false=等待位数据高电平
    for (int i = 0; i < n && bit < 40; i++)
    {
        uint32_t dur[2] = { s_symbols[i].duration0, s_symbols[i].duration1 };
        int lev[2] = { s_symbols[i].level0, s_symbols[i].level1 };
        for (int k = 0; k < 2; k++)
        {
            if (dur[k] == 0) continue;    //符号空半段
            uint32_t us = dur[k] + 2;     //粗略边沿补偿
            if (expect_low)
            {
                if (lev[k] != 0) continue;              //高电平脉冲（响应高/释放间隙）
                if (us >= 65 && us <= 110) continue;    //响应帧低电平80us，跳过
                if (us >= 25 && us <= 60) expect_low = false;    //位起始低~50us
                else return -1;                         //异常时序
            }
            else
            {
                if (lev[k] != 1) return -1;
                out[bit / 8] = (uint8_t)(out[bit / 8] << 1);
                if (us > 45) out[bit / 8] |= 1;         //高~70us=1，~26us=0
                bit++;
                expect_low = true;
            }
        }
    }
    return (bit == 40) ? 0 : -1;
}

esp_err_t dht11_read(float *temp_c, float *hum_pct)
{
    uint8_t b[5];
    esp_err_t err = ESP_OK;

    /*1. 起始信号：开漏强拉低约20ms（兼容外部上拉模块）*/
    gpio_set_level(DHT11_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /*2. 先武装RMT接收，再释放总线，避免错过传感器应答*/
    int n = -1;
    xQueueReset(s_rx_queue);
    rmt_receive_config_t rcfg = {
        .signal_range_min_ns = 1000,         //滤除<1us毛刺
        .signal_range_max_ns = 300 * 1000,   //电平保持>300us视为帧结束
    };
    err = rmt_receive(s_rx_chan, s_rmt_buf, sizeof(s_rmt_buf), &rcfg);

    /*3. 释放总线（开漏输出1=悬空，由上拉拉高），传感器20~40us后开始应答*/
    gpio_set_level(DHT11_GPIO, 1);

    if (err == ESP_OK)
    {
        if (xQueueReceive(s_rx_queue, &n, pdMS_TO_TICKS(DHT_FRAME_TIMEOUT_MS)) == pdTRUE && n > 0)
        {
            if (decode_frame(n, b) == 0)
            {
                uint8_t sum = (uint8_t)(b[0] + b[1] + b[2] + b[3]);
                if (sum == b[4])
                {
                    uint8_t hum_d  = (b[1] <= 9) ? b[1] : 0;    //兼容新旧格式
                    uint8_t temp_d = (b[3] <= 9) ? b[3] : 0;
                    if (temp_c != NULL)  *temp_c  = b[2] + temp_d / 10.0f;
                    if (hum_pct != NULL) *hum_pct = b[0] + hum_d / 10.0f;
                    if (s_last_fail)
                    {
                        ESP_LOGI(TAG, "read ok: %u.%uC %u.%u%% (%d symbols)",
                                 b[2], temp_d, b[0], hum_d, n);
                        s_last_fail = false;
                    }
                    return ESP_OK;
                }
                err = ESP_ERR_INVALID_CRC;
            }
            else err = ESP_ERR_INVALID_RESPONSE;
        }
        else err = ESP_ERR_TIMEOUT;
    }

    if (!s_last_fail)
    {
        ESP_LOGW(TAG, "read fail: %s (symbols=%d)", esp_err_to_name(err), n);
        s_last_fail = true;
    }
    return err;
}
