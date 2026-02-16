/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include "soc/system_struct.h"
#include "soc/spi_struct.h"
#include "hal/spi_ll.h"
#include "esp_private/periph_ctrl.h"
#include "hal/dma_types.h"
#include "esp_private/gdma.h"
#include "esp_pm.h"
#include "nvs_flash.h"

#include "common.h"
#include "main.h"
#include "pmic.h"
#include "eink.h"
#include "network.h"

spi_device_handle_t dispSpi;        // global spi device
i2c_master_bus_handle_t i2cHandle;
i2c_master_dev_handle_t i2cDevPmic;

// the display updater task starter/handler
TaskHandle_t dispTask_h;
SemaphoreHandle_t displayFbMutex;

void dispFreeRtosUpdateLoop(void *args);

void mcuInit(void){
    // configure Dynamic Frequency Scaling (DFS) settings
    esp_pm_config_t pm_config = {
            .max_freq_mhz = 160,
            .min_freq_mhz = 20,
            .light_sleep_enable = false
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // init IO
    gpio_config_t gpio_conf;
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;

    // init IO, display input
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = ((uint64_t) 0x01 << IO_DISP_RST) | ((uint64_t) 0x01 << IO_DISP_DC) | ((uint64_t) 0x01 << IO_DISP_CS);
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));
    // init IO, debug LEDs
    gpio_conf.pin_bit_mask  = ((uint64_t) 0x01 << IO_DBG_LED1) | ((uint64_t) 0x01 << IO_DBG_LED2);
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));
    // init IO, display input
    gpio_conf.mode         = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = ((uint64_t) 0x01 << IO_DISP_BUSY);
    gpio_conf.pull_up_en   = GPIO_PULLUP_ENABLE;        // enable pull-up for busy, shouldn't be really needed but alas...
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));

    // init spi
    spi_bus_config_t buscfg = {0};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = IO_DISP_MOSI;
    buscfg.sclk_io_num = IO_DISP_SCK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = DISPLAY_H * DISPLAY_H / 2;
    spi_device_interface_config_t devcfg = {0};
    devcfg.spics_io_num = -1;
    devcfg.clock_speed_hz = 10 * 1000 * 1000; //Clock out at 10 MHz
    devcfg.mode = 0;                //SPI mode 0
    devcfg.queue_size = 7;                //We want to be able to queue 7 transactions at a time
    devcfg.command_bits = 0;
    devcfg.address_bits = 0;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &devcfg, &dispSpi));

    // init i2c
    i2c_master_bus_config_t i2c_bus_cfg;
    memset(&i2c_bus_cfg, 0, sizeof(i2c_bus_cfg));
    i2c_bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_cfg.i2c_port = 0;
    i2c_bus_cfg.scl_io_num = IO_I2C_SCL;
    i2c_bus_cfg.sda_io_num = IO_I2C_SDA;
    i2c_bus_cfg.glitch_ignore_cnt = 7;
    i2c_bus_cfg.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2cHandle));
    i2c_device_config_t dev_cfg;
    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = I2C_ADDR_AXP2101;
    dev_cfg.scl_speed_hz    = 300000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2cHandle, &dev_cfg, &i2cDevPmic));

    // init nvs flash, which is used for some wifi stuff
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

}

void app_main(void)
{
    printf("Hello world!\n");
    mcuInit();
    pmic_init();
    dispInit();

    displayFbMutex = xSemaphoreCreateMutex();

    wifiInit();
    startHttpServer();

    printf("Done with init\n");

    xTaskCreatePinnedToCore(dispFreeRtosUpdateLoop, "display", 4096, NULL, 5,
                            &dispTask_h, 0);
}

void dispTrigUpdate(void){
    xTaskNotifyGive(dispTask_h);
}

void dispFreeRtosUpdateLoop(void *args){
    for(EVER){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(displayFbMutex, portMAX_DELAY);
        dispUpdate();
        xSemaphoreGive(displayFbMutex);
    }
}