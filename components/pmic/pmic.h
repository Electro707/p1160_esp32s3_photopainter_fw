#ifndef PMIC_H
#define PMIC_H

#include "driver/i2c_master.h"


typedef enum{
    PMIC_CHR_DIR_STANDBY = 0,
    PMIC_CHR_DIR_CHARGE = 1,
    PMIC_CHR_DIR_DISCHARGE = 2,
}pmicChrDir_e;

typedef enum{
    PMIC_CHR_STAT_TRI = 0,
    PMIC_CHR_STAT_PRE = 1,
    PMIC_CHR_STAT_CC = 2,
    PMIC_CHR_STAT_CV = 3,
    PMIC_CHR_STAT_DONE = 4,
    PMIC_CHR_STAT_NO_CHARGE = 5,
}pmicChrStat;

typedef struct{
    float battVolt;
    float vBusVolt;
    float sysVolt;
    uint8_t battPercentage;
    uint8_t vBusGood;
    uint8_t battPresent;
    uint8_t currLimited;
    pmicChrDir_e chargeDir;
    pmicChrStat chargeStat;
}pmicTelemetry;

void pmicInit(i2c_master_bus_handle_t *masterHandle);
void pmicGetTelemetry(pmicTelemetry *telemetry);

#endif