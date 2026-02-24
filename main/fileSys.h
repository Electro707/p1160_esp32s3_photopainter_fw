#ifndef FS_H
#define FS_H

#include <cJSON.h>

#define IMAGE_DIR       "img"

int initFs(void);
int mountFs(void);

int fileSysGetAvailableImages(cJSON *jsonArr);

#endif