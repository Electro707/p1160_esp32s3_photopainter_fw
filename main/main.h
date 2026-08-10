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
    MODE_IMAGE_PLAYLIST,
    MODE_IMAGE_PLAYLIST_LP,     // low power version of playlist, goes into deep sleep mode
}mode_e;

typedef enum{
    PLAYLIST_MODE_SELECT,        // cycle through pre-selected images
    PLAYLIST_MODE_ALL,           // cycle through all images on the SD card
    PLAYLIST_MODE_RANDOM,        // randomly get an image per cycle to display
}imgPlaylistMode_e;

typedef enum{
    RET_SET_MODE_OK = 0,
    RET_SET_MODE_IMG_PL_NONE_SET,
    RET_SET_MODE_IMG_NO_IMG,
    RET_SET_MODE_ERR,
}setModeRet_e;

typedef struct{
    imgPlaylistMode_e mode;
    TickType_t period_ticks;      // the duration of the cycle in rtos ticks. Must NOT be less than 12-15 seconds due to display refresh rate
    char imgSelect[MAX_PLAYLIST_IMG][MAX_IMAGE_NAME_LEN];
    u32 imgSelectEn[MAX_PLAYLIST_IMG];        // if an image in index X is used (1) or not (0)
                                                    // this allows to remove images at any index
    // internal variables
    u32 totalImg;       // total number of images available
    u32 currIdx;           // the current image index
    TimerHandle_t timerHandler;
}imgPlaylist_t;

extern spi_device_handle_t dispSpi;             // global spi device
extern i2c_master_bus_handle_t i2cHandle;       // global i2c handler
extern sdmmc_card_t sdCard;                     // global sdcard handler

extern pmicTelemetry pmicTelem;                        // global pmic telemetry struct
                                                // todo: move it so a dedicated function returns a pointer to it or None if busy
extern SemaphoreHandle_t pmicTelemetryMutex;           // mutex for pmic telemetry

// todo: lock this from threading, OR have dedicated functions to access it's variables
extern imgPlaylist_t imgPlaylist;
extern mode_e runMode;

/**
 * Gets the firmware operation mode
 */
setModeRet_e setMode(mode_e newMode);

/**
 * Triggers a display refresh
 *
 * Returns non-zero if the display update was already happening when we triggered it
 */
int dispTrigUpdate(void);

/**
 *
 */
int isDisplayUpdating(void);

void goDeepSleep(void);

#endif