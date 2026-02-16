#ifndef MOCK_H
#define MOCK_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

typedef void* httpd_handle_t;
typedef void (*httpd_work_fn_t)(void *arg);

#define pdFALSE                                  ( 0 )
#define pdTRUE                                   ( 1 )

#define CONFIG_HTTPD_MAX_URI_LEN 512
#define MAX_RESPONSE_LEN         512
#define MAX_RESPONSE_TYPE        64

#define HTTPD_SOCK_ERR_TIMEOUT -1


#define BIT1     0x00000002
#define BIT0     0x00000001

typedef enum {
    HTTP_GET,
    HTTP_POST,
}httpd_method_t;

typedef enum{
    ESP_OK,
    ESP_FAIL,
}esp_err_t;

typedef enum {
    WIFI_MODE_NULL = 0,  /**< Null mode */
    WIFI_MODE_STA,       /**< Wi-Fi station mode */
    WIFI_MODE_AP,        /**< Wi-Fi soft-AP mode */
    WIFI_MODE_APSTA,     /**< Wi-Fi station + soft-AP mode */
    WIFI_MODE_NAN,       /**< Wi-Fi NAN mode */
    WIFI_MODE_MAX
} wifi_mode_t;

#define HTTP_200_CODE_STR   "200 OK"
#define HTTP_400_CODE_STR   "400 Bad Request"
#define HTTP_404_CODE_STR   "404 Not Found"
#define HTTP_500_CODE_STR   "500 Internal Server Error"
#define HTTP_418_CODE_STR   "418 I'm a teapot"

typedef enum{
    HTTPD_400_BAD_REQUEST,
    HTTPD_404_NOT_FOUND,
    HTTPD_500_INTERNAL_SERVER_ERROR,
}httpd_err_code_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,         /**< Authenticate mode : open */
    WIFI_AUTH_WEP,              /**< Authenticate mode : WEP */
    WIFI_AUTH_WPA_PSK,          /**< Authenticate mode : WPA_PSK */
    WIFI_AUTH_WPA2_PSK,         /**< Authenticate mode : WPA2_PSK */
    WIFI_AUTH_WPA_WPA2_PSK,     /**< Authenticate mode : WPA_WPA2_PSK */
    WIFI_AUTH_ENTERPRISE,       /**< Authenticate mode : Wi-Fi EAP security, treated the same as WIFI_AUTH_WPA2_ENTERPRISE */
    WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE,  /**< Authenticate mode : WPA2-Enterprise security */
    WIFI_AUTH_WPA3_PSK,         /**< Authenticate mode : WPA3_PSK */
    WIFI_AUTH_WPA2_WPA3_PSK,    /**< Authenticate mode : WPA2_WPA3_PSK */
    WIFI_AUTH_WAPI_PSK,         /**< Authenticate mode : WAPI_PSK */
    WIFI_AUTH_OWE,              /**< Authenticate mode : OWE */
    WIFI_AUTH_WPA3_ENT_192,     /**< Authenticate mode : WPA3_ENT_SUITE_B_192_BIT */
    WIFI_AUTH_WPA3_EXT_PSK,     /**< This authentication mode will yield same result as WIFI_AUTH_WPA3_PSK and not recommended to be used. It will be deprecated in future, please use WIFI_AUTH_WPA3_PSK instead. */
    WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE, /**< This authentication mode will yield same result as WIFI_AUTH_WPA3_PSK and not recommended to be used. It will be deprecated in future, please use WIFI_AUTH_WPA3_PSK instead.*/
    WIFI_AUTH_DPP,              /**< Authenticate mode : DPP */
    WIFI_AUTH_WPA3_ENTERPRISE,  /**< Authenticate mode : WPA3-Enterprise Only Mode */
    WIFI_AUTH_WPA2_WPA3_ENTERPRISE, /**< Authenticate mode : WPA3-Enterprise Transition Mode */
    WIFI_AUTH_WPA_ENTERPRISE,   /**< Authenticate mode : WPA-Enterprise security */
    WIFI_AUTH_MAX
} wifi_auth_mode_t;

typedef struct httpd_req {
    httpd_handle_t  handle;                     /*!< Handle to server instance */
    int             method;                     /*!< The type of HTTP request, -1 if unsupported method, HTTP_ANY for wildcard method to support every method */
    const char      uri[CONFIG_HTTPD_MAX_URI_LEN + 1]; /*!< The URI of this request (1 byte extra for null termination) */
    size_t          content_len;                /*!< Length of the request body */
    void           *aux;                        /*!< Internally used members */
    void *user_ctx;
    // internal stuff used during tests
    char responseType[MAX_RESPONSE_TYPE];
    char responseDat[MAX_RESPONSE_LEN];
    char retCode[MAX_RESPONSE_TYPE];
} httpd_req_t;

typedef struct{
    int test;
}httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() { \
        .test      = 123,       \
}

#define ESP_LOGD(_TAG, __VA) printf(__VA)

typedef struct httpd_uri {
    const char       *uri;    /*!< The URI to handle */
    httpd_method_t    method; /*!< Method supported by the URI, HTTP_ANY for wildcard method to support all methods*/
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;
} httpd_uri_t;

typedef esp_err_t (*httpd_err_handler_func_t)(httpd_req_t *req, httpd_err_code_t error);

extern const int displayFbMutex;

int xSemaphoreTake(int contextN, int timeout);
void xSemaphoreGive(int contextN);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t error, httpd_err_handler_func_t err_handler_fn);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *usr_msg);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_queue_work(httpd_handle_t handle, httpd_work_fn_t work, void *arg);

// void wifiStartAP(void);
void wifiStartSTA(void *arg);
void dispTrigUpdate(void);
// void saveWifiNvmConf(void);
// void dispTrigUpdate(void);

#endif