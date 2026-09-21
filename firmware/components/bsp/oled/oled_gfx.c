#include "oled_gfx.h"
#include "oled.h"
#include "oled_font.h"
#include <string.h>

#define GFX_W     128
#define GFX_H     64
#define GFX_PAGES (GFX_H / 8)

/*帧缓冲：页结构（每页8行），与SSD1306显存布局一致，Flush可直接整页传输*/
static uint8_t s_fb[GFX_PAGES][GFX_W];

void OLED_Gfx_Clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void OLED_Gfx_Pixel(int16_t X, int16_t Y, uint8_t On)
{
    if (X < 0 || X >= GFX_W || Y < 0 || Y >= GFX_H) return;
    if (On) s_fb[Y / 8][X] |= (uint8_t)(1 << (Y % 8));
    else    s_fb[Y / 8][X] &= (uint8_t)~(1 << (Y % 8));
}

void OLED_Gfx_Line(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, uint8_t On)
{
    int16_t dx = X1 > X0 ? X1 - X0 : X0 - X1;
    int16_t dy = Y1 > Y0 ? Y1 - Y0 : Y0 - Y1;
    int16_t sx = X0 < X1 ? 1 : -1;
    int16_t sy = Y0 < Y1 ? 1 : -1;
    int16_t err = dx - dy;
    while (1)
    {
        OLED_Gfx_Pixel(X0, Y0, On);
        if (X0 == X1 && Y0 == Y1) break;
        int16_t e2 = (int16_t)(err * 2);
        if (e2 > -dy) { err -= dy; X0 += sx; }
        if (e2 <  dx) { err += dx; Y0 += sy; }
    }
}

void OLED_Gfx_Rect(int16_t X, int16_t Y, int16_t W, int16_t H, uint8_t On)
{
    if (W <= 0 || H <= 0) return;
    OLED_Gfx_Line(X, Y, X + W - 1, Y, On);
    OLED_Gfx_Line(X, Y + H - 1, X + W - 1, Y + H - 1, On);
    OLED_Gfx_Line(X, Y, X, Y + H - 1, On);
    OLED_Gfx_Line(X + W - 1, Y, X + W - 1, Y + H - 1, On);
}

void OLED_Gfx_FillRect(int16_t X, int16_t Y, int16_t W, int16_t H, uint8_t On)
{
    for (int16_t y = 0; y < H; y++)
    {
        for (int16_t x = 0; x < W; x++)
        {
            OLED_Gfx_Pixel(X + x, Y + y, On);
        }
    }
}

void OLED_Gfx_Circle(int16_t CX, int16_t CY, int16_t R, uint8_t On)
{
    if (R <= 0) return;
    int16_t x = R, y = 0;
    int16_t err = 1 - R;
    while (x >= y)    //中点圆算法，8分对称
    {
        OLED_Gfx_Pixel(CX + x, CY + y, On);
        OLED_Gfx_Pixel(CX - x, CY + y, On);
        OLED_Gfx_Pixel(CX + x, CY - y, On);
        OLED_Gfx_Pixel(CX - x, CY - y, On);
        OLED_Gfx_Pixel(CX + y, CY + x, On);
        OLED_Gfx_Pixel(CX - y, CY + x, On);
        OLED_Gfx_Pixel(CX + y, CY - x, On);
        OLED_Gfx_Pixel(CX - y, CY - x, On);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

/*把一个字节按像素行Y写入（跨页时自动拆分到相邻两页，OR叠加）*/
static void blit_byte(int16_t X, int16_t Y, uint8_t Byte)
{
    if (X < 0 || X >= GFX_W || Byte == 0) return;
    if (Y < -7 || Y >= GFX_H + 7) return;                    //完全出屏
    int16_t page = (Y >= 0) ? (Y / 8) : (int16_t)((Y - 7) / 8);    //向下取整
    uint8_t sh = (uint8_t)((Y % 8 + 8) % 8);
    if (page >= 0 && page < GFX_PAGES)
    {
        s_fb[page][X] |= (uint8_t)(Byte << sh);
    }
    if (sh != 0 && page + 1 >= 0 && page + 1 < GFX_PAGES)
    {
        s_fb[page + 1][X] |= (uint8_t)(Byte >> (8 - sh));
    }
}

void OLED_Gfx_Bitmap(int16_t X, int16_t Y, int16_t W, int16_t H, const uint8_t *Bits)
{
    if (W <= 0 || H <= 0 || H % 8 != 0 || Bits == NULL) return;
    for (int16_t p = 0; p < H / 8; p++)            //源页（每页8行）
    {
        for (int16_t i = 0; i < W; i++)            //源列
        {
            blit_byte(X + i, Y + p * 8, Bits[p * W + i]);
        }
    }
}

void OLED_Gfx_Char(int16_t X, int16_t Y, char Char)
{
    if (Char < ' ' || Char > '~') Char = '?';
    OLED_Gfx_Bitmap(X, Y, 8, 16, OLED_F8x16[Char - ' ']);
}

void OLED_Gfx_String(int16_t X, int16_t Y, const char *String)
{
    int16_t cx = X;
    for (int16_t i = 0; String[i] != '\0'; i++, cx += 8)
    {
        OLED_Gfx_Char(cx, Y, String[i]);
    }
}

void OLED_Gfx_ProgressBar(int16_t X, int16_t Y, int16_t W, int16_t H, uint8_t Percent)
{
    if (W <= 0 || H <= 0) return;
    if (Percent > 100) Percent = 100;
    OLED_Gfx_Rect(X, Y, W, H, 1);
    if (H > 2 && W > 2)
    {
        int16_t fw = (int16_t)((W - 2) * Percent / 100);
        if (fw > 0) OLED_Gfx_FillRect(X + 1, Y + 1, fw, H - 2, 1);
    }
}

void OLED_Gfx_Flush(void)
{
    for (int16_t p = 0; p < GFX_PAGES; p++)
    {
        OLED_SetCursor((uint8_t)p, 0);
        OLED_WriteDataBulk(s_fb[p], GFX_W);
    }
}
