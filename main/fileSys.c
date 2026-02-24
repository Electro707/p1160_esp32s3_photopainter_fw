#include "ff.h"
#include "diskio_sdmmc.h"
#include "esp_log.h"

#include "common.h"
#include "main.h"
#include "fileSys.h"

static FATFS fs;     /* Pointer to the filesystem object */

static const char *TAG = "fileSys";

int initFs(void){
    ff_diskio_register_sdmmc(0, &sdCard);
    return 0;
}

int mountFs(void){
    FILINFO fno;
    FRESULT fsStat;

    fsStat = f_mount(&fs, "", 1);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to mount fatFS file system");
        return -1;
    }
    fsStat = f_stat(IMAGE_DIR, &fno);
    switch(fsStat){
        case FR_OK:
            break;
        case FR_NO_FILE:        // intentional no-break
        case FR_NO_PATH:
            ESP_LOGD(TAG, "image directory does not exist, making that directory\n");
            f_mkdir(IMAGE_DIR);
            break;
        default:
            ESP_LOGD(TAG, "An error occured. (%d)\n", fsStat);
            return -1;
    }

    return 0;
}

int deInitFs(void){
    FRESULT fsStat;
    fsStat = f_mount(NULL, "", 0);
    return fsStat;
}

int fileSysGetAvailableImages(cJSON *jsonArr){
    FRESULT fsStat;
    FF_DIR imageDir;
    FILINFO fno;

    cJSON *string_item;

    fsStat = f_opendir(&imageDir, IMAGE_DIR);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open image directory, even though one should have been made");
        return -1;
    }

    for (;;) {
        fsStat = f_readdir(&imageDir, &fno);
        if (fno.fname[0] == 0) break;               // break if no more files are fount
        if (fno.fattrib & AM_DIR) {
            // skip directories for now, a flag structure
            // todo: maybe allow for sub-structures
            continue;
        } else {
            string_item = cJSON_CreateString(fno.fname);
            cJSON_AddItemToArray(jsonArr, string_item);
        }
    }
    f_closedir(&imageDir);

    return 0;
}