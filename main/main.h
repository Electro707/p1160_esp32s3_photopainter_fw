#if !defined(MAIN_H) && !defined(UNIT_TEST)
#define MAIN_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "sdmmc_cmd.h"

#include "pmic.h"

typedef enum{
    MODE_STANDBY,
    MODE_IMAGE_CYCLE
}mode_e;

typedef enum{
    IMAGE_CYCLE_MODE_SELECTED,      // cycle through pre-selected images
    IMAGE_CYCLE_MODE_ALL,           // cycle through all images on the SD card
    IMAGE_CYCLE_MODE_RANDOM,        // randomly get an image per cycle to display
}imgCycleMode_e;

typedef enum{
    RET_SET_MODE_OK = 0,
    RET_SET_MODE_IMG_CYCLE_NONE_SET,
    RET_SET_MODE_IMG_NO_IMG,
    RET_SET_MODE_ERR,
}setModeRet_e;

typedef struct{
    imgCycleMode_e mode;
    TickType_t period_ticks;      // the duration of the cycle in rtos ticks. Must NOT be less than 12-15 seconds due to display refresh rate
    char imgCycleSel[MAX_IMAGE_CYCLE_N][MAX_IMAGE_NAME_LEN];
    u32 imgCycleSelAvail[MAX_IMAGE_CYCLE_N];        // if an image in index X is used (1) or not (0)
                                                    // this allows to remove images at any index
    // u32 imgCycleSelTotal;       // number of images to cycle through
    // internal variables
    u32 imgCycleTotalImg;       // total number of images available    
    u32 cycleCurrIdx;           // the current image index
    TimerHandle_t handler;
}imgCycleSettings_t;

extern spi_device_handle_t dispSpi;             // global spi device
extern i2c_master_bus_handle_t i2cHandle;       // global i2c handler
extern sdmmc_card_t sdCard;                     // global sdcard handler

extern pmicTelemetry pmicTelem;                        // global pmic telemetry struct
                                                // todo: move it so a dedicated function returns a pointer to it or None if busy
extern SemaphoreHandle_t pmicTelemetryMutex;           // mutex for pmic telemetry

// todo: lock this from threading, OR have dedicated functions to access it's variables
extern imgCycleSettings_t imgCycleSettings;
extern mode_e runMode;

/**
 * Gets the firmware operation mode
 */
setModeRet_e setMode(mode_e newMode);

u32 dispTrigUpdate(void);

#endif