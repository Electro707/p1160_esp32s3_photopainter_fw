idf_component_register(SRCS "network.c" "eink.c" "main.c" "network.c"
                       PRIV_REQUIRES 
                            spi_flash
                            esp_driver_gpio
                            esp_driver_spi
                            esp_driver_i2c
                            soc
                            hal
                            esp_wifi
                            esp_http_server
                            nvs_flash
                            esp_psram
                       INCLUDE_DIRS "."
                       REQUIRES pmic)
