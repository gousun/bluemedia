/*BlueMedia: BLE 媒体控制器
  旋钮=音量，短按=下一个，双击=上一个，长按=切换模式（音乐/抖音）
  设备只上报事件，具体按键动作由 PC 上位机（blueapp/media_host.py）按模式映射*/

#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "led.h"
#include "oled.h"
#include "encoder.h"
#include "app_state.h"
#include "ble_svc.h"
#include "ui.h"

#define BTN_LONG_MS        700    /*长按判定阈值*/
#define BTN_DOUBLE_MS      250    /*双击两次按下最大间隔*/
#define BTN_HOLD_SHORT_MS  400    /*按住>=此值后释放：立即判短按，不进双击窗口
                                    （避免慢速单击被误判为双击的前半段）*/

static const char *BTN_TAG = "btn";
static const char *btn_state_name[] = { "IDLE", "PRESSED", "WAIT2" };

static void send_event(const char *ev, uint8_t app_ev)
{
    app_state_set_event(app_ev);
    ESP_LOGI(BTN_TAG, "send %s", ev);
    ble_svc_notify_event(ev);
}

/*编码器+按键任务（5ms轮询）：
  旋转→EV:ROT; 按键状态机→EV:KEY:S(短按)/EV:KEY:D(双击)/EV:MODE:n(长按)
  长按在按住达到阈值时立即触发（全局模式切换）*/
static void encoder_task(void *arg)
{
    typedef enum { BTN_IDLE, BTN_PRESSED, BTN_WAIT2 } btn_state_t;
    btn_state_t bs = BTN_IDLE;
    int64_t press_t0 = 0, release_t0 = 0, last_beat = 0;
    bool long_fired = false;
    char buf[16];

    while (1)
    {
        int16_t delta = encoder_read();
        if (delta != 0)
        {
            snprintf(buf, sizeof(buf), "EV:ROT:%+d", delta);
            send_event(buf, delta > 0 ? MEDIA_EV_VOL_UP : MEDIA_EV_VOL_DOWN);
        }

        bool pressed = encoder_pressed();
        bool released = encoder_released();
        int64_t now = esp_timer_get_time() / 1000;

        if (pressed)  ESP_LOGI(BTN_TAG, "press edge   state=%s", btn_state_name[bs]);
        if (released) ESP_LOGI(BTN_TAG, "release edge state=%s", btn_state_name[bs]);

        if (now - last_beat >= 5000)    /*心跳：卡死时用于判断本任务是否存活*/
        {
            last_beat = now;
            ESP_LOGI(BTN_TAG, "alive state=%s", btn_state_name[bs]);
        }

        switch (bs)
        {
        case BTN_IDLE:
            if (pressed)
            {
                bs = BTN_PRESSED;
                press_t0 = now;
                long_fired = false;
            }
            break;

        case BTN_PRESSED:
            if (!long_fired && now - press_t0 >= BTN_LONG_MS)
            {
                long_fired = true;
                app_state_cycle_mode();                 /*长按：切换模式（全局）*/
                app_state_t st;
                app_state_get(&st);
                snprintf(buf, sizeof(buf), "EV:MODE:%u", (unsigned)(st.mode + 1));
                send_event(buf, MEDIA_EV_MODE);
            }
            if (released)
            {
                if (long_fired)
                {
                    bs = BTN_IDLE;                      /*长按已触发，忽略本次释放*/
                }
                else if (now - press_t0 >= BTN_HOLD_SHORT_MS)
                {
                    send_event("EV:KEY:S", MEDIA_EV_NEXT);    /*按住较久的单击：立即生效*/
                    bs = BTN_IDLE;
                }
                else
                {
                    release_t0 = now;
                    bs = BTN_WAIT2;
                }
            }
            break;

        case BTN_WAIT2:    /*第一次已释放，等待可能的第二次按下*/
            if (pressed)
            {
                bs = BTN_PRESSED;                       /*双击确认；第二击按住不再触发长按*/
                press_t0 = now;
                long_fired = true;
                send_event("EV:KEY:D", MEDIA_EV_PREV);
            }
            else if (now - release_t0 >= BTN_DOUBLE_MS)
            {
                send_event("EV:KEY:S", MEDIA_EV_NEXT);  /*超时无第二击=短按*/
                bs = BTN_IDLE;
            }
            break;

        default:
            bs = BTN_IDLE;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(5));    //编码器扫描需<=10ms周期
    }
}

void app_main(void)
{
    led_init();
    led(0);

    app_state_init();
    OLED_Init();
    encoder_init();
    ble_svc_init();

    xTaskCreate(encoder_task, "encoder", 3072, NULL, 6, NULL);
    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);
}
