CC=gcc

CFLAGS += -DUNIT_TEST -DUNITY_INCLUDE_PRINT_FORMATTED
CFLAGS += -Og -g -Wall -Wextra
CFLAGS += -I .
CFLAGS += -I ../main
# CFLAGS += -I ../build/config		# for sdkconfig.h, which is used throughout esp-land

CFLAGS += -I $(IDF_PATH)/components/unity/unity/src
CFLAGS += -I ../managed_components/espressif__cjson/cJSON


BUILD_DIR := build

test_apis: $(BUILD_DIR)/testAPI.o $(BUILD_DIR)/network.o $(BUILD_DIR)/cJSON.o $(BUILD_DIR)/unity.o
	$(CC) $(CFLAGS) $^ -o $@.out

$(BUILD_DIR)/testAPI.o: testAPI.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/network.o: ../main/network.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cJSON.o: ../managed_components/espressif__cjson/cJSON/cJSON.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/unity.o: $(IDF_PATH)/components/unity/unity/src/unity.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) *.o test_apis