set(src_dirs  ./)
set(include_dirs  ./ 
                  ./REG)
idf_component_register(
  SRC_DIRS ${src_dirs}
  INCLUDE_DIRS ${include_dirs}
  REQUIRES esp_driver_i2c
)
add_compile_definitions(XPOWERS_CHIP_AXP2101 CONFIG_XPOWERS_ESP_IDF_NEW_API)
##REQUIRES