#include "ui.h"
#include "oled.h"
#include "oled_gfx.h"
#include "app_state.h"
#include "led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

/*------------- 音符图标（16x16 内程序化绘制：双符头+符干+斜梁） -------------*/

static void draw_note_icon(int x, int y)
{
    for (int dy = -3; dy <= 3; dy++)                 //两个实心符头
    {
        for (int dx = -3; dx <= 3; dx++)
        {
            if (dx * dx + dy * dy <= 8)
            {
                OLED_Gfx_Pixel(x + 4 + dx, y + 12 + dy, 1);
                OLED_Gfx_Pixel(x + 11 + dx, y + 10 + dy, 1);
            }
        }
    }
    for (int i = 0; i <= 8; i++)                     //两根符干
    {
        OLED_Gfx_Pixel(x + 7, y + 4 + i, 1);
        OLED_Gfx_Pixel(x + 14, y + 2 + i, 1);
    }
    OLED_Gfx_Line(x + 7, y + 4, x + 14, y + 1, 1);   //斜梁
    OLED_Gfx_Line(x + 7, y + 5, x + 14, y + 2, 1);
}

/*------------- 启动动画：约1.4s -------------*/

static void boot_animation(void)
{
    const int frames = 12;
    for (int f = 0; f <= frames; f++)
    {
        OLED_Gfx_Clear();
        draw_note_icon(56, 2 + (f % 2));             //图标轻微跳动
        OLED_Gfx_String(28, 24, "BlueMedia");
        OLED_Gfx_ProgressBar(24, 52, 80, 8, (uint8_t)(f * 100 / frames));
        OLED_Gfx_Flush();
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

static const char *ev_label(uint8_t ev)
{
    switch (ev)
    {
    case MEDIA_EV_VOL_UP:   return "VOL+";
    case MEDIA_EV_VOL_DOWN: return "VOL-";
    case MEDIA_EV_NEXT:     return "NEXT";
    case MEDIA_EV_PREV:     return "PREV";
    case MEDIA_EV_MODE:     return "MODE";
    default:                return "--";
    }
}

/*------------- 状态页 -------------*/

static void render_status(const app_state_t *st)
{
    char line[24];
    OLED_Gfx_Clear();

    /*行1：当前模式*/
    snprintf(line, sizeof(line), "MODE:%s", app_state_mode_name(st->mode));
    OLED_Gfx_String(0, 0, line);

    /*行2：最近事件*/
    snprintf(line, sizeof(line), "LAST:%s", ev_label(st->last_event));
    OLED_Gfx_String(0, 16, line);

    /*行3：音量条（由上位机回报；未回报时显示 --）*/
    if (st->volume_valid)
    {
        OLED_Gfx_String(0, 32, "VOL");
        OLED_Gfx_ProgressBar(28, 34, 96, 10, st->volume);
    }
    else
    {
        OLED_Gfx_String(0, 32, "VOL --");
    }

    /*行4：BLE连接 + 模式序号*/
    snprintf(line, sizeof(line), "BLE:%s M%u/%u",
             st->ble_connected ? "OK" : "--",
             (unsigned)(st->mode + 1), (unsigned)MEDIA_MODE_COUNT);
    OLED_Gfx_String(0, 48, line);

    OLED_Gfx_Flush();
}

void ui_task(void *arg)
{
    boot_animation();
    app_state_t st;
    int beats = 0;
    while (1)
    {
        app_state_get(&st);
        led(st.ble_connected);        /*LED=BLE连接指示*/
        render_status(&st);
        if (++beats >= 25)            /*心跳：卡死时用于判断本任务是否存活*/
        {
            beats = 0;
            ESP_LOGI("ui", "alive");
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
