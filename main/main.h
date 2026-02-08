#ifndef MAIN_H
#define MAIN_H


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

extern spi_device_handle_t dispSpi;        // global spi device
extern i2c_master_bus_handle_t i2cHandle;
extern i2c_master_dev_handle_t i2cDevPmic;

extern SemaphoreHandle_t displayFbMutex;

void dispTrigUpdate(void);

#endif