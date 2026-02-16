#include "unity.h"
#include "mock.h"
#include "network.h"
#include <string.h>
#include <cJSON.h>

// #define DEBUG_PRINT

#define MAX_URLS        10
#define FILL_MAGIC      0xA5

const int displayFbMutex = 1;

httpd_req_t globalHttpReq;
httpd_err_handler_func_t httpErrFunc;
httpd_uri_t urlToProc[MAX_URLS];
int urlToProcLen;

// flags to be able to trip errors
int dispSemaphoreTake = 0;
int setFrameBuff_resp = ESP_OK;
int didDisplayTrig = false;

int xSemaphoreTake(int contextN, int timeout){
    return dispSemaphoreTake;
}

void xSemaphoreGive(int contextN){

}

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode){
    *mode = WIFI_MODE_STA;
    return ESP_OK;
}
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config){
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler){
    if(urlToProcLen >= MAX_URLS){
        TEST_FAIL_MESSAGE("Unable to allocate more URLs");
    }
    memcpy(&urlToProc[urlToProcLen], uri_handler, sizeof(httpd_uri_t));
    urlToProcLen++;
    return ESP_OK;
}

esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t error, httpd_err_handler_func_t err_handler_fn){
    httpErrFunc = err_handler_fn;
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str){
#ifdef DEBUG_PRINT
    TEST_PRINTF("-> Setting response %s", str);
#endif
    strcpy(r->responseDat, str);
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type){
#ifdef DEBUG_PRINT
    TEST_PRINTF("-> Setting the response type to %s", type);
#endif
    strcpy(r->responseType, type);
    return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status){
    strcpy(r->retCode, status);
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *usr_msg){
#ifdef DEBUG_PRINT
    TEST_PRINTF("-> Setting response %s with code %d", usr_msg, error);
#endif
    strcpy(req->responseDat, usr_msg);
    switch(error){
        case HTTPD_400_BAD_REQUEST:
            strcpy(req->retCode, HTTP_400_CODE_STR);
            break;
        case HTTPD_404_NOT_FOUND:
            strcpy(req->retCode, HTTP_404_CODE_STR);
            break;
        case HTTPD_500_INTERNAL_SERVER_ERROR:
            strcpy(req->retCode, HTTP_500_CODE_STR);
            break;
        default:
            TEST_FAIL_MESSAGE("invalid response");
            break;
    }
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len){
    memset(buf, FILL_MAGIC, buf_len);
    return buf_len;
}

esp_err_t httpd_queue_work(httpd_handle_t handle, httpd_work_fn_t work, void *arg){
    return ESP_OK;
}

void dispCheckerPattern(uint32_t checkerSizeLog2){

}

int setFrameBuffRaw(uint8_t *data, uint32_t len, uint32_t offset){
    TEST_ASSERT_EACH_EQUAL_UINT8(FILL_MAGIC, data, len);
    return setFrameBuff_resp;
}

void dispTrigUpdate(void){
    didDisplayTrig = true;
}

void wifiStartSTA(void *arg){

}

void saveWifiNvmConf(void){

}

#define BASIC_RESPONSE_CHECK() \
    jRoot = cJSON_Parse(globalHttpReq.responseDat);                 \
    TEST_ASSERT_NOT_NULL(jRoot);                                    \
    jToCheck = cJSON_GetObjectItem(jRoot, "stat");        \
    TEST_ASSERT_NOT_NULL(jToCheck);                                    \
    TEST_ASSERT_TRUE(cJSON_IsString(jToCheck));                        \
    TEST_ASSERT_NOT_NULL(jToCheck->valuestring);


void setUp(void) {
    // set stuff up here
    urlToProcLen = 0;
    startHttpServer();
    memset(&globalHttpReq, 0, sizeof(httpd_req_t));
    strcpy(globalHttpReq.retCode, HTTP_200_CODE_STR);
    dispSemaphoreTake = pdTRUE;
    setFrameBuff_resp = ESP_OK;
    didDisplayTrig = false;
}

void tearDown(void) {
    // clean stuff up here
}


static void httpExecute(const char *uri, httpd_method_t method){
    bool didExecute = false;
    for(int i=0;i<urlToProcLen;i++){
        if(strcmp(uri, urlToProc[i].uri) == 0 && (urlToProc[i].method == method)){
            TEST_ASSERT_FALSE(didExecute);      // to ensure only one handle is made for a certain URL

            urlToProc[i].handler(&globalHttpReq);
            didExecute = true;
        }
    }
    if(didExecute == false){
        if(httpErrFunc) httpErrFunc(&globalHttpReq, HTTPD_404_NOT_FOUND);
    }
}

void test_version(void){
    globalHttpReq.content_len = 0;
    httpExecute("/api/v1/version", HTTP_GET);
    // todo: generate a contract expectation based off the openAPI yaml file
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_200_CODE_STR, globalHttpReq.retCode);
    cJSON *jRoot = NULL;
    const cJSON *jToCheck = NULL;
    BASIC_RESPONSE_CHECK();
    TEST_ASSERT_EQUAL_STRING("ok", jToCheck->valuestring);       
    cJSON_Delete(jRoot);
}

void test_coffee(void){
    globalHttpReq.content_len = 0;
    httpExecute("/api/v1/coffee", HTTP_GET);
    // todo: generate a contract expectation based off the openAPI yaml file
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_418_CODE_STR, globalHttpReq.retCode);
    cJSON *jRoot = NULL;
    const cJSON *jToCheck = NULL;
    BASIC_RESPONSE_CHECK();
    TEST_ASSERT_EQUAL_STRING("I'm a teapot", jToCheck->valuestring);      

    cJSON_Delete(jRoot);
}

void test_handleUriGetWifiInfo(void){
    globalHttpReq.content_len = 0;
    httpExecute("/api/v1/wifiInfo", HTTP_GET);
    // todo: generate a contract expectation based off the openAPI yaml file
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_200_CODE_STR, globalHttpReq.retCode);

    cJSON *jRoot = NULL;
    const cJSON *jToCheck = NULL;
    BASIC_RESPONSE_CHECK();
    TEST_ASSERT_EQUAL_STRING("ok", jToCheck->valuestring);    

    jToCheck = cJSON_GetObjectItem(jRoot, "currentMode");
    TEST_ASSERT_TRUE(cJSON_IsString(jToCheck));
    TEST_ASSERT_NOT_NULL(jToCheck);
    jToCheck = cJSON_GetObjectItem(jRoot, "staSSID");
    TEST_ASSERT_TRUE(cJSON_IsString(jToCheck));
    TEST_ASSERT_NOT_NULL(jToCheck);
    jToCheck = cJSON_GetObjectItem(jRoot, "staPass");
    TEST_ASSERT_TRUE(cJSON_IsString(jToCheck));
    TEST_ASSERT_NOT_NULL(jToCheck);

    cJSON_Delete(jRoot);
}

void test_setFbRaw_invalidLen(void){
    cJSON *jRoot = NULL; const cJSON *jToCheck = NULL;
    globalHttpReq.content_len = 0;
    httpExecute("/api/v1/dispFbRaw", HTTP_POST);
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_400_CODE_STR, globalHttpReq.retCode);
    BASIC_RESPONSE_CHECK();
    TEST_ASSERT_FALSE(didDisplayTrig);
}

void test_setFbRaw_failSemaphore(void){
    cJSON *jRoot = NULL; const cJSON *jToCheck = NULL;
    globalHttpReq.content_len = 192000;
    dispSemaphoreTake = pdFALSE;
    httpExecute("/api/v1/dispFbRaw", HTTP_POST);
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_500_CODE_STR, globalHttpReq.retCode);
    BASIC_RESPONSE_CHECK();
    TEST_ASSERT_FALSE(didDisplayTrig);
}

void test_setFbRaw_bufferOverflow(void){
    cJSON *jRoot = NULL; const cJSON *jToCheck = NULL;
    globalHttpReq.content_len = 192000;
    setFrameBuff_resp = 1;
    httpExecute("/api/v1/dispFbRaw", HTTP_POST);
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_500_CODE_STR, globalHttpReq.retCode);
    BASIC_RESPONSE_CHECK();
    TEST_ASSERT_FALSE(didDisplayTrig);
}

void test_setFbRaw_normal(void){
    cJSON *jRoot = NULL; const cJSON *jToCheck = NULL;
    globalHttpReq.content_len = 192000;
    httpExecute("/api/v1/dispFbRaw", HTTP_POST);
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_200_CODE_STR, globalHttpReq.retCode);
    BASIC_RESPONSE_CHECK();
    TEST_ASSERT_TRUE(didDisplayTrig);
}

void test_invalidURL(void){
    httpExecute("/api/v1/awda", HTTP_GET);
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_404_CODE_STR, globalHttpReq.retCode);
    cJSON *jRoot = NULL; const cJSON *jToCheck = NULL;
    BASIC_RESPONSE_CHECK(); 
    TEST_ASSERT_EQUAL_STRING("Not Found", jToCheck->valuestring);

    httpExecute("/api/v1/version", HTTP_POST);
    TEST_ASSERT_EQUAL_STRING("application/json", globalHttpReq.responseType);
    TEST_ASSERT_EQUAL_STRING(HTTP_404_CODE_STR, globalHttpReq.retCode);
    BASIC_RESPONSE_CHECK(); 
    TEST_ASSERT_EQUAL_STRING("Not Found", jToCheck->valuestring);
}


void main(){
    UNITY_BEGIN();
    RUN_TEST(test_version);
    RUN_TEST(test_coffee);
    RUN_TEST(test_handleUriGetWifiInfo);

    RUN_TEST(test_setFbRaw_invalidLen);
    RUN_TEST(test_setFbRaw_failSemaphore);
    RUN_TEST(test_setFbRaw_bufferOverflow);
    RUN_TEST(test_setFbRaw_normal);

    RUN_TEST(test_invalidURL);
    UNITY_END();
}