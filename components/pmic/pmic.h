#ifndef PMIC_H
#define PMIC_H

#include "driver/i2c_master.h"


typedef struct{
    uint16_t battVolt_mV;
    uint16_t vBusVolt_mV;
    uint16_t sysVolt_mV;
    uint8_t battPercentage;
}pmicTelemetry;

void pmicInit(i2c_master_bus_handle_t *masterHandle);
void pmicGetTelemetry(pmicTelemetry *telemetry);

#endif