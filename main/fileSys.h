#ifndef FS_H
#define FS_H

#include <cJSON.h>
#include "common.h"
#include "eink.h"
#include "ff.h"

#define IMAGE_DIR       "IMG"

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
 * The local image buffer. Right now only used to buffer what to write to the SD card
 */
extern u8 sdCardFrameBuff[DISP_FB_SIZE];

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

/**
 * Returns 0 if the image name given is a valid and available file
 */
fSysRet fileSysIsImageValid(const char *imgName);

fSysRet fileSysOpenImage(const char *imgName, FIL *file);

fSysRet fileSysLoadImage(const char* imgName, u8 *datOut, bool isNameDirect);

fSysRet fileSysLoadNextImageFromIdx(u32 *lastIdx, u8 *datOut);

/**
 * Saves the local image buffer (sdCardFrameBuff) to an image file
 */
fSysRet fileSysSaveImage(const char* imgName);

/**
 * Deletes an image file
 */
fSysRet fileSysDelImage(const char *imgName);

#endif