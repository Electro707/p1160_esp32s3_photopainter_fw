#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define FW_NAME     "ESP32S3 PhotoPainer Open Firmware"
#define FW_REV      "0.1.0-dev"

////////// Pin definition

#define IO_DISP_DC      8
#define IO_DISP_CS      9
#define IO_DISP_SCK     10
#define IO_DISP_MOSI    11
#define IO_DISP_RST     12
#define IO_DISP_BUSY    13

#define IO_I2C_SDA      47
#define IO_I2C_SCL      48

#define IO_DBG_LED1     45
#define IO_DBG_LED2     42

#define I2C_ADDR_AXP2101    0x34

////////// Other defines
#define EVER    ;;

typedef uint8_t u8;
typedef uint32_t u32;

#define delayMs(_X)     vTaskDelay(_X / portTICK_PERIOD_MS)

#endif