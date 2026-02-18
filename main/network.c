
#include <string.h>
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
#else
#include "mock.h"
#endif

#include <cJSON.h>

#include "network.h"
#include "common.h"
#include "eink.h"
#include "main.h"

/* FreeRTOS event group to signal when we are connected*/
#ifndef UNIT_TEST
EventGroupHandle_t wifiEvents;
wifi_config_t wifiConfig;               // the esp internal wifi configuration
static nvs_handle_t wifiNvsHandle;      // the handle for nvm
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

static esp_err_t handleUriGetPmicInfo(httpd_req_t *req){
    esp_err_t ret;
    cJSON *jRoot;
    char *jsonPrint;

    httpd_resp_set_type(req, "application/json");
    jRoot = cJSON_CreateObject();

    if(xSemaphoreTake(pmicTelemetryMutex, pdMS_TO_TICKS(500)) != pdTRUE){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Could not take semephore for pmic struct\"}");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(jRoot, "stat", "ok");
    cJSON_AddNumberToObject(jRoot, "battVolt", (float)pmicTelem.battVolt_mV);
    cJSON_AddNumberToObject(jRoot, "sysVolt", (float)pmicTelem.sysVolt_mV);
    cJSON_AddNumberToObject(jRoot, "vBusVolt", (float)pmicTelem.vBusVolt_mV);
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
    char *recvBuf;
    char responseBuff[128];

    cJSON *jRoot = NULL;
    const cJSON *jSSID = NULL;
    const cJSON *jPass = NULL;

    httpd_resp_set_type(req, "application/json");

    int remaining = req->content_len;   // total bytes expected
    recvBuf = malloc(req->content_len);
    int r = httpd_req_recv(req, (char*)recvBuf, remaining);
    if(r < 0){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"Error while receiving info\"}");
        ret = ESP_FAIL;
        goto end;
    }
    jRoot = cJSON_Parse(recvBuf);

    if(jRoot == NULL){
        const char *error_ptr = cJSON_GetErrorPtr();
        if(error_ptr != NULL){
            snprintf(responseBuff, 128, "{\"stat\": \"JSON invalid: %s\"}", error_ptr);
        } else {
            snprintf(responseBuff, 128, "{\"stat\": \"JSON invalid: unknown\"}");
        }
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, responseBuff);
        ret = ESP_FAIL;
        goto end;
    }
    jSSID = cJSON_GetObjectItem(jRoot, "ssid");
    if (!cJSON_IsString(jSSID) || (jSSID->valuestring == NULL)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: ssid not a string\"}");
        ret = ESP_FAIL;
        goto end;
    }

    jPass = cJSON_GetObjectItem(jRoot, "pass");
    if (!cJSON_IsString(jPass) || (jPass->valuestring == NULL)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: password not a string\"}");
        ret = ESP_FAIL;
        goto end;
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

end:
    cJSON_Delete(jRoot);
    free(recvBuf);
    return ret;
}

static esp_err_t handleUriPostWifiStaConn(httpd_req_t *req){
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    httpd_queue_work(req->handle, wifiStartSTA, NULL);
    return ESP_OK;

}

static esp_err_t handleUriPostImageRaw(httpd_req_t *req){
    httpd_resp_set_type(req, "application/json");

    if(req->content_len != DISP_FB_SIZE){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"Frame buffer size invalid\"}");
        return ESP_FAIL;
    }

    if(xSemaphoreTake(displayFbMutex, 0) != pdTRUE){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Could not take frame buffer mutex\"}");
        return ESP_FAIL;
    }

    const size_t buf_sz = 16384;
    u8 *buf = malloc(buf_sz);
    u32 displayOffset = 0;
    u32 stat = 0;

    int remaining = req->content_len;   // total bytes expected
    while(1){
        int to_read = remaining < buf_sz ? remaining : buf_sz;
        
        int r = httpd_req_recv(req, (char*)buf, to_read);
        if (r > 0) {
            stat = setFrameBuffRaw(buf, r, displayOffset);
            if(stat){
                break;
            }

            displayOffset += r;
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
    
    free(buf);
    xSemaphoreGive(displayFbMutex);
    
    if(stat){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Frame buffer overflow\"}");
    }
    else{
        httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
        dispTrigUpdate();
    }

    return ESP_OK;
}

static esp_err_t handleUriPostImageCheckerPattern(httpd_req_t *req){
    esp_err_t ret;
    char *recvBuf;
    char responseBuff[128];

    cJSON *jRoot = NULL;
    const cJSON *jCheckSize = NULL;

    httpd_resp_set_type(req, "application/json");

    if(xSemaphoreTake(displayFbMutex, 0) != pdTRUE){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"stat\": \"Could not take frame buffer mutex\"}");
        return ESP_FAIL;
    }

    int remaining = req->content_len;   // total bytes expected
    recvBuf = malloc(req->content_len);
    int r = httpd_req_recv(req, (char*)recvBuf, remaining);
    if(r < 0){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"Error while receiving info\"}");
        ret = ESP_FAIL;
        goto end;
    }
    jRoot = cJSON_Parse(recvBuf);

    if(jRoot == NULL){
        const char *error_ptr = cJSON_GetErrorPtr();
        if(error_ptr != NULL){
            snprintf(responseBuff, 128, "{\"stat\": \"JSON invalid: %s\"}", error_ptr);
        } else {
            snprintf(responseBuff, 128, "{\"stat\": \"JSON invalid: unknown\"}");
        }
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, responseBuff);
        ret = ESP_FAIL;
        goto end;
    }
    jCheckSize = cJSON_GetObjectItem(jRoot, "checkSize");
    if (!cJSON_IsNumber(jCheckSize)){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"stat\": \"JSON invalid: checkSize not an integer\"}");
        ret = ESP_FAIL;
        goto end;
    }

    dispCheckerPattern(jCheckSize->valueint);
    dispTrigUpdate();
    httpd_resp_sendstr(req, "{\"stat\": \"ok\"}");
    ret = ESP_OK;

end:
    xSemaphoreGive(displayFbMutex);
    cJSON_Delete(jRoot);
    free(recvBuf);
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
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // config.uri_match_fn = httpd_uri_match_wildcard;

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

    uriMatch.handler = handleUriGetWifiInfo;
    uriMatch.uri = "/api/v1/wifiInfo";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetPmicInfo;
    uriMatch.uri = "/api/v1/pmicInfo";
    httpd_register_uri_handler(server, &uriMatch);

    /**** POST commands */
    uriMatch.method = HTTP_POST;
    uriMatch.handler = handleUriPostImageRaw;
    uriMatch.uri = "/api/v1/dispFbRaw";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostImageCheckerPattern;
    uriMatch.uri = "/api/v1/dispCheckPattern";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostWifiSta;
    uriMatch.uri = "/api/v1/wifiInfo";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriPostWifiStaConn;
    uriMatch.uri = "/api/v1/wifiConnectSta";
    httpd_register_uri_handler(server, &uriMatch);
}