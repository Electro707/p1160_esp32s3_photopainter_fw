
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "network.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "common.h"
#include "eink.h"
#include "main.h"

/* FreeRTOS event group to signal when we are connected*/
EventGroupHandle_t wifiEvents;

static const char *TAG = "wifi";

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define WIFI_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define WIFI_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define WIFI_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


/********** URI match handlers **********/
static esp_err_t handleUriGetVersion(httpd_req_t* req){
    char tmp[128];
    httpd_resp_set_type(req, "application/json");
    snprintf(tmp, 128, "{\"stat\": \"ok\", \"version\": \"%s Rev %s\"}", FW_NAME, FW_REV);
    httpd_resp_sendstr(req, tmp);
    return ESP_OK;
}

static esp_err_t handleUriGetCoffee(httpd_req_t* req){
    httpd_resp_set_status(req, "418 I'm a teapot");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"stat\": \"I'm a teapot\"}");
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
        // dispTrigUpdate();
    }

    return ESP_OK;
}

/********** wifi events **********/
static void wifiIpEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int s_retry_num = 0;

    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
        esp_wifi_connect();
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
        if(s_retry_num < CONFIG_ESP_MAXIMUM_RETRY){
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else{
            xEventGroupSetBits(wifiEvents, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifiEvents, WIFI_CONNECTED_BIT);
    }
}

/********** init functions **********/
void wifiInit(void){
    wifiEvents = xEventGroupCreate();

    // init tcp ip stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // todo: support reading wifi ssid and password from nvs later on
    // todo: support switching to AP mode if this fails
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

    wifi_config_t wifiConfig = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = WIFI_H2E_IDENTIFIER,
        },
    };
    if(strlen(CONFIG_ESP_WIFI_PASSWORD) == 0){
        wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) );
    ESP_ERROR_CHECK(esp_wifi_start());
}


void startHttpServer(void){
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_start(&server, &config);

    httpd_uri_t uriMatch = {0};
    uriMatch.method = HTTP_GET;
    uriMatch.user_ctx = NULL;

    /**** GET commands */
    uriMatch.handler = handleUriGetVersion;
    uriMatch.uri = "/api/v1/version";
    httpd_register_uri_handler(server, &uriMatch);

    uriMatch.handler = handleUriGetCoffee;
    uriMatch.uri = "/api/v1/coffee";
    httpd_register_uri_handler(server, &uriMatch);

    /**** POST commands */
    uriMatch.method = HTTP_POST;
    uriMatch.handler = handleUriPostImageRaw;
    uriMatch.uri = "/api/v1/fbRaw";
    httpd_register_uri_handler(server, &uriMatch);

}