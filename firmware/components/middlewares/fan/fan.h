#ifndef FAN_H
#define FAN_H

#include <stdint.h>

/*风扇 DAC 控制：GPIO25 = DAC1 (DAC_CHANNEL_1)，输出 0~3.3V 模拟电压*/

#define FAN_KICK_DAC 255    /*强启输出（满量程，维持500ms由 app_state 控制）*/

#ifndef FAN_DAC_MIN
#define FAN_DAC_MIN 0       /*实机校准：风扇最低有效转速对应的 DAC 值*/
#endif
#ifndef FAN_DAC_MAX
#define FAN_DAC_MAX 255     /*实机校准：风扇满速对应的 DAC 值*/
#endif

void fan_init(void);
/*直接输出 DAC 原始值 0~255*/
void fan_set_raw(uint8_t dac_val);
/*转速百分比(0~100)映射为 DAC 值（FAN_DAC_MIN~FAN_DAC_MAX 线性）*/
uint8_t fan_speed_to_dac(uint8_t pct);

#endif
