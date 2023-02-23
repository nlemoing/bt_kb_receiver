#include "esp_log.h"
#include "esp_hidh_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "usb_hid_codes.h"

// Set this to the BT MAC adress of the HID device that you're connecting to
static esp_bd_addr_t _peer_bd_addr = { 0xDC, 0x2C, 0x26, 0x00, 0x37, 0xA9 };

#define READY_TIMEOUT (10 * 1000)

// _hid_event_group
static EventGroupHandle_t _hid_event_group = NULL;
#define HID_RUNNING      0x01
#define HID_CONNECTED    0x02
#define HID_CLOSED       0x04

char *key_mappings[0x64];

static void init_key_mappings(void) {
    key_mappings[KEY_KP0] = "0";
    key_mappings[KEY_KP1] = "1";
    key_mappings[KEY_KP2] = "2";
    key_mappings[KEY_KP3] = "3";
    key_mappings[KEY_KP4] = "4";
    key_mappings[KEY_KP5] = "5";
    key_mappings[KEY_KP6] = "6";
    key_mappings[KEY_KP7] = "7";
    key_mappings[KEY_KP8] = "8";
    key_mappings[KEY_KP9] = "9";
    key_mappings[KEY_KPSLASH] = "slash";
    key_mappings[KEY_KPASTERISK] = "asterisk";
    key_mappings[KEY_KPMINUS] = "minus";
    key_mappings[KEY_KPPLUS] = "plus";
    key_mappings[KEY_KPENTER] = "enter";
    key_mappings[KEY_ESC] = "esc";
    key_mappings[KEY_TAB] = "tab";
    key_mappings[KEY_BACKSPACE] = "backspace";
}


static void key_press(uint8_t key) {
    const char *TAG = "key_press";
    ESP_LOGI(TAG, "Received key press: 0x%x", key);
    char *url_path = key_mappings[key];
    if (url_path != 0) {
        ESP_LOGI(TAG, "Sending data to path %s", url_path);
        // Send HTTP request
    }
}

static void esp_hidh_cb(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param) {
    const char *TAG = "esp_hidh_cb";
    switch (event) {
        case ESP_HIDH_INIT_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_INIT_EVT");
            xEventGroupSetBits(_hid_event_group, HID_RUNNING);
            break;
        case ESP_HIDH_DEINIT_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_DEINIT_EVT");
            break;
        case ESP_HIDH_OPEN_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_OPEN_EVT");
            xEventGroupSetBits(_hid_event_group, HID_CONNECTED);
            xEventGroupClearBits(_hid_event_group, HID_CLOSED);
            break;
        case ESP_HIDH_CLOSE_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_CLOSE_EVT");
            xEventGroupSetBits(_hid_event_group, HID_CLOSED);
            xEventGroupClearBits(_hid_event_group, HID_CONNECTED);
            break;
        case ESP_HIDH_GET_RPT_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_GET_RPT_EVT");
            break;
        case ESP_HIDH_SET_RPT_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_SET_RPT_EVT");
            break;
        case ESP_HIDH_GET_PROTO_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_GET_PROTO_EVT");
            break;
        case ESP_HIDH_SET_PROTO_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_SET_PROTO_EVT");
            break;
        case ESP_HIDH_GET_IDLE_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_GET_IDLE_EVT");
            break;
        case ESP_HIDH_SET_IDLE_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_SET_IDLE_EVT");
            break;
        case ESP_HIDH_GET_DSCP_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_GET_DSCP_EVT");
            break;
        case ESP_HIDH_ADD_DEV_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_ADD_DEV_EVT");
            break;
        case ESP_HIDH_RMV_DEV_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_RMV_DEV_EVT");
            break;
        case ESP_HIDH_VC_UNPLUG_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_VC_UNPLUG_EVT");
            break;
        case ESP_HIDH_DATA_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_DATA_EVT");
            break;
        case ESP_HIDH_DATA_IND_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_DATA_IND_EVT");
            ESP_LOGI(TAG, "Status: %d", param->data_ind.status);
            ESP_LOGI(TAG, "Data length: %d", param->data_ind.len);
            // Key presses (both up and down) are 9 packets
            // First 3 packets: 0x01 0x00 0x00
            // Packets 4-9: keys currently pressed down
            // To avoid maintaining some hairy state, we'll just assume we only have
            // one key pressed down at a time so we can always read from packet 4
            if (param->data_ind.status == ESP_HIDH_OK && param->data_ind.len == 9) {
                key_press(param->data_ind.data[3]);
            }
            break;
        case ESP_HIDH_SET_INFO_EVT:
            ESP_LOGI(TAG, "ESP_HIDH_SET_INFO_EVT");
            break;
    }
}

static bool _init_bt()
{
    const char *TAG = "_init_bt";
    if(!_hid_event_group){
        _hid_event_group = xEventGroupCreate();
        if(!_hid_event_group){
            ESP_LOGE(TAG, "HID Event Group Create Failed!");
            return false;
        }
        xEventGroupClearBits(_hid_event_group, 0xFFFFFF);
        xEventGroupSetBits(_hid_event_group, HID_CLOSED);
    }

    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "initialize controller failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "enable controller failed: %s\n", esp_err_to_name(ret));
        return false;
    }
    
    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "initialize bluedroid failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "enable bluedroid failed: %s\n", esp_err_to_name(ret));
        return false;
    }

    if ((ret = esp_bt_hid_host_register_callback(esp_hidh_cb)) != ESP_OK) {
        ESP_LOGE(TAG, "hidh register failed");
        return false;
    }

    if ((ret = esp_bt_hid_host_init()) != ESP_OK) {
        ESP_LOGE(TAG, "hidh init failed");
        return false;
    }

    ESP_LOGI(TAG, "device name set");
    esp_bt_dev_set_device_name("BT KB Receiver");

    return true;
}

static bool waitForConnect(int timeout) {
    const char *TAG = "waitForConnect";
    TickType_t xTicksToWait = timeout / portTICK_PERIOD_MS;
    // wait for connected or closed
    EventBits_t rc = xEventGroupWaitBits(_hid_event_group, HID_CONNECTED | HID_CLOSED, pdFALSE, pdFALSE, xTicksToWait);
    if((rc & HID_CONNECTED) != 0)
        return true;
    else if((rc & HID_CLOSED) != 0) {
        ESP_LOGD(TAG, "connection closed!");
        return false;
    }
    ESP_LOGD(TAG, "timeout");
    return false;
}

void app_main(void)
{
    const char *TAG = "app_main";

    init_key_mappings();

    if (!_init_bt()) {
        ESP_LOGE(TAG, "Failed to initialize Bluetooth, exiting.");
        return;
    }

    esp_err_t initRet;
    bool waitRet;

    // This currently only works if the device is in pairing mode when the code starts up
    // Look into making the device connectable/discoverable if there's nothing connected?
    // Look into whether the MAC address of the receiver changes somehow which might confuse the KB
    ESP_LOGI(TAG, "Connection attempt initializing...");
    if ((initRet = esp_bt_hid_host_connect(_peer_bd_addr)) != ESP_OK) {
        
        ESP_LOGE(TAG, "Failed to initialize connection attempt: %s", esp_err_to_name(initRet));
    }
    
    ESP_LOGI(TAG, "Connection attempt initialized, waiting for connection...");
    waitRet = waitForConnect(READY_TIMEOUT);
    if (waitRet) {
        ESP_LOGI(TAG, "Connection successful");
    } else {
        ESP_LOGI(TAG, "Connection unsuccessful");
    }
}
