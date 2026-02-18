#if !defined(MAIN_H) && !defined(UNIT_TEST)
#define MAIN_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#include "pmic.h"


extern spi_device_handle_t dispSpi;             // global spi device
extern i2c_master_bus_handle_t i2cHandle;       // global i2c handler

extern SemaphoreHandle_t displayFbMutex;        // mutex for display updating

extern pmicTelemetry pmicTelem;                        // global pmic telemetry struct
                                                // todo: move it so a dedicated function returns a pointer to it or None if busy
extern SemaphoreHandle_t pmicTelemetryMutex;           // mutex for pmic telemetry

void dispTrigUpdate(void);

#endif