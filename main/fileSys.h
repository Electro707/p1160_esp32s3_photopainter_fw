#ifndef FS_H
#define FS_H

#include <cJSON.h>
#include "common.h"

#define IMAGE_DIR       "img"

typedef enum{
    FILE_SYS_RET_OK = 0,            // all is good
    FILE_SYS_RET_FAIL,              // generic fail
    FILE_SYS_UNABLE_MOUNT,          // unable to mount the file system
    FILE_SYS_UNABLE_OPEN,           // unable to open a file
    FILE_SYS_UNABLE_WRITE,          // unable to write to a file
    FILE_SYS_UNABLE_READ,          // unable to read a file the amount of bytes we desired
    FILE_SYS_NO_FILE_FOUND,         // the desired file cannot be found on disk
    FILE_SYS_NO_IMG_DIR,            // the image directory is missing
    FILE_SYS_INVALID_FILE,          // the file to be loaded is invalid, for example an image file isn't of the right size
}fSysRet;

/**
 * Initializes the file system
 * 
 * Returns 0 on success, other values on failure
 */
fSysRet initFs(void);

/**
 * Mounts an SD card and sets up the directory structure
 * 
 * Returns 0 on success, other values on failure
 */
fSysRet mountFs(void);

/**
 * Finds all available images and fills a cJSON array object
 * 
 * Returns 0 on success, other values on failure
 */
fSysRet fileSysGetAvailableImages(cJSON *jsonArr);

fSysRet fileSysLoadImage(const char* imgName, u8 *datOut);
fSysRet fileSysSaveImage(const char* imgName, u8 *dat);
fSysRet fileSysDelImage(const char *imgName);

#endif