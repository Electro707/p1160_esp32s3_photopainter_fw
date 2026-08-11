#include "ff.h"
#include "diskio_sdmmc.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "common.h"
#include "main.h"
#include "fileSys.h"
#include "eink.h"

static FATFS fs;     /* Pointer to the filesystem object */

static const char *TAG = "fileSys";

WORD_ALIGNED_ATTR EXT_RAM_BSS_ATTR u8 sdCardFrameBuff[DISP_FB_SIZE];


static void getImagePath(const char *imgName, char *outName, u32 maxLen){
    snprintf(outName, maxLen, IMAGE_DIR "/%s.RAW", imgName);
}

static void getWebPath(const char *imgName, char *outName, u32 maxLen){
    snprintf(outName, maxLen, WEB_DIR "%s", imgName);
}

void initFs(void){
    ff_diskio_register_sdmmc(0, &sdCard);
}

fSysRet mountFs(void){
    FILINFO fno;
    FRESULT fsStat;

    // mounts the sd card fatfs
    fsStat = f_mount(&fs, "", 1);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to mount fatFS file system");
        return FILE_SYS_UNABLE_MOUNT;
    }

    // check if the image directory exists, and if it doesn't create it
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
            return FILE_SYS_RET_FAIL;
    }

    return FILE_SYS_RET_OK;
}

int deInitFs(void){
    FRESULT fsStat;
    fsStat = f_mount(NULL, "", 0);
    return fsStat;
}

fSysRet fileSysGetIfWebAsset(const char *fileName){
    FRESULT fsStat;
    FF_DIR httpDir;
    FILINFO fno;
    fSysRet stat;

    fsStat = f_opendir(&httpDir, WEB_DIR);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open image directory, even though one should have been made");
        return FILE_SYS_INVALID_DIR;
    }

    stat = FILE_SYS_INVALID_FILE;
    for (;;) {
        fsStat = f_readdir(&httpDir, &fno);
        if (fno.fname[0] == 0) break;               // break if no more files are fount
        if (fno.fattrib & AM_DIR) {
            // skip directories for now, a flag structure
            // todo: maybe allow recursive HTTP...not needed though....
            continue;
        } else {
            ESP_LOGI(TAG, "%s", fno.fname);
            if(strcmp(fileName, fno.fname) == 0){
                stat = FILE_SYS_RET_OK;
                break;
            }
        }
    }
    f_closedir(&httpDir);

    return stat;
}

fSysRet fileSysOpenWebAsset(const char *fileName, FIL *file){
    FRESULT fsStat;
    FF_DIR httpDir;
    FILINFO fno;
    char webFullPath[128];

    getWebPath(fileName, webFullPath, sizeof(webFullPath));
    fsStat = f_open(file, webFullPath, FA_READ);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open file '%s' for reading - %d", webFullPath, fsStat);
        return FILE_SYS_UNABLE_OPEN;
    }

    return FILE_SYS_RET_OK;
}

fSysRet fileSysGetAvailableImages(cJSON *jsonArr, u32 *count){
    FRESULT fsStat;
    FF_DIR imageDir;
    FILINFO fno;

    cJSON *string_item;
    char *dotIdx;

    fsStat = f_opendir(&imageDir, IMAGE_DIR);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open image directory, even though one should have been made");
        return FILE_SYS_INVALID_DIR;
    }

    for (;;) {
        fsStat = f_readdir(&imageDir, &fno);
        if (fno.fname[0] == 0) break;               // break if no more files are fount
        if (fno.fattrib & AM_DIR) {
            // skip directories for now, a flag structure
            // todo: maybe allow for sub-structures
            continue;
        } else {
            // filter by .raw extension
            dotIdx = strrchr(fno.fname, '.');
            if(dotIdx == NULL){     // don't allow extension-less names
                continue;
            }
            if(strcmp(dotIdx, ".RAW") != 0){
                continue;
            }
            if(jsonArr){
                *dotIdx = '\0';      // effectively remove extension from listed images
                string_item = cJSON_CreateString(fno.fname);
                cJSON_AddItemToArray(jsonArr, string_item);
            }
            if(count){
                (*count)++;
            }
        }
    }
    f_closedir(&imageDir);

    return FILE_SYS_RET_OK;
}

fSysRet fileSysIsImageValid(const char *imgName){
    FILINFO fno;
    FRESULT fsStat;
    char imagePath[128];

    getImagePath(imgName, imagePath, sizeof(imagePath));

    fsStat = f_stat(imagePath, &fno);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "File does not exist, exiting");
        return FILE_SYS_NO_FILE_FOUND;
    }
    if(fno.fsize != DISP_FB_SIZE){
        ESP_LOGW(TAG, "File size is not that of a frame buffer, exiting!");
        return FILE_SYS_INVALID_FILE;
    }

    return FILE_SYS_RET_OK;
}

fSysRet fileSysOpenImage(const char *imgName, FIL *file){
    FRESULT fsStat;
    fSysRet stat;
    char imagePath[128];

    stat = fileSysIsImageValid(imgName);
    if(stat) return stat;

    // yes the last function already got the name...too bad!
    getImagePath(imgName, imagePath, sizeof(imagePath));
    fsStat = f_open(file, imagePath, FA_READ);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open file for reading");
        return FILE_SYS_UNABLE_OPEN;
    }

    return FILE_SYS_RET_OK;
}

fSysRet fileSysLoadImage(const char* imgName, u8 *datOut, bool isNameDirect){
    FRESULT fsStat;
    FIL file;
    char imagePath[128];
    UINT nRead;
    fSysRet ret = FILE_SYS_RET_OK;

    ESP_LOGI(TAG, "Loading image %s", imgName);

    if(isNameDirect){
        snprintf(imagePath, sizeof(imagePath), IMAGE_DIR "/%s", imgName);
    } else {
        ret = fileSysIsImageValid(imgName);
        if(ret){
            return ret;
        }
        getImagePath(imgName, imagePath, sizeof(imagePath));
    }

    fsStat = f_open(&file, imagePath, FA_READ);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open file for writing");
        return FILE_SYS_UNABLE_OPEN;
    }
    fsStat = f_read(&file, datOut, DISP_FB_SIZE, &nRead);
    if(nRead != DISP_FB_SIZE){
        ESP_LOGW(TAG, "Unable to read the whole file for some reason?");
        ret = FILE_SYS_UNABLE_READ;
    }

    f_close(&file);
    return ret;
}

fSysRet fileSysLoadNextImageFromIdx(u32 imgIdx, u8 *datOut){
    FRESULT fsStat;
    FILINFO fno;
    fSysRet ret;
    FF_DIR imageDir;
    int fileCnt = 0;

    fsStat = f_opendir(&imageDir, IMAGE_DIR);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open image directory, even though one should have been made");
        return FILE_SYS_INVALID_DIR;
    }
    while(1){   // todo: timeout
        fsStat = f_readdir(&imageDir, &fno);
        if (fno.fname[0] == 0){         // no file around
            break;
        }
        if (fno.fattrib & AM_DIR) {
            // skip directories for now, a flag structure
            // todo: maybe allow for sub-structures
            continue;
        }
        if(fileCnt == imgIdx) break;
        fileCnt++;
    }
    f_closedir(&imageDir);

    if(fileCnt < imgIdx){
        return FILE_SYS_NO_FILE_FOUND;
    }

    ret = fileSysLoadImage((const char *)fno.fname, datOut, true);
    return ret;
}


fSysRet fileSysSaveImage(const char* imgName){
    FRESULT fsStat;
    FIL file;
    char imagePath[128];
    UINT nWritten;

    getImagePath(imgName, imagePath, sizeof(imagePath));

    ESP_LOGI(TAG, "Started write to file %s", imagePath);

    fsStat = f_open(&file, imagePath, FA_CREATE_ALWAYS | FA_WRITE);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open file for writing");
        return FILE_SYS_UNABLE_OPEN;
    }

    fsStat = f_write(&file, sdCardFrameBuff, DISP_FB_SIZE, &nWritten);
    if(nWritten != DISP_FB_SIZE){
        ESP_LOGW(TAG, "Did it completely write the file");
        return FILE_SYS_UNABLE_WRITE;
    }

    ESP_LOGI(TAG, "Done with write operation");
    f_close(&file);
    return FILE_SYS_RET_OK;
}


fSysRet fileSysDelImage(const char *imgName){
    FRESULT fsStat;
    char imagePath[128];
    FILINFO fno;
    fSysRet ret;

    ret = fileSysIsImageValid(imgName);
    if(ret){
        return ret;
    }
    getImagePath(imgName, imagePath, sizeof(imagePath));

    fsStat = f_unlink(imagePath);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to delete image file");
        return FILE_SYS_RET_FAIL;
    }
    return FILE_SYS_RET_OK;
}