#ifndef NETWORK_H
#define NETWORK_H

#ifndef UNIT_TEST
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#endif

typedef enum{
    WIFI_CONNECTED_BIT      = BIT0,
    WIFI_FAIL_BIT           = BIT1,
}wifiEventsBits_e;

void wifiInit(void);
void startHttpServer(void);

#ifndef UNIT_TEST
extern EventGroupHandle_t wifiEvents;
#endif

#endif