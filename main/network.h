#ifndef NETWORK_H
#define NETWORK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"

typedef enum{
    WIFI_CONNECTED_BIT = BIT0,
    WIFI_FAIL_BIT      = BIT1,
}wifiEventsBits_e;

void wifiInit(void);
void startHttpServer(void);

extern EventGroupHandle_t wifiEvents;

#endif