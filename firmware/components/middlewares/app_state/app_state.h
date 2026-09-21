#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>

/*BlueMedia 设备共享状态：模式/最近事件/音量/BLE连接，互斥锁保护*/

/*模式枚举：新增模式在此加一项，并在 mode_names[] 加名字，
  上位机 MODES 表加同编号映射即可（留作功能扩展接口）*/
typedef enum
{
    MEDIA_MODE_MUSIC = 0,    /*音乐控制*/
    MEDIA_MODE_DOUYIN,       /*抖音控制*/
    MEDIA_MODE_USER1,        /*自定义槽位1（名字由上位机同步，<=6字符ASCII）*/
    MEDIA_MODE_USER2,        /*自定义槽位2*/
    MEDIA_MODE_COUNT
} media_mode_t;

/*最近事件（供OLED显示标签）*/
typedef enum
{
    MEDIA_EV_NONE = 0,
    MEDIA_EV_VOL_UP,
    MEDIA_EV_VOL_DOWN,
    MEDIA_EV_NEXT,
    MEDIA_EV_PREV,
    MEDIA_EV_MODE,
} media_event_t;

typedef struct
{
    uint8_t mode;             /*当前模式 media_mode_t*/
    uint8_t last_event;       /*最近事件 media_event_t*/
    uint8_t volume;           /*上位机回报的系统音量 0~100*/
    bool    volume_valid;     /*音量有效（上位机已回报过）*/
    bool    ble_connected;
} app_state_t;

void app_state_init(void);
void app_state_get(app_state_t *out);
/*切到指定模式（越界自动取模）*/
void app_state_set_mode(uint8_t mode);
/*切换到下一模式（长按）*/
void app_state_cycle_mode(void);
/*记录最近事件（供OLED显示）*/
void app_state_set_event(uint8_t ev);
/*更新上位机回报的音量*/
void app_state_set_volume(uint8_t pct, bool valid);
void app_state_set_ble_connected(bool on);
/*模式名（"MUSIC"/"DOUYIN"/自定义名，用于BLE与OLED）*/
const char *app_state_mode_name(uint8_t mode);
/*设置自定义模式名（仅USER1/USER2有效；ASCII，<=6字符，超长截断，空串恢复默认）*/
void app_state_set_mode_name(uint8_t mode, const char *name);

#endif
