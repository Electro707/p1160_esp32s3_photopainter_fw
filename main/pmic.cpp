#include "esp_log.h"
#include "pmic.h"
#include "common.h"
#include "main.h"
#include "XPowersLib.h"

static XPowersPMU axp2101;


static int AXP2101_SLAVE_Read(u8 devAddr, u8 regAddr, u8 *data, u8 len) {
    int ret;

    i2c_master_bus_wait_all_done(i2cHandle, pdMS_TO_TICKS(1000));
    ret = i2c_master_transmit_receive(i2cDevPmic, &regAddr, 1, data, len, pdMS_TO_TICKS(5000));

    return 0;
}

static int AXP2101_SLAVE_Write(u8 devAddr, u8 regAddr, u8 *data, u8 len) {
    int ret;
    uint8_t *pbuf = NULL;

    i2c_master_bus_wait_all_done(i2cHandle, pdMS_TO_TICKS(1000));

    // YUCK! dynamic memory
    // once I have more time with shall get re-written so it's sane
    pbuf = (uint8_t *) malloc(len + 1);
    pbuf[0] = regAddr;
    for (uint8_t i = 0; i < len; i++) {
        pbuf[i + 1] = data[i];
    }
    ret = i2c_master_transmit(i2cDevPmic, pbuf, len + 1, pdMS_TO_TICKS(5000));
    free(pbuf);
    pbuf = NULL;

    return 0;
}

void pmic_init(void){
    if (axp2101.begin(I2C_ADDR_AXP2101, AXP2101_SLAVE_Read, AXP2101_SLAVE_Write)) {
        ESP_LOGW("axp2101_init_log", "Init PMU SUCCESS!");
    } else {
        ESP_LOGW("axp2101_init_log", "Init PMU FAILED!");
    }

    int data = axp2101.readRegister(0x26);
    ESP_LOGW("axp2101_init_log","reg_26:0x%02x",data);
    if(data & 0x01) {
        axp2101.enableWakeup();
        ESP_LOGW("axp2101_init_log","i2c_wakeup");
    }
    if(data & 0x08) {
        axp2101.wakeupControl(XPOWERS_AXP2101_WAKEUP_PWROK_TO_LOW, false);
        ESP_LOGW("axp2101_init_log","When setting the wake-up operation, pwrok does not need to be pulled down.");
    }
    if(axp2101.getPowerKeyPressOffTime() != XPOWERS_POWEROFF_4S) {
        axp2101.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
        ESP_LOGW("axp2101_init_log","Press and hold the pwr button for 4 seconds to shut down the device.");
    }
    if(axp2101.getPowerKeyPressOnTime() != XPOWERS_POWERON_128MS) {
        axp2101.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
        ESP_LOGW("axp2101_init_log","Click PWR to turn on the device.");
    }
    if(axp2101.getChargingLedMode() != XPOWERS_CHG_LED_OFF) {
        axp2101.setChargingLedMode(XPOWERS_CHG_LED_OFF);
        ESP_LOGW("axp2101_init_log","Disable the CHGLED function.");
    }
    if(axp2101.getChargeTargetVoltage() != XPOWERS_AXP2101_CHG_VOL_4V1) {
        axp2101.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);
        ESP_LOGW("axp2101_init_log","Set the full charge voltage of the battery to 4.1V.");
    }
    if(axp2101.getButtonBatteryVoltage() != 3300) {
        axp2101.setButtonBatteryChargeVoltage(3300);
        ESP_LOGW("axp2101_init_log","Set Button Battery charge voltage");
    }
    if(axp2101.isEnableButtonBatteryCharge() == 0) {
        axp2101.enableButtonBatteryCharge();
        ESP_LOGW("axp2101_init_log","Enable Button Battery charge");
    }
    if(axp2101.getDC1Voltage() != 3300) {
        axp2101.setDC1Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set DCDC1 to output 3V3");
    }
    if(axp2101.getALDO3Voltage() != 3300) {
        axp2101.setALDO3Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set ALDO3 to output 3V3");
    }
    if(axp2101.getALDO4Voltage() != 3300) {
        axp2101.setALDO4Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set ALDO4 to output 3V3");
    }
}