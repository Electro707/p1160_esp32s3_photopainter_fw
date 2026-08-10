
#include <string.h>
#include <ctype.h>
#ifndef UNIT_TEST
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "esp_log.h"
#include "network.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "mdns.h"
#include "ff.h"
#else
#include "mock.h"
#endif

#include <cJSON.h>

#include "network.h"
#include "fileSys.h"
#include "common.h"
#include "eink.h"
#include "main.h"

/* FreeRTOS event group to signal when we are connected*/
#ifndef UNIT_TEST
EventGroupHandle_t wifiEvents;
wifi_config_t wifiConfig;               // the esp internal wifi configuration
static nvs_handle_t wifiNvsHandle;      // the handle for nvm

httpd_handle_t server = NULL;
#endif

static const char *TAG = "wifi";
static const char *NVS_ID = "wifi";

struct{
    char staSsid[MAX_WIFI_INFO_STRLEN];
    char staPass[MAX_WIFI_INFO_STRLEN];
    wifi_auth_mode_t authMode;
}wifiNvmConf;                           // a local configuration for wifi that is saved/loaded from nvm

void wifiStartAP(void);
void wifiStartSTA(void *arg);
void saveWifiNvmConf(void);

void toUpperChar(char *s){
    while(*s){
        *s = toupper(*s);
        s++;
    }
}

/**
 * Internal helper function that receives a JSON content from the request, and parses
 *  it to a cJSON object.
 * If this function returns anything but ESP_OK, it also put out the error http response, so the caller
 * doesn't have to do anything beyond cleanup and exist
 */
esp_err_t getJsonFromReq(httpd_req_t *req, char **contextBuff, cJSON **jRoot){
    char responseBuff[128];
    esp_err_t ret = ESP_OK;

    int remaining = req->content_len;   // total bytes expected
    *contextBuff = malloc(req->content_len);
    int r = httpd_req_recv(req, (char*)*contextBuff, remaining);
    if(r < 0){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"Error while receiving info\"}");
        ret = ESP_FAIL;
    }
    else{
        *jRoot = cJSON_Parse(*contextBuff);

        if(*jRoot == NULL){
            const char *error_ptr = cJSON_GetErrorPtr();
            if(error_ptr != NULL){
                snprintf(responseBuff, 128, "{\"stat\": \"JSON invalid: %s\"}", error_ptr);
            } else {
                snprintf(responseBuff, 128, "{\"stat\": \"JSON invalid: unknown\"}");
            }
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, responseBuff);
            ret = ESP_FAIL;
        }
    }

    return ret;
}

/********** URI match handlers **********/
static esp_err_t handleUriGetVersion(httpd_req_t *req){
    char tmp[128];
    httpd_resp_set_type(req, "application/json");
    snprintf(tmp, 128, "{\"stat\": \"ok\", \"version\": \"%s Rev %s\"}", FW_NAME, FW_REV);
    httpd_resp_sendstr(req, tmp);
    return ESP_OK;
}

static esp_err_t handleUriGetCoffee(httpd_req_t *req){
    httpd_resp_set_status(req, "418 I'm a teapot");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"stat\": \"I'm a teapot\"}");
    return ESP_OK;
}

static esp_err_t handleUriGetStatus(httpd_req_t *req){
    esp_err_t ret;
    cJSON *jRoot;
    char *jsonPrint;

    httpd_resp_set_type(req, "application/json");
    jRoot = cJSON_CreateObject();

    cJSON_AddStringToObject(jRoot, "stat", "ok");
    cJSON_AddBoolToObject(jRoot, "dispBusy", isDisplayUpdating());

    jsonPrint = cJSON_PrintUnformatted(jRoot);
    if(jsonPrint == NULL){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"CJSON Fail\"}");
        ret = ESP_FAIL;
    } else {
        httpd_resp_sendstr(req, jsonPrint);
        ret = ESP_OK;
    }

    cJSON_Delete(jRoot);
    return ret;
}

static esp_err_t addVoltageToJson(cJSON *jRoot, const char *key, float val){
    char numb[16];
    snprintf(numb, 16, "%.2f", val);
    cJSON_AddStringToObject(jRoot, key, numb);
    return ESP_OK;
}

static esp_err_t handleUriGetPmicInfo(httpd_req_t *req){
    esp_err_t ret;
    cJSON *jRoot;
    char *jsonPrint;
    const char *strToFill;

    httpd_resp_set_type(req, "application/json");
    jRoot = cJSON_CreateObject();

    if(xSemaphoreTake(pmicTelemetryMutex, pdMS_TO_TICKS(500)) != pdTRUE){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Could not take semephore for pmic struct\"}");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(jRoot, "stat", "ok");
    cJSON_AddNumberToObject(jRoot, "battVolt", pmicTelem.battVolt);
    cJSON_AddNumberToObject(jRoot, "sysVolt", pmicTelem.sysVolt);
    cJSON_AddNumberToObject(jRoot, "vBusVolt", pmicTelem.vBusVolt);
    cJSON_AddNumberToObject(jRoot, "battPercentage", pmicTelem.battPercentage);
    cJSON_AddBoolToObject(jRoot, "vBusGood", pmicTelem.vBusGood);
    cJSON_AddBoolToObject(jRoot, "battPresent", pmicTelem.battPresent);
    cJSON_AddBoolToObject(jRoot, "currLimited", pmicTelem.currLimited);

    switch(pmicTelem.chargeDir){
        case PMIC_CHR_DIR_STANDBY:
            strToFill = "Standby";
            break;
        case PMIC_CHR_DIR_CHARGE:
            strToFill = "Charge";
            break;
        case PMIC_CHR_DIR_DISCHARGE:
            strToFill = "Discharge";
            break;
        default:
            strToFill = "Error";
            break;
    }
    cJSON_AddStringToObject(jRoot, "chargeDir", strToFill);

    switch(pmicTelem.chargeStat){
        case PMIC_CHR_STAT_TRI:
            strToFill = "Tri-State";
            break;
        case PMIC_CHR_STAT_PRE:
            strToFill = "Pre-Charge";
            break;
        case PMIC_CHR_STAT_CC:
            strToFill = "Constant Current";
            break;
        case PMIC_CHR_STAT_CV:
            strToFill = "Constant Voltage";
            break;
        case PMIC_CHR_STAT_DONE:
            strToFill = "Done";
            break;
        case PMIC_CHR_STAT_NO_CHARGE:
            strToFill = "Not Charging";
            break;
        default:
            strToFill = "Error";
            break;
    }
    cJSON_AddStringToObject(jRoot, "chargeState", strToFill);

    xSemaphoreGive(pmicTelemetryMutex);

    jsonPrint = cJSON_PrintUnformatted(jRoot);
    if(jsonPrint == NULL){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"CJSON Fail\"}");
        ret = ESP_FAIL;
    } else {
        httpd_resp_sendstr(req, jsonPrint);
        ret = ESP_OK;
    }

    cJSON_Delete(jRoot);
    return ret;
}

static esp_err_t handleUriGetImgAvailable(httpd_req_t *req){
    esp_err_t ret;
    cJSON *jRoot;
    char *jsonPrint;

    httpd_resp_set_type(req, "application/json");
    jRoot = cJSON_CreateObject();
    cJSON_AddStringToObject(jRoot, "stat", "ok");
    cJSON * jArr = cJSON_AddArrayToObject(jRoot, "img");

    ret = fileSysGetAvailableImages(jArr, NULL);
    if(ret){
        ESP_LOGW(TAG, "Unable to get images in directory");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Directory Fail\"}");
        ret = ESP_FAIL;
    } else {
        jsonPrint = cJSON_PrintUnformatted(jRoot);
        if(jsonPrint == NULL){
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"CJSON Fail\"}");
            ret = ESP_FAIL;
        } else {
            httpd_resp_sendstr(req, jsonPrint);
            ret = ESP_OK;
        }
    }

    cJSON_Delete(jRoot);
    return ret;
}

static esp_err_t handleUriImgGet(httpd_req_t *req){
    esp_err_t ret;
    esp_err_t espStat;
    fSysRet fSysStat;
    char *urlQuery;
    char imgName[32+1];
    int urlQueryLen;

    httpd_resp_set_type(req, "application/json");

    urlQueryLen = httpd_req_get_url_query_len(req);
    urlQueryLen += 1;
    urlQuery = malloc(urlQueryLen);
    espStat = httpd_req_get_url_query_str(req, urlQuery, urlQueryLen);
    if(espStat){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"bad url\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    espStat = httpd_query_key_value(urlQuery, "name", imgName, 32);
    if(espStat){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"name was not valid\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    fSysStat = fileSysLoadImage(imgName, sdCardFrameBuff, false);
    if(fSysStat != FILE_SYS_RET_OK){
        if(fSysStat == FILE_SYS_NO_FILE_FOUND){
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"image does not exist\"}");
        }
        else if(fSysStat == FILE_SYS_INVALID_FILE){
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"invalid image file\"}");
        }
        else{
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"error reading file\"}");
        }
        ret = ESP_FAIL;
        goto cleanup;
    }


    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send(req, (char *)sdCardFrameBuff, DISP_FB_SIZE);
    ret = ESP_OK;

cleanup:
    free(urlQuery);
    return ret;
}


static esp_err_t handleUriGetPlaylistImages(httpd_req_t *req){
    esp_err_t ret;
    cJSON *jRoot;
    char *jsonPrint;
    cJSON *string_item;

    httpd_resp_set_type(req, "application/json");
    jRoot = cJSON_CreateObject();
    cJSON_AddStringToObject(jRoot, "stat", "ok");
    cJSON * jArr = cJSON_AddArrayToObject(jRoot, "img");
    for(int i=0;i<MAX_PLAYLIST_IMG;i++){
        if(imgPlaylist.imgSelectEn[i]){
            string_item = cJSON_CreateString(imgPlaylist.imgSelect[i]);
            cJSON_AddItemToArray(jArr, string_item);
        }
    }

    jsonPrint = cJSON_PrintUnformatted(jRoot);
    if(jsonPrint == NULL){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"CJSON Fail\"}");
        ret = ESP_FAIL;
    } else {
        httpd_resp_sendstr(req, jsonPrint);
        ret = ESP_OK;
    }

    cJSON_Delete(jRoot);
    return ret;
}

static esp_err_t handleUriGetWifiInfo(httpd_req_t *req){
    wifi_mode_t wifiM;
    esp_err_t ret;
    char wifiModeStr[32];
    char *jsonPrint;

    esp_wifi_get_mode(&wifiM);
    switch(wifiM){
        case WIFI_MODE_NULL: strcpy(wifiModeStr, "null"); break;
        case WIFI_MODE_STA: strcpy(wifiModeStr, "sta"); break;
        case WIFI_MODE_AP: strcpy(wifiModeStr, "ap"); break;
        case WIFI_MODE_APSTA: strcpy(wifiModeStr, "apsta"); break;
        case WIFI_MODE_NAN: strcpy(wifiModeStr, "nan"); break;
        default: strcpy(wifiModeStr, "error"); break;
    }

    cJSON *jRoot = cJSON_CreateObject();
    cJSON_AddStringToObject(jRoot, "stat", "ok");
    cJSON_AddStringToObject(jRoot, "currentMode", wifiModeStr);
    cJSON_AddStringToObject(jRoot, "staSSID", wifiNvmConf.staSsid);
    cJSON_AddStringToObject(jRoot, "staPass", wifiNvmConf.staPass);

    httpd_resp_set_type(req, "application/json");
    jsonPrint = cJSON_PrintUnformatted(jRoot);
    if(jsonPrint == NULL){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"CJSON Fail\"}");
        ret = ESP_FAIL;
    } else {
        httpd_resp_sendstr(req, jsonPrint);
        ret = ESP_OK;
    }

    cJSON_Delete(jRoot);
    return ret;
}

static esp_err_t handleUriPostWifiSta(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;
    const cJSON *jSSID = NULL;
    const cJSON *jPass = NULL;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }


    jSSID = cJSON_GetObjectItem(jRoot, "ssid");
    if (!cJSON_IsString(jSSID) || (jSSID->valuestring == NULL)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: ssid not a string\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    jPass = cJSON_GetObjectItem(jRoot, "pass");
    if (!cJSON_IsString(jPass) || (jPass->valuestring == NULL)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: password not a string\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    strcpy(wifiNvmConf.staSsid, jSSID->valuestring);
    strcpy(wifiNvmConf.staPass, jPass->valuestring);
    // todo: for now just allow psk, allow more auth options
    if(strlen(wifiNvmConf.staPass) == 0){
        wifiNvmConf.authMode = WIFI_AUTH_OPEN;
    } else {
        wifiNvmConf.authMode = WIFI_AUTH_WPA2_PSK;
    }

    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    ESP_LOGD(TAG, "Got info for STA mode, saving to nvm");
    saveWifiNvmConf();
    ret = ESP_OK;

cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;
}

static esp_err_t handleUriPostWifiStaConn(httpd_req_t *req){
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    httpd_queue_work(req->handle, wifiStartSTA, NULL);
    return ESP_OK;

}


static esp_err_t handleUriPostSetFbCommon(httpd_req_t *req, u32 dest){
    u8 *destBuff;

    httpd_resp_set_type(req, "application/json");

    if(req->content_len != DISP_FB_SIZE){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"Frame buffer size invalid\"}");
        return ESP_FAIL;
    }

    if(dest == 0x00){
        destBuff = takeDispFb(0);
        if(destBuff == NULL){
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Could not take frame buffer mutex\"}");
            return ESP_FAIL;
        }
    }
    else if(dest == 0x01){
        destBuff = sdCardFrameBuff;
    }
    else{
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"internal error, invalid dest\"}");
        return ESP_FAIL;
    }

    const size_t chunkReadSize = 16384;

    int remaining = req->content_len;   // total bytes expected
    while(1){
        int to_read = remaining < chunkReadSize ? remaining : chunkReadSize;

        int r = httpd_req_recv(req, (char*)destBuff, to_read);
        if (r > 0) {
            destBuff += r;
            remaining -= r;
        }
        // todo: handle other errors
        else if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            // client is slow; retry
        }
        else{
            break;
        }
    }

    if(dest == 0x00){
        releaseDispFb();
    }
    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");

    return ESP_OK;
}


/**
 * Set the internal display framebuffer
 */
static esp_err_t handleUriPostSetDisplayFb(httpd_req_t *req){
    return handleUriPostSetFbCommon(req, 0x00);
}

/**
 * Set the internal display framebuffer
 */
static esp_err_t handleUriPostUploadSdImage(httpd_req_t *req){
    return handleUriPostSetFbCommon(req, 0x01);
}

static esp_err_t handleUriPostUpdateDisplay(httpd_req_t *req){
    httpd_resp_set_type(req, "application/json");
    u32 stat = dispTrigUpdate();
    if(stat){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"updating display, please wait\"}");
        return ESP_FAIL;
    }
    else{
        httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
        return ESP_OK;
    }
}

static esp_err_t handleUriPostImageCheckerPattern(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }

    const cJSON *jCheckSize = cJSON_GetObjectItem(jRoot, "checkSize");
    if (!cJSON_IsNumber(jCheckSize)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: checkSize not an integer\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    dispCheckerPattern(jCheckSize->valueint);
    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    ret = ESP_OK;

cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;
}

static esp_err_t handleUriSaveImage(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }

    const cJSON *jImgName = cJSON_GetObjectItem(jRoot, "name");
    if (!cJSON_IsString(jImgName)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: imgName not a string\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    fSysRet fsRet = fileSysSaveImage(jImgName->valuestring);
    if(fsRet == FILE_SYS_RET_OK){
        httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
        ret = ESP_OK;
    }
    else{
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"unable to save frame buffer to file\"}");
        ret = ESP_FAIL;
    }

cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;
}


static esp_err_t handleUriLoadImage(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }

    const cJSON *jImgName = cJSON_GetObjectItem(jRoot, "name");
    if (!cJSON_IsString(jImgName)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: imgName not a string\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    u8 *destBuff = takeDispFb(0);
    if(destBuff == NULL){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Could not take frame buffer mutex\"}");
        return ESP_FAIL;
    }
    fSysRet stat = fileSysLoadImage(jImgName->valuestring, destBuff, false);
    releaseDispFb();
    if(stat == FILE_SYS_RET_OK){
        httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
        ret = ESP_OK;
    }
    else{
        if(stat == FILE_SYS_NO_FILE_FOUND){
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"image does not exist\"}");
        }
        else if(stat == FILE_SYS_INVALID_FILE){
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"invalid image file\"}");
        }
        else{
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"error reading file\"}");
        }
        ret = ESP_FAIL;
    }

cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;
}

static esp_err_t handleUriDeleteImage(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }

    const cJSON *jImgName = cJSON_GetObjectItem(jRoot, "name");
    if (!cJSON_IsString(jImgName)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: name not a string\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    fSysRet stat = fileSysDelImage(jImgName->valuestring);
    if(stat == FILE_SYS_RET_OK){
        httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
        ret = ESP_OK;
    }
    else{
        if(stat == FILE_SYS_NO_FILE_FOUND){
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"image does not exist\"}");
        }
        else if(stat == FILE_SYS_INVALID_FILE){
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"invalid image file\"}");
        }
        else{
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"error deleting file\"}");
        }
        ret = ESP_FAIL;
    }

cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;

}

static esp_err_t handleUriPlaylistAdd(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }

    const cJSON *jImgName = cJSON_GetObjectItem(jRoot, "name");
    if (!cJSON_IsString(jImgName)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: name not a string\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }
    if(fileSysIsImageValid(jImgName->valuestring)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"invalid file, not on disk\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // first, check if it exists, then if not actually add
    int freeSpot = -1;    // as we are already going through the list anyways, if an empty spot is added record it
    for(int i=0;i<MAX_PLAYLIST_IMG;i++){
        if(imgPlaylist.imgSelectEn[i]){
            // if we find the same image, we already added it. Don't add it again
            if(strcmp(imgPlaylist.imgSelect[i], jImgName->valuestring) == 0){
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"image already exists in playlist\"}");
                ret = ESP_FAIL;
                goto cleanup;
            }
        } else if(freeSpot == -1){
            freeSpot = i;
        }
    }

    if(freeSpot == -1){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"No more room to add image\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }
    // then add it
    strcpy(imgPlaylist.imgSelect[freeSpot], jImgName->valuestring);
    imgPlaylist.imgSelectEn[freeSpot] = 1;

    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    ret = ESP_OK;


cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;
}


static esp_err_t handleUriPlaylistDel(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }

    const cJSON *jImgName = cJSON_GetObjectItem(jRoot, "name");
    if (!cJSON_IsString(jImgName)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: name not a string\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // add check if we are currently in the playlist selected mode, don't allow deletion as it remove the last
    //  available image, causing cascading of issues
    if(runMode == MODE_IMAGE_PLAYLIST && imgPlaylist.mode == PLAYLIST_MODE_SELECT){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"current running in mode, stop image cycling to remove\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    int i;
    for(i=0;i<MAX_PLAYLIST_IMG;i++){
        if(imgPlaylist.imgSelectEn[i]){
            if(strcmp(imgPlaylist.imgSelect[i], jImgName->valuestring) == 0){
                imgPlaylist.imgSelectEn[i] = 0;
                break;
            }
        }
    }
    if(i == MAX_PLAYLIST_IMG){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Image not found to delete\"}");
        ret = ESP_FAIL;
        goto cleanup;
    }

    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    ret = ESP_OK;


cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;
}


static esp_err_t handleUriGetMode(httpd_req_t *req){
    esp_err_t ret;
    cJSON *jRoot;
    char *jsonPrint;
    const char *strToFill;

    httpd_resp_set_type(req, "application/json");
    jRoot = cJSON_CreateObject();

    cJSON_AddStringToObject(jRoot, "stat", "ok");
    switch(runMode){
        case MODE_STANDBY:
            strToFill = "standby";
            break;
        case MODE_IMAGE_PLAYLIST:
            strToFill = "playlist";
            break;
        case MODE_IMAGE_PLAYLIST_LP:
            strToFill = "playlistLP";
            break;
        default:
            strToFill = "error";
            break;
    }
    cJSON_AddStringToObject(jRoot, "mode", strToFill);

    cJSON * const jPlaylist = cJSON_AddObjectToObject(jRoot, "playlist");
    cJSON_AddStringToObject(jRoot, "stat", "ok");
    switch(imgPlaylist.mode){
        case PLAYLIST_MODE_SELECT:
            strToFill = "select";
            break;
        case PLAYLIST_MODE_ALL:
            strToFill = "all";
            break;
        case PLAYLIST_MODE_RANDOM:
            strToFill = "random";
            break;
        default:
            strToFill = "Error";
            break;
    }
    cJSON_AddStringToObject(jPlaylist, "mode", strToFill);
    cJSON_AddNumberToObject(jPlaylist, "duration", (((float)imgPlaylist.period_ticks) / ((float)configTICK_RATE_HZ) / 60.0));


    jsonPrint = cJSON_PrintUnformatted(jRoot);
    if(jsonPrint == NULL){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"CJSON Fail\"}");
        ret = ESP_FAIL;
    } else {
        httpd_resp_sendstr(req, jsonPrint);
        ret = ESP_OK;
    }

    cJSON_Delete(jRoot);
    return ret;
}

static esp_err_t handleUriSetOperationMode(httpd_req_t *req){
    esp_err_t ret;
    char *contextBuff;
    cJSON *jRoot = NULL;

    const cJSON *jObj;

    httpd_resp_set_type(req, "application/json");

    if(getJsonFromReq(req, &contextBuff, &jRoot)){
        ret = ESP_FAIL;
        goto cleanup;
    }

    // handle if the playlist config are to be changed
    jObj = cJSON_GetObjectItem(jRoot, "playlist");
    if (cJSON_IsObject(jObj)){
        const cJSON *jPlaylist;
        jPlaylist = cJSON_GetObjectItem(jObj, "mode");
        if (cJSON_IsString(jPlaylist)){
            if(strcmp(jPlaylist->valuestring, "select") == 0){
                imgPlaylist.mode = PLAYLIST_MODE_SELECT;
            }
            else if(strcmp(jPlaylist->valuestring, "all") == 0){
                imgPlaylist.mode = PLAYLIST_MODE_ALL;
            }
            else if(strcmp(jPlaylist->valuestring, "random") == 0){
                imgPlaylist.mode = PLAYLIST_MODE_RANDOM;
            }
            else{
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"invalid playlist mode\"}");
                ret = ESP_FAIL;
                goto cleanup;
            }
        }

        jPlaylist = cJSON_GetObjectItem(jObj, "duration");
        if (cJSON_IsNumber(jPlaylist)){
            double timeSet = jPlaylist->valuedouble;
            if(timeSet < MIN_PLAYLIST_DUR){
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"playlist duration less than minimum of 30 sec\"}");
                ret = ESP_FAIL;
                goto cleanup;
            }
            timeSet *= 60;      // to seconds from minutes
            timeSet *= configTICK_RATE_HZ;      // to the tick rate
            imgPlaylist.period_ticks = (TickType_t)timeSet;
        }
    }

    // handle the mode setting last
    jObj = cJSON_GetObjectItem(jRoot, "mode");
    // if "mode" was given
    if (cJSON_IsString(jObj)){
        if(strcmp(jObj->valuestring, "standby") == 0){
            setModeRet_e stat = setMode(MODE_STANDBY);
            if(stat){
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"internal error\"}");
                ret = ESP_FAIL;
                goto cleanup;
            }
        }
        else if(strcmp(jObj->valuestring, "playlist") == 0){
            setModeRet_e stat = setMode(MODE_IMAGE_PLAYLIST);
            if(stat){
                if(stat == RET_SET_MODE_IMG_PL_NONE_SET){
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"no images selected for playlist mode\"}");
                }
                else{
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"internal error\"}");
                }
                ret = ESP_FAIL;
                goto cleanup;
            }
        }
        else if(strcmp(jObj->valuestring, "playlistLP") == 0){
            setModeRet_e stat = setMode(MODE_IMAGE_PLAYLIST_LP);
            if(stat){
                if(stat == RET_SET_MODE_IMG_PL_NONE_SET){
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"no images selected for playlist mode\"}");
                }
                else{
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"internal error\"}");
                }
                ret = ESP_FAIL;
                goto cleanup;
            }
        }
        else{
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"invalid mode\"}");
            ret = ESP_FAIL;
            goto cleanup;
        }
    }


    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    ret = ESP_OK;

cleanup:
    cJSON_Delete(jRoot);
    free(contextBuff);
    return ret;

}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

// copied from example file_server.c
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "text/javascript");
    } else {
        /* This is a limited set only */
        /* For any other type always set as plain text */
        return httpd_resp_set_type(req, "text/plain");
    }
}


static esp_err_t handleUriWebGet(httpd_req_t *req){
    char filepath[128];
    FIL file;
    esp_err_t espStat;
    esp_err_t ret;

    httpd_resp_set_type(req, "text/plain");

    strcpy(filepath, req->uri);
    // if we exactly get /, then rename to index
    if(strcmp(filepath, "/") == 0){
        strcpy(filepath, "/index.html");
    }
    // upper case everything as FatFS has it upper case
    ESP_LOGI(TAG, "Requested file for http '%s'", filepath);

    // +1 added to ignore initial slash
    if(fileSysGetIfWebAsset(filepath+1) != FILE_SYS_RET_OK){
        // return 404, not found
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "404 not found");
        return ESP_FAIL;
    }

    // get that file and sent it up to the user
    if(fileSysOpenWebAsset(filepath, &file) != FILE_SYS_RET_OK){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "500 Unable to load file");
        ret = ESP_FAIL;
        goto cleanup;
    }

    uint8_t datOut[8192];
    size_t nRead;
    FRESULT fsStat;
    set_content_type_from_file(req, filepath);
    do{
        fsStat = f_read(&file, datOut, 8192, &nRead);
        if(fsStat != FR_OK){
            httpd_resp_send_chunk(req, NULL, 0);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error in fatfs when reading");
            ret = ESP_FAIL;
            goto cleanup;
            break;
        }
        if(nRead){
            espStat = httpd_resp_send_chunk(req, (char *)datOut, nRead);
            if(espStat){
                ESP_LOGW(TAG, "ERROR 0x%x", espStat);
                httpd_resp_send_chunk(req, NULL, 0);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to send file, error in send_chunk");
                ret = ESP_FAIL;
                goto cleanup;
            }
        }
    }while(nRead);

    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send_chunk(req, NULL, 0);
    ret = ESP_OK;

cleanup:
    f_close(&file);
    return ret;
}

static esp_err_t handle404NotFound(httpd_req_t *req, httpd_err_code_t error){
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_err(req, error, "{\"stat\": \"Not Found\"}");
    return ESP_OK;
}

/********** wifi events **********/
#ifndef UNIT_TEST
static void wifiIpEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int s_retry_num = 0;

    if(event_base == WIFI_EVENT){
        switch(event_id){
            /**** STA Stuff */
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "connected to the AP success");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG,"connect to the AP fail");
                if(s_retry_num < CONFIG_ESP_MAXIMUM_RETRY){
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "retry to connect to the AP");
                }
                else{
                    xEventGroupSetBits(wifiEvents, WIFI_FAIL_BIT);
                    ESP_LOGI(TAG, "Failed to connect over STA, starting AP Mode");
                    wifiStartAP();
                }
                break;
            /**** AP Stuff */
            case WIFI_EVENT_AP_STACONNECTED:
                wifi_event_ap_staconnected_t* eventConn = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(eventConn->mac), eventConn->aid);
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                wifi_event_ap_stadisconnected_t* eventDisconn = (wifi_event_ap_stadisconnected_t*) event_data;
                ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d", MAC2STR(eventDisconn->mac), eventDisconn->aid, eventDisconn->reason);
                break;
            default:
                break;
        }
    }
    else if(event_base == IP_EVENT){
        switch(event_id){
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
                s_retry_num = 0;
                xEventGroupSetBits(wifiEvents, WIFI_CONNECTED_BIT);
                break;
            default:
                break;
        }
    }
}

void wifiStartAP(void){
    ESP_ERROR_CHECK(esp_wifi_stop());

    strcpy((char *)wifiConfig.ap.ssid, CONFIG_ESP_AP_WIFI_SSID);
    strcpy((char *)wifiConfig.ap.password, CONFIG_ESP_AP_WIFI_PASSWORD);
    wifiConfig.ap.channel = CONFIG_ESP_AP_WIFI_CH;
    wifiConfig.ap.max_connection = CONFIG_ESP_AP_WIFI_MAX_CONN;
    if(strlen((char *)wifiConfig.ap.password) == 0){
        wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        wifiConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifiConfig) );
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifiStartSTA(void *arg){
    ESP_ERROR_CHECK(esp_wifi_stop());

    // copy configuration from internal config over to the stuff esp uses
    strcpy((char *)wifiConfig.sta.ssid, wifiNvmConf.staSsid);
    strcpy((char *)wifiConfig.sta.password, wifiNvmConf.staPass);
    wifiConfig.sta.threshold.authmode = wifiNvmConf.authMode;

    wifiConfig.sta.sae_pwe_h2e = WPA3_SAE_PWE_HUNT_AND_PECK;
    wifiConfig.sta.sae_h2e_identifier[0] = '\x00';

    wifiConfig.sta.listen_interval = 10;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) );
    ESP_ERROR_CHECK(esp_wifi_start());
}

void saveWifiNvmConf(void){
    ESP_LOGD(TAG, "saving wifi info");
    nvs_set_str(wifiNvsHandle, "ssid", (char *)wifiNvmConf.staSsid);
    nvs_set_str(wifiNvsHandle, "pass", (char *)wifiNvmConf.staPass);
    nvs_set_u32(wifiNvsHandle, "auth", wifiNvmConf.authMode);
    nvs_commit(wifiNvsHandle);
}
#endif

/**
 * Loads information about WiFi from flash. Returns either 0 for success, or -1 for failed (should load default after)
 */
#ifndef UNIT_TEST
int wifiLoadNvmConf(void){
    u32 authMode;
    size_t maxStrLen = MAX_WIFI_INFO_STRLEN;

    if(nvs_get_str(wifiNvsHandle, "ssid", (char *)wifiNvmConf.staSsid, &maxStrLen) != ESP_OK){
        return -1;
    }
    if(nvs_get_str(wifiNvsHandle, "pass", (char *)wifiNvmConf.staPass, &maxStrLen) != ESP_OK){
        return -1;
    }
    if(nvs_get_u32(wifiNvsHandle, "auth", &authMode) != ESP_OK){
        return -1;
    }
    wifiNvmConf.authMode = authMode;

    return 0;
}
#endif

/********** init functions **********/
#ifndef UNIT_TEST
void wifiInit(void){
    esp_netif_t *netifSta;
    wifiEvents = xEventGroupCreate();

    // open nvs handler
    ESP_ERROR_CHECK(nvs_open(NVS_ID, NVS_READWRITE, &wifiNvsHandle));

    // init tcp ip stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    netifSta = esp_netif_create_default_wifi_sta();
    // set the hostname advertized to the router
    esp_netif_set_hostname(netifSta, CONFIG_ESP_HOSTNAME);

    // init mDNS
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(CONFIG_ESP_HOSTNAME);

    wifi_init_config_t wifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifiInitCfg));
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifiIpEventHandler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifiIpEventHandler,
                                                        NULL,
                                                        &instance_got_ip));


    int nvmStat = wifiLoadNvmConf();
    if(nvmStat){
        ESP_LOGW(TAG, "Unable to load NVM flash, setting as as AP");
        wifiStartAP();
    }
    else{
        wifiStartSTA(NULL);
    }
}
#endif


void startHttpServer(void){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = HTTPD_MAX_URI_HANDLERS;
    config.stack_size = 4096*7;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_start(&server, &config);

    httpd_uri_t uriMatch = {0};
    uriMatch.method = HTTP_GET;
    uriMatch.user_ctx = NULL;

    /**** 404 commands */
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handle404NotFound);

    /**** GET commands */
    uriMatch.handler = handleUriGetVersion;
    uriMatch.uri = "/api/v1/version";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetCoffee;
    uriMatch.uri = "/api/v1/coffee";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetStatus;
    uriMatch.uri = "/api/v1/status";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetWifiInfo;
    uriMatch.uri = "/api/v1/wifi/info";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetPmicInfo;
    uriMatch.uri = "/api/v1/pmic";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetImgAvailable;
    uriMatch.uri = "/api/v1/img/available";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriImgGet;
    uriMatch.uri = "/api/v1/img/get";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetMode;
    uriMatch.uri = "/api/v1/mode";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetPlaylistImages;
    uriMatch.uri = "/api/v1/img/playlist/get";
    httpd_register_uri_handler(server, &uriMatch);

    /**** POST commands */
    uriMatch.method = HTTP_POST;
    uriMatch.handler = handleUriPostSetDisplayFb;
    uriMatch.uri = "/api/v1/disp/setFb";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostUpdateDisplay;
    uriMatch.uri = "/api/v1/disp/update";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostImageCheckerPattern;
    uriMatch.uri = "/api/v1/disp/setCheckPattern";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostWifiSta;
    uriMatch.uri = "/api/v1/wifi/info";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostWifiStaConn;
    uriMatch.uri = "/api/v1/wifi/connect";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriSaveImage;
    uriMatch.uri = "/api/v1/img/save";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostUploadSdImage;
    uriMatch.uri = "/api/v1/img/upload";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriLoadImage;
    uriMatch.uri = "/api/v1/img/load";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriDeleteImage;
    uriMatch.uri = "/api/v1/img/delete";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPlaylistAdd;
    uriMatch.uri = "/api/v1/img/playlist/add";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPlaylistDel;
    uriMatch.uri = "/api/v1/img/playlist/del";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriSetOperationMode;
    uriMatch.uri = "/api/v1/mode";
    httpd_register_uri_handler(server, &uriMatch);

    // last but not least, handle matching any generic web requests
    uriMatch.method = HTTP_GET;
    uriMatch.handler = handleUriWebGet;
    uriMatch.uri = "/*";
    httpd_register_uri_handler(server, &uriMatch);
}