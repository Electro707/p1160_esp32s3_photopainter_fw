#include "axp2101.h"
#include "pmic.h"
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define I2C_READ_WAIT       pdMS_TO_TICKS(500)

typedef uint8_t u8;
typedef uint16_t u16;

// global i2c device handler
i2c_master_dev_handle_t i2cDevPmic;

esp_err_t axp2101RegRead(u8 reg, u8 *val);
esp_err_t axp2101RegWrite(u8 reg, u8 val);

esp_err_t axp2101RegRead(u8 reg, u8 *val){
    return i2c_master_transmit_receive(i2cDevPmic, &reg, 1, val, 1, I2C_READ_WAIT);
}

esp_err_t axp2101RegWrite(u8 reg, u8 val){
    const u8 toWrite[2] = {reg, val};
    return i2c_master_transmit(i2cDevPmic, toWrite, 2, I2C_READ_WAIT);
}

u16 pmicGetVoltMonitor(u8 baseReg){
    u8 tmp;
    u16 volts = 0;

    // todo: can a register be read by continous read? if so, do that!
    axp2101RegRead(baseReg, &tmp);
    tmp &= 0x1F;
    volts |= ((u16)tmp << 8);
    axp2101RegRead(baseReg+1, &tmp);
    volts |= tmp;

    return volts;
}

void pmicGetTelemetry(pmicTelemetry *telemetry){
    telemetry->battVolt_mV = pmicGetVoltMonitor(APX2101_REG_MON_VBAT_BASE);
    telemetry->vBusVolt_mV = pmicGetVoltMonitor(APX2101_REG_MON_VBUS_BASE);
    telemetry->sysVolt_mV = pmicGetVoltMonitor(APX2101_REG_MON_VSYS_BASE);
    // todo: battery percentage, is charged, etc
}

void pmicInit(i2c_master_bus_handle_t *masterHandle){
    // init i2c bus
    i2c_device_config_t dev_cfg;
    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = CONFIG_I2C_ADDR_AXP2101;
    // proving the i2c waveform, it has a very slow risetime, to the point where 400kHz doesn't get up to vcc by the time it trips back low
    // while it does *work*, it might cause unforseen issues. something to keep in mind
    // probably why Waveshare's example had it down to 300Khz
    dev_cfg.scl_speed_hz    = 400000;
    // dev_cfg.scl_speed_hz    = 250000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*masterHandle, &dev_cfg, &i2cDevPmic));

    // init device
    u8 wakeReg;
    ESP_ERROR_CHECK(axp2101RegRead(APX2101_REG_SLEEP_WAKE, &wakeReg));
    wakeReg &= ~0x08;       // clear that PWROK to be set low during wakeup
    if(wakeReg & 0x01){         // if we are in sleep mode, wake the device up
        wakeReg |= 0x02;        // wake up enable = 1
    }
    // todo: disable NTC measurement
    axp2101RegWrite(APX2101_REG_ADC_EN, 0xFD);              // disable TS pin ADC
    axp2101RegWrite(APX2101_REG_SLEEP_WAKE, wakeReg);
    axp2101RegWrite(APX2101_REG_LEVEL_DUR, 0b110000);       // irqlevel: 2.5s, offlevel: 4s, onlevel: 120mS
    axp2101RegWrite(APX2101_REG_CHGLED_CNT, 0x00);          // disable CHGLED functionality
    axp2101RegWrite(APX2101_REG_MAIN_CHARGE_VOLT, 0b010);        // 4.1v max charge
    axp2101RegWrite(APX2101_REG_BUTTON_TERM_VOLT, 0b111);        // 3.3v termination for button
    axp2101RegWrite(APX2101_REG_CHARGE_EN, 0b1110);         // enable guage module, button charging, main cell charging, disable watchdog
    axp2101RegWrite(APX2101_REG_DC1_VOLT, APX2101_DCDC_VOLT_3V3);
    axp2101RegWrite(APX2101_REG_LDO_EN0, 0x0C);       // disable all LDOs except for e-ink one
                                                      // note: without enabling the audio LDO, the system crashes and the I2C bus (what I probed use far)
                                                      // falls to ~1v. Stupid, deal with later
    axp2101RegWrite(APX2101_REG_LDO_EN1, 0x00);
    axp2101RegWrite(APX2101_REG_ALDO4_VOLT, APX2101_ALDO_VOLT_3V3);
}