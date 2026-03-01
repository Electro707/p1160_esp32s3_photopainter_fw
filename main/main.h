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
}imgCycleMode_e;

extern spi_device_handle_t dispSpi;             // global spi device
extern i2c_master_bus_handle_t i2cHandle;       // global i2c handler
extern sdmmc_card_t sdCard;                     // global sdcard handler

extern pmicTelemetry pmicTelem;                        // global pmic telemetry struct
                                                // todo: move it so a dedicated function returns a pointer to it or None if busy
extern SemaphoreHandle_t pmicTelemetryMutex;           // mutex for pmic telemetry

u32 dispTrigUpdate(void);

#endif