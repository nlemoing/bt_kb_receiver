#include "esp_log.h"
#include "esp_hidh_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_gap_bt_api.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "usb_hid_codes.h"
#include "wifi_constants.h"

// Set this to the BT MAC adress of the HID device that you're connecting to
static esp_bd_addr_t _peer_bd_addr = { 0xDC, 0x2C, 0x26, 0x00, 0x37, 0xA9 };

#define READY_TIMEOUT (10 * 1000)

// _hid_event_group
static EventGroupHandle_t _hid_event_group = NULL;
#define HID_RUNNING      0x01
#define HID_CONNECTED    0x02
#define HID_CLOSED       0x04

// _wifi_event_group
static EventGroupHandle_t _wifi_event_group = NULL;
#define WIFI_CONNECTED   0x01
#define WIFI_FAIL        0x02

// HTTP defs
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
    key_mappings[KEY_KPDOT] = "dot";
    key_mappings[KEY_KPSLASH] = "slash";
    key_mappings[KEY_KPASTERISK] = "asterisk";
    key_mappings[KEY_KPMINUS] = "minus";
    key_mappings[KEY_KPPLUS] = "plus";
    key_mappings[KEY_KPENTER] = "enter";
    key_mappings[KEY_ESC] = "esc";
    key_mappings[KEY_TAB] = "tab";
    key_mappings[KEY_BACKSPACE] = "backspace";
}

static void send_request(const char *path) {
    const char *TAG = "send_request";
    esp_http_client_config_t config = {
        .host = SERVER_IP,
        .path = path,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Error initializing HTTP client");
        return;
    }

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Request completed successfully");
    } else {
        ESP_LOGE(TAG, "Request failed %s\n", esp_err_to_name(ret));
    }
}


static void key_press(uint8_t key) {
    const char *TAG = "key_press";
    char *key_path = key_mappings[key];
    if (key_path != NULL) {
        ESP_LOGI(TAG, "Received key press: 0x%x", key);
        char path[32] = "/remote/";
        strcat(path, key_path);
        send_request(path);
    } else {
        ESP_LOGW(TAG, "Received unknown key press: 0x%x", key);
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
            if (param->open.conn_status == ESP_HIDH_CONN_STATE_CONNECTED) {
                xEventGroupSetBits(_hid_event_group, HID_CONNECTED);
                xEventGroupClearBits(_hid_event_group, HID_CLOSED);
            }
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

    // Setting this after we've already connected means that devices can connect
    // back to us if they power off and back on again
    if ((ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)) != ESP_OK) {
        ESP_LOGE(TAG, "setting scan mode failed");
        return false;
    }

    return true;
}

static bool try_connect_bt(int timeout) {
    const char *TAG = "try_connect_bt";

    esp_err_t initRet;

    ESP_LOGI(TAG, "Connection attempt initializing...");
    if ((initRet = esp_bt_hid_host_connect(_peer_bd_addr)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize connection attempt: %s", esp_err_to_name(initRet));
    }

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

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    const char *TAG = "wifi_event_handler";
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(_wifi_event_group, WIFI_FAIL);
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(_wifi_event_group, WIFI_CONNECTED);
    }
}


static bool _init_wifi(void) {
    const char *TAG = "_init_wifi";

    if (!_wifi_event_group) {
        _wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_HOSTNAME,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(_wifi_event_group,
            WIFI_CONNECTED | WIFI_FAIL,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED) {
        ESP_LOGI(TAG, "Connected to WiFi network %s successfully.", WIFI_HOSTNAME);
        return true;
    }
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

    if (!_init_wifi()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi, exiting.");
        return;
    }

    // After starting up, try connecting to the device. Before the devices have
    // paired, this step is necessary to initiate the pairing. The device has to
    // be in pairing mode when the host is starting up for this to succeed - what
    // I ended up doing was putting the device in pairing mode and then hitting
    // reset on the ESP32. Once the devices have paired, this step is unnecessary
    // as the device will attempt to connect on its own (we've set the GAP scan mode
    // to connectable, non-discoverable which allows previously paired devices to
    // connect).
    try_connect_bt(READY_TIMEOUT);
}
