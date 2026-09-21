#include "app_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

/*模式名表：与 media_mode_t 前两项对应；自定义槽位名字可被上位机同步*/
static const char *const mode_names[] = {
    "MUSIC",     /*音乐控制*/
    "DOUYIN",    /*抖音控制*/
};
static char custom_names[2][7] = {"USER1", "USER2"};    /*USER1/USER2 槽位名（6字符ASCII）*/

static app_state_t s_state;
static SemaphoreHandle_t s_mutex;

static void st_lock(void)   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void st_unlock(void) { xSemaphoreGive(s_mutex); }

void app_state_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    memset(&s_state, 0, sizeof(s_state));
}

void app_state_get(app_state_t *out)
{
    st_lock();
    *out = s_state;
    st_unlock();
}

void app_state_set_mode(uint8_t mode)
{
    st_lock();
    s_state.mode = mode % MEDIA_MODE_COUNT;
    s_state.last_event = MEDIA_EV_MODE;
    st_unlock();
}

void app_state_cycle_mode(void)
{
    st_lock();
    s_state.mode = (uint8_t)((s_state.mode + 1) % MEDIA_MODE_COUNT);
    s_state.last_event = MEDIA_EV_MODE;
    st_unlock();
}

void app_state_set_event(uint8_t ev)
{
    st_lock();
    s_state.last_event = ev;
    st_unlock();
}

void app_state_set_volume(uint8_t pct, bool valid)
{
    if (pct > 100) pct = 100;
    st_lock();
    s_state.volume = pct;
    s_state.volume_valid = valid;
    st_unlock();
}

void app_state_set_ble_connected(bool on)
{
    st_lock();
    s_state.ble_connected = on;
    st_unlock();
}

const char *app_state_mode_name(uint8_t mode)
{
    mode %= MEDIA_MODE_COUNT;
    if (mode == MEDIA_MODE_USER1) return custom_names[0];
    if (mode == MEDIA_MODE_USER2) return custom_names[1];
    return mode_names[mode];
}

void app_state_set_mode_name(uint8_t mode, const char *name)
{
    if (mode < MEDIA_MODE_USER1 || mode >= MEDIA_MODE_COUNT || name == NULL) return;
    int idx = mode - MEDIA_MODE_USER1;
    int n = 0;
    while (name[n] != '\0' && n < 6)
    {
        char c = name[n];
        custom_names[idx][n] = (c >= 32 && c < 127) ? c : '?';    //仅可打印ASCII（OLED字库限制）
        n++;
    }
    if (n == 0)    //空名恢复默认
    {
        custom_names[idx][0] = 'U';
        custom_names[idx][1] = 'S';
        custom_names[idx][2] = 'E';
        custom_names[idx][3] = 'R';
        custom_names[idx][4] = (char)('0' + mode - MEDIA_MODE_USER1 + 1);
        n = 5;
    }
    custom_names[idx][n] = '\0';
}
