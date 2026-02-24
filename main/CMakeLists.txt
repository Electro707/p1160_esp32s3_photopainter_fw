idf_component_register(SRCS "fileSys.c" "network.c" "eink.c" "main.c" "network.c"
                       PRIV_REQUIRES 
                            spi_flash
                            esp_driver_gpio
                            esp_driver_spi
                            esp_driver_i2c
                            esp_driver_sdmmc
                            soc
                            hal
                            esp_wifi
                            esp_http_server
                            nvs_flash
                            esp_psram
                            sdmmc
                            fatfs
                       INCLUDE_DIRS "."
                       REQUIRES pmic)
