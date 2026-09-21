#ifndef DHT11_H
#define DHT11_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_check.h"
#include "driver/gpio.h"

#define DHT11_GPIO GPIO_NUM_4    /*DHT11 DATA 引脚*/

esp_err_t dht11_init(void);
/*阻塞读取一次（约25~140ms），两次调用间隔应>=1s。
  成功时写 temp_c 与 hum_pct 并返回 ESP_OK，失败返回错误码且不改动输出。
  DATA 线采用开漏驱动（内部上拉即可，兼容外部上拉模块）。*/
esp_err_t dht11_read(float *temp_c, float *hum_pct);

#endif
