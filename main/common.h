#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

////////// fw info
#define FW_NAME     "ESP32S3 PhotoPainter Open Firmware"
#define FW_REV      "0.1.0-dev"

////////// debug options
// #define DEBUG_DISABLE_DISPLAY_UPDATE            // use this to not actually update the display
                                                // used for example to test the image playlist functionality without waiting for display refreshes

////////// Pin definition

#define IO_DISP_DC      8
#define IO_DISP_CS      9
#define IO_DISP_SCK     10
#define IO_DISP_MOSI    11
#define IO_DISP_RST     12
#define IO_DISP_BUSY    13

#define IO_I2C_SDA      47
#define IO_I2C_SCL      48

#define IO_SDMMC_D0     40
#define IO_SDMMC_D1     1
#define IO_SDMMC_D2     2
#define IO_SDMMC_D3     38
#define IO_SDMMC_CLK    39
#define IO_SDMMC_CMD    41

#define IO_DBG_LED1     45
#define IO_DBG_LED2     42

// GPIO levels (high or low) to turn on or off LEDs
#define LED_LVL_OFF     1
#define LED_LVL_ON      0

////////// Misc settings
#define I2C_ADDR_AXP2101    0x34

#define MAX_WIFI_INFO_STRLEN       32

#define MAX_IMAGE_NAME_LEN      32

#define MAX_PLAYLIST_IMG       10                  // up to how many images can be added to a playlist

#define MIN_PLAYLIST_DUR        0.5                 // min, minimum duration for playlist

#define DEFAULT_SCAN_IMAGE_DUR_MIN      5           // min, the default duration for the image scan mode

#define INITIAL_BOOT_SLEEP_DELAY        6000        // mS, how long to wait after init before enabling auto sleep mode, which won't allow
                                                    // programming

#define PMIC_TELEMETRY_ACQ_DELAY        5000        // mS, how long to wait between each measurement of PMIC stats
////////// Other defines
#define EVER    ;;

////////// Sane typedefs
typedef uint8_t u8;
typedef uint32_t u32;

////////// macros
#define delayMs(_X)     vTaskDelay(_X / portTICK_PERIOD_MS)

#endif