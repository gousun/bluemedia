#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include <stdbool.h>

/*旋转编码器：A=GPIO32, B=GPIO33, 按键=GPIO13(低电平有效)*/

void encoder_init(void);
/*扫描旋转增量，需在主循环中频繁调用(<=10ms)。
  软件消抖10ms，每2个跳变=一格。
  返回：+1右转, -1左转, 0无*/
int16_t encoder_read(void);
/*按键按下事件，软件消抖20ms，一次按下只返回一次true*/
bool encoder_pressed(void);
/*按键释放沿，一次释放只返回一次true（与encoder_pressed配对使用）*/
bool encoder_released(void);

#endif
