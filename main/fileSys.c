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
    snprintf(outName, maxLen, IMAGE_DIR "/%s.raw", imgName);
}

fSysRet initFs(void){
    ff_diskio_register_sdmmc(0, &sdCard);
    return FILE_SYS_RET_OK;
}

fSysRet mountFs(void){
    FILINFO fno;
    FRESULT fsStat;

    fsStat = f_mount(&fs, "", 1);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to mount fatFS file system");
        return FILE_SYS_UNABLE_MOUNT;
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
            return FILE_SYS_RET_FAIL;
    }

    return FILE_SYS_RET_OK;
}

int deInitFs(void){
    FRESULT fsStat;
    fsStat = f_mount(NULL, "", 0);
    return fsStat;
}

fSysRet fileSysGetAvailableImages(cJSON *jsonArr){
    FRESULT fsStat;
    FF_DIR imageDir;
    FILINFO fno;

    cJSON *string_item;

    fsStat = f_opendir(&imageDir, IMAGE_DIR);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open image directory, even though one should have been made");
        return FILE_SYS_NO_IMG_DIR;
    }

    for (;;) {
        fsStat = f_readdir(&imageDir, &fno);
        if (fno.fname[0] == 0) break;               // break if no more files are fount
        if (fno.fattrib & AM_DIR) {
            // skip directories for now, a flag structure
            // todo: maybe allow for sub-structures
            continue;
        } else {
            // todo: filter by .raw extension
            string_item = cJSON_CreateString(fno.fname);
            cJSON_AddItemToArray(jsonArr, string_item);
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
        ESP_LOGW(TAG, "Unable to open file for writing");
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

    if(isNameDirect){
        snprintf(imagePath, sizeof(imagePath), IMAGE_DIR "/%s", imgName);
    } else {
        ret = fileSysIsImageValid(imgName);
        if(ret){
            return ret;
        }
        getImagePath(imgName, imagePath, sizeof(imagePath));
    }
    
    ESP_LOGI(TAG, "Loading image %s", imagePath);
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

/**
 * dev notes: initially I tried keeping the DIR object intact and just use readdir per iteration, but looking more
 * into FatFS, FF_FS_REENTRANT is enabled, so the volume is locked unless closeDir is used.
 *  
 * Either I, head of time, read through all images and store their indexes, or per loop go through as many file as
 * I need to reach the end. I prefer the latter as with the former, I would not know during compile time how
 * many files strings I would need to allocate, and dynamic memory can get messy.
 */
fSysRet fileSysLoadNextImageFromIdx(u32 *lastIdx, u8 *datOut){
    FRESULT fsStat;
    FILINFO fno;
    fSysRet ret;
    FF_DIR imageDir;
    int fileCnt = -1;


    fsStat = f_opendir(&imageDir, IMAGE_DIR);
    if(fsStat != FR_OK){
        ESP_LOGW(TAG, "Unable to open image directory, even though one should have been made");
        return FILE_SYS_NO_IMG_DIR;
    }
    while(1){   // todo: timeout
        fsStat = f_readdir(&imageDir, &fno);
        if (fno.fname[0] == 0){
            if(fileCnt == -1) return FILE_SYS_NO_FILE_FOUND;       // no files in directory
            // if no more files are found, restart counter
            f_readdir(&imageDir, NULL);
            fileCnt = -1;
            *lastIdx = 0;
            continue;
        }
        if (fno.fattrib & AM_DIR) {
            // skip directories for now, a flag structure
            // todo: maybe allow for sub-structures
            continue;
        }
        fileCnt++;
        if(fileCnt == *lastIdx) break;
    }
    f_closedir(&imageDir);

    if(fileCnt < *lastIdx){
        return FILE_SYS_NO_FILE_FOUND;
    }
        
    ret = fileSysLoadImage((const char *)fno.fname, datOut, true);
    return ret;
}

// todo: fileSysLoadNextImageFromIdx reset

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