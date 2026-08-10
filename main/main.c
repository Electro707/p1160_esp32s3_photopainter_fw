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
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

// stuff that I was trying to do my own SPI implementation
// #include "soc/gpio_struct.h"
// #include "soc/io_mux_reg.h"
// #include "soc/system_struct.h"
// #include "soc/spi_struct.h"
// #include "hal/spi_ll.h"
// #include "esp_private/periph_ctrl.h"
// #include "hal/dma_types.h"
// #include "esp_private/gdma.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_random.h"

#include "common.h"
#include "main.h"
#include "eink.h"
#include "network.h"
#include "fileSys.h"

// configure as part of RTC NOINIT RAM due to deep sleep
RTC_NOINIT_ATTR imgPlaylist_t imgPlaylist = { 0 };
RTC_NOINIT_ATTR mode_e runMode;

spi_device_handle_t dispSpi;        // global spi device
i2c_master_bus_handle_t i2cHandle;
sdmmc_card_t sdCard;

pmicTelemetry pmicTelem;

// the display updater task starter/handler
TaskHandle_t dispTask_h;
EventGroupHandle_t dispEvents;
enum{
    RTOS_DISP_EVENT_COMPLETE = 0x01,    // set when we finish updating the display, cleared when we are using the display
};
StaticEventGroup_t dispEventsStaticData;

TaskHandle_t pmicTelemTask_h;
SemaphoreHandle_t pmicTelemetryMutex;

void taskTimerImagePlaylist(TimerHandle_t xTimer);
void taskPmicTelemetry(void *args);
void taskDispUpdate(void *args);
int imagePlaylistLoad(void);

static const char *TAG = "main";

void mcuInit(void){
    // configure Dynamic Frequency Scaling (DFS) settings
    esp_pm_config_t pm_config = {
            .max_freq_mhz = 160,
            .min_freq_mhz = 40,
#ifdef CONFIG_APP_ENABLE_LIGHT_SLEEP
            .light_sleep_enable = true
#else
            .light_sleep_enable = false
#endif
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // init IO
    gpio_config_t gpio_conf;
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;

    // init IO as output, display input
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = ((uint64_t) 0x01 << IO_DISP_RST) | ((uint64_t) 0x01 << IO_DISP_DC) | ((uint64_t) 0x01 << IO_DISP_CS);
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));
    // init IO as output, debug LEDs
    gpio_conf.pin_bit_mask  = ((uint64_t) 0x01 << IO_DBG_LED1) | ((uint64_t) 0x01 << IO_DBG_LED2);
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));
    // init IO as input, display input
    gpio_conf.mode         = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = ((uint64_t) 0x01 << IO_DISP_BUSY);
    gpio_conf.pull_up_en   = GPIO_PULLUP_ENABLE;        // enable pull-up for busy, shouldn't be really needed but alas...
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));

    // turn LEDs off by default
    gpio_set_level(IO_DBG_LED1, LED_LVL_OFF);
    gpio_set_level(IO_DBG_LED2, LED_LVL_OFF);

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

    // init sd card
    ESP_ERROR_CHECK(sdmmc_host_init());
    sdmmc_slot_config_t sdmmcConf = {
        .clk = IO_SDMMC_CLK,
        .cmd = IO_SDMMC_CMD,
        .d0 = IO_SDMMC_D0,
        .d1 = IO_SDMMC_D1,
        .d2 = IO_SDMMC_D2,
        .d3 = IO_SDMMC_D3,
        .d4 = GPIO_NUM_NC,
        .d5 = GPIO_NUM_NC,
        .d6 = GPIO_NUM_NC,
        .d7 = GPIO_NUM_NC,
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };
    ESP_ERROR_CHECK(sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &sdmmcConf));

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_card_init(&host, &sdCard);

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

void app_main(void){
    esp_err_t stat;

    printf("Hello world!\n");
    mcuInit();
    pmicInit(&i2cHandle);
    dispInit();

    // load SD card
    stat = sdmmc_get_status(&sdCard);
    if(stat == ESP_OK){
        stat = initFs();
        mountFs();
        // todo: do something with return value?
    } else {
        // todo: handle hot plug?
        ESP_LOGW(TAG, "SD card status not good! %d", stat);
    }

    delayMs(INITIAL_BOOT_SLEEP_DELAY);      // debug, remove when firmware is tested

    // at this point we check if we were in "low power cycle" mode, or if
    // in low power cycle mode, check wake source. If timer, put on next image and exit.
    // if from button, revert mode to standby
    esp_sleep_wakeup_cause_t wakeSource = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "sleep source: %d", wakeSource);
    if(wakeSource == ESP_SLEEP_WAKEUP_TIMER){
        if(runMode == MODE_IMAGE_PLAYLIST_LP){
            ESP_LOGI(TAG, "Booted up from low power while we have a playlist, load the next image and go back to sleep");
            // put the next image up and bail instantly!
            imagePlaylistLoad();

            pmicEnableLDOs();
            delayMs(100);            // some boot up time, todo: instrument
            dispBoot();
            dispUpdate();

            goDeepSleep();
        }
    }
    else if(wakeSource == ESP_SLEEP_WAKEUP_EXT1){
        // if we manually woke the thing up, assume standby mode so we can connect to it over WiFi and other stuff
        // todo: is it better to assume MODE_IMAGE_PLAYLIST instead? So we can keep the picture frame while
        //       allowing WiFi connectivity?
        ESP_LOGI(TAG, "Manually woke up device with EXT0, switching to standby mode");
        runMode = MODE_STANDBY;
    }
    else{
        runMode = MODE_STANDBY;
    }

    // todo: load these configs from NVM
    imgPlaylist.period_ticks = configTICK_RATE_HZ * 60 * DEFAULT_SCAN_IMAGE_DUR_MIN;
    imgPlaylist.mode = PLAYLIST_MODE_RANDOM;

    // setup WiFi and http server
    wifiInit();
    startHttpServer();

    // create FreeRTOS objects
    memset(&pmicTelem, 0, sizeof(pmicTelem));
    pmicTelemetryMutex = xSemaphoreCreateMutex();

    dispEvents = xEventGroupCreateStatic(&dispEventsStaticData);
    configASSERT( dispEvents );

    // create FreeRTOS tasks and timers
    imgPlaylist.timerHandler = xTimerCreate("playlist", imgPlaylist.period_ticks, pdTRUE, ( void * )0, taskTimerImagePlaylist);

    xTaskCreatePinnedToCore(taskDispUpdate, "display", 4096, NULL, 4,
                            &dispTask_h, 0);

    xTaskCreatePinnedToCore(taskPmicTelemetry, "pmicTelem", 4096, NULL, 4,
                            &pmicTelemTask_h, 0);

    printf("Done with init\n");

    // dev notes: adding logic to disable ES7210 doesn't make a difference
    // debug, used to determine power consumption with deep sleep and what can be done to save on power
    // delayMs(INITIAL_BOOT_SLEEP_DELAY);
    // init i2c bus
    // i2c_device_config_t i2cConf;
    // i2c_master_dev_handle_t audDev;
    // memset(&i2cConf, 0, sizeof(i2cConf));
    // i2cConf.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    // i2cConf.device_address  = 0x40;
    // i2cConf.scl_speed_hz    = 250000;
    // i2c_master_bus_add_device(i2cHandle, &i2cConf, &audDev);
    // // const u8 toWrite2[2] = {0x01, 0xFF};
    // // i2c_master_transmit(audDev, toWrite2, 2, pdMS_TO_TICKS(500));
    // // const u8 toWrite1[2] = {0x00, 0b00110001};
    // // i2c_master_transmit(audDev, toWrite1, 2, pdMS_TO_TICKS(500));
    // pmicDisableLDOsAll();
    // delayMs(100);
    // esp_deep_sleep_start();
}

void goDeepSleep(void){
    uint32_t n;

    ESP_LOGI(TAG, "Commanded to go to deep sleep");
    if(runMode == MODE_IMAGE_PLAYLIST_LP){
        // enable timer
        // todo: limit check
        ESP_LOGI(TAG, "Setting timer to %d mS", imgPlaylist.period_ticks);      // debug
        esp_sleep_enable_timer_wakeup((uint64_t)imgPlaylist.period_ticks * 1000 * 1000 / configTICK_RATE_HZ);
        // todo: setup RTC memory to store the last state
    }
    // always enable the boot button as a valid wakeup source
    // we must use EXT1 as the button is a pulldown, while EXT0 only supports when the IO goes high
    rtc_gpio_pullup_en(IO_BUTTON);
    rtc_gpio_pulldown_dis(IO_BUTTON);
    rtc_gpio_hold_en(IO_BUTTON);
    esp_sleep_enable_ext1_wakeup((1 << IO_BUTTON), ESP_EXT1_WAKEUP_ANY_LOW);

    // keep RTC enabled due to SRAM used
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // disable all LDOs and wait for I2C to finish operations
    // todo: shutdown PMIC monitor task
    // pmicDisableLDOsAll();
    i2c_master_bus_wait_all_done(i2cHandle, 100);

    // wait for display handler to finish
    // ESP_LOGI(TAG, "Waiting for display to finish updating before going to low power mode");
    // waitForDisplay(portMAX_DELAY);

    ESP_LOGI(TAG, "Good night");
    GOOD_NIGHT();
}

void lowPowerDisplayUpdate(void){
    // this is called when the display is to be updated due to a low power image cycle
    // just load the next image and bail
}

setModeRet_e setMode(mode_e newMode){
    if(newMode == MODE_IMAGE_PLAYLIST || newMode == MODE_IMAGE_PLAYLIST_LP){
        // set totalImg before going into the mode
        if(fileSysGetAvailableImages(NULL, &imgPlaylist.totalImg)){
            return RET_SET_MODE_ERR;
        }
        // check we have images available. Should be at least 2 for an image playlist to work as a playlist
        if(imgPlaylist.totalImg < 2){
            return RET_SET_MODE_IMG_NO_IMG;
        }
        // check we have images selected if in selection mode
        if(imgPlaylist.mode == PLAYLIST_MODE_SELECT){
            u32 sum = 0;    // really cheeky way to see if we have any image available. because the available variable
                            // will just be 1 or 0, we can just sum the array and check if the sum is 0, meaning no image
                            // is present
            for(int i=0;i<MAX_PLAYLIST_IMG;i++){
                sum += imgPlaylist.imgSelectEn[i];
            }
            if(sum == 0){
                return RET_SET_MODE_IMG_PL_NONE_SET;
            }
        }
        imgPlaylist.currIdx = 0;
        xTimerChangePeriod(imgPlaylist.timerHandler, imgPlaylist.period_ticks, pdTICKS_TO_MS(100));
        // xTimerStart(imgPlaylist.timerHandler, 0);        // above change period causes it to start
    } else {
        xTimerStop(imgPlaylist.timerHandler, 0);
    }
    runMode = newMode;
    return RET_SET_MODE_OK;
}

int dispTrigUpdate(void){
    u32 prevVal;
    xTaskNotifyAndQuery(dispTask_h, 0, eIncrement, &prevVal);
    if(prevVal != 0){
        return 1;
    }
    return 0;
}

#define s imgPlaylist      // nice macro as I don't feel like calling the long struct name for the function below
void taskTimerImagePlaylist(TimerHandle_t xTimer){
    u32 stat;

    ESP_LOGI(TAG, "Playlist timer triggered, loading next image");
    stat = imagePlaylistLoad();
    if(stat){
        ESP_LOGW(TAG, "Failed to load image playlist, stat=%d", stat);
        return;
    }
    stat = dispTrigUpdate();
    if(stat){
        ESP_LOGW(TAG, "This was already updating the display");
    }
}

int imagePlaylistLoad(void){
    fSysRet stat;
    u32 n;

    u8 *destBuff = takeDispFb(pdTICKS_TO_MS(100));
    if(destBuff == NULL){
        ESP_LOGE(TAG, "Unable to take ownership of frame buffer");
        return -1;
    }

    // todo: should fileSysGetAvailableImages be called here? if an image is added AFTER the mode is started, they are not
    //       taken into account

    // if in all mode, cycle through all images on the SD card
    switch(s.mode){
        case PLAYLIST_MODE_ALL:
            stat = fileSysLoadNextImageFromIdx(s.currIdx, destBuff);
            s.currIdx++;
            s.currIdx %= s.totalImg;
            break;
        case PLAYLIST_MODE_RANDOM:
            u32 newIdx;
            n = 20;        // just some upper limiter rather than while(1)
            // ensure the next random image is different from the current one
            while(n--){
                newIdx = esp_random() % s.totalImg;
                if(newIdx != s.currIdx) break;
            }
            s.currIdx = newIdx;
            stat = fileSysLoadNextImageFromIdx(s.currIdx, destBuff);
            break;
        case PLAYLIST_MODE_SELECT:
            n = MAX_PLAYLIST_IMG;
            while(n--){
                if(s.imgSelectEn[s.currIdx]){
                    break;
                }
                s.currIdx++;
                s.currIdx %= MAX_PLAYLIST_IMG;
            }
            if(n == 0){
                // this should never occur, there always should be the next image to load if wrapping around the entire list
                abort();
            }
            stat = fileSysLoadImage(s.imgSelect[s.currIdx], destBuff, false);
            s.currIdx++;
            s.currIdx %= MAX_PLAYLIST_IMG;
            break;
        default:
            abort();
            break;
    }

    releaseDispFb();

    if(stat != FILE_SYS_RET_OK){
        return -2;
    }

    return 0;
}

#undef s

/**
 * A task that reads telemetry from the power IC
 * Reads every 2sec
 */
void taskPmicTelemetry(void *args){
    for(EVER){
        xSemaphoreTake(pmicTelemetryMutex, portMAX_DELAY);
        pmicGetTelemetry(&pmicTelem);
        xSemaphoreGive(pmicTelemetryMutex);
        vTaskDelay(pdMS_TO_TICKS(PMIC_TELEMETRY_ACQ_DELAY));
    }
}

/**
 * A task that updates the display
 * This task only "runs" when the task is notified, otherwise halts
 *
 */
void taskDispUpdate(void *args){
    esp_pm_lock_handle_t sleepLockHandle;
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "disp", &sleepLockHandle));
    for(EVER){
        // clear flags before going back to waiting
        xTaskNotifyStateClear(NULL);
        xEventGroupSetBits(dispEvents, RTOS_DISP_EVENT_COMPLETE);
        // wait for us to signal to update the display in this task
        xTaskNotifyWait(ULONG_MAX, 0, NULL, portMAX_DELAY);
        xEventGroupClearBits(dispEvents, RTOS_DISP_EVENT_COMPLETE);

        esp_pm_lock_acquire(sleepLockHandle);
#ifdef DEBUG_DISABLE_DISPLAY_UPDATE
        ESP_LOGI(TAG, "Mock updating display");
#else
        pmicEnableLDOs();
        delayMs(100);            // some boot up time, todo: instrument
        dispBoot();
        dispUpdate();
        // debug
        // todo: is this the best place to put the device to deep sleep?
        if(runMode == MODE_IMAGE_PLAYLIST_LP){
            goDeepSleep();
            return;
        }
        pmicDisableLDOs();      // after we are done, shut down the display for power savings
#endif
        esp_pm_lock_release(sleepLockHandle);       // we-allow sleep mode
    }
}

// waits for the display to finish updating
void waitForDisplay(TickType_t timeout){
    xEventGroupWaitBits(dispEvents, RTOS_DISP_EVENT_COMPLETE, pdFALSE, pdTRUE, timeout);
}

// do not use in an infinite while loop, as it will hold up the task
int isDisplayUpdating(void) {
    return (xEventGroupGetBits(dispEvents) & RTOS_DISP_EVENT_COMPLETE) == 0;
}
