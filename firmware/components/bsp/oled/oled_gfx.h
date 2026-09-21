#ifndef OLED_GFX_H
#define OLED_GFX_H

#include <stdint.h>

/*128x64 帧缓冲图形接口：先绘制到 RAM 帧缓冲（1KB），OLED_Gfx_Flush() 整帧刷到屏幕*/

void OLED_Gfx_Clear(void);
void OLED_Gfx_Pixel(int16_t X, int16_t Y, uint8_t On);
void OLED_Gfx_Line(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, uint8_t On);
void OLED_Gfx_Rect(int16_t X, int16_t Y, int16_t W, int16_t H, uint8_t On);
void OLED_Gfx_FillRect(int16_t X, int16_t Y, int16_t W, int16_t H, uint8_t On);
void OLED_Gfx_Circle(int16_t CX, int16_t CY, int16_t R, uint8_t On);
/*页格式位图（每字节=一列8像素，bit0在上），H须为8的倍数；按位偏移写入（OR叠加）*/
void OLED_Gfx_Bitmap(int16_t X, int16_t Y, int16_t W, int16_t H, const uint8_t *Bits);
/*8x16 字符/字符串，像素级定位：X任意像素列，Y建议为8的倍数(0~48)*/
void OLED_Gfx_Char(int16_t X, int16_t Y, char Char);
void OLED_Gfx_String(int16_t X, int16_t Y, const char *String);
/*进度条：外框+按百分比填充*/
void OLED_Gfx_ProgressBar(int16_t X, int16_t Y, int16_t W, int16_t H, uint8_t Percent);
/*整帧刷写到OLED（8页×128字节，约9ms@400kHz）*/
void OLED_Gfx_Flush(void);

#endif
