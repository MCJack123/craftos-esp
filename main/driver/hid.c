#include <esp_hidh.h>
#include <driver/gpio.h>
#include <freertos/task.h>
#include <usb/hid_host.h>
#include <usb/hid_usage_mouse.h>
#include <usb/hid_usage_keyboard.h>
#include <usb/usb_host.h>
#include "../event.h"
#include "hid.h"

static const char* const TAG = "hid";
ESP_EVENT_DEFINE_BASE(HID_EVENT);

// If your host terminal support ansi escape code, it can be use to simulate mouse cursor
#define USE_ANSI_ESCAPE   0

#define MAX_REPORT  4

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void *arg;
} hid_event_t;

static const char keycode2ascii[128][2] =  {
    {0     , 0      }, /* 0x00 */
    {0     , 0      }, /* 0x01 */
    {0     , 0      }, /* 0x02 */
    {0     , 0      }, /* 0x03 */
    {'a'   , 'A'    }, /* 0x04 */
    {'b'   , 'B'    }, /* 0x05 */
    {'c'   , 'C'    }, /* 0x06 */
    {'d'   , 'D'    }, /* 0x07 */
    {'e'   , 'E'    }, /* 0x08 */
    {'f'   , 'F'    }, /* 0x09 */
    {'g'   , 'G'    }, /* 0x0a */
    {'h'   , 'H'    }, /* 0x0b */
    {'i'   , 'I'    }, /* 0x0c */
    {'j'   , 'J'    }, /* 0x0d */
    {'k'   , 'K'    }, /* 0x0e */
    {'l'   , 'L'    }, /* 0x0f */
    {'m'   , 'M'    }, /* 0x10 */
    {'n'   , 'N'    }, /* 0x11 */
    {'o'   , 'O'    }, /* 0x12 */
    {'p'   , 'P'    }, /* 0x13 */
    {'q'   , 'Q'    }, /* 0x14 */
    {'r'   , 'R'    }, /* 0x15 */
    {'s'   , 'S'    }, /* 0x16 */
    {'t'   , 'T'    }, /* 0x17 */
    {'u'   , 'U'    }, /* 0x18 */
    {'v'   , 'V'    }, /* 0x19 */
    {'w'   , 'W'    }, /* 0x1a */
    {'x'   , 'X'    }, /* 0x1b */
    {'y'   , 'Y'    }, /* 0x1c */
    {'z'   , 'Z'    }, /* 0x1d */
    {'1'   , '!'    }, /* 0x1e */
    {'2'   , '@'    }, /* 0x1f */
    {'3'   , '#'    }, /* 0x20 */
    {'4'   , '$'    }, /* 0x21 */
    {'5'   , '%'    }, /* 0x22 */
    {'6'   , '^'    }, /* 0x23 */
    {'7'   , '&'    }, /* 0x24 */
    {'8'   , '*'    }, /* 0x25 */
    {'9'   , '('    }, /* 0x26 */
    {'0'   , ')'    }, /* 0x27 */
    {0     , 0      }, /* 0x28 */
    {0     , 0      }, /* 0x29 */
    {0     , 0      }, /* 0x2a */
    {0     , 0      }, /* 0x2b */
    {' '   , ' '    }, /* 0x2c */
    {'-'   , '_'    }, /* 0x2d */
    {'='   , '+'    }, /* 0x2e */
    {'['   , '{'    }, /* 0x2f */
    {']'   , '}'    }, /* 0x30 */
    {'\\'  , '|'    }, /* 0x31 */
    {'#'   , '~'    }, /* 0x32 */
    {';'   , ':'    }, /* 0x33 */
    {'\''  , '\"'   }, /* 0x34 */
    {'`'   , '~'    }, /* 0x35 */
    {','   , '<'    }, /* 0x36 */
    {'.'   , '>'    }, /* 0x37 */
    {'/'   , '?'    }, /* 0x38 */
                                
    {0     , 0      }, /* 0x39 */
    {0     , 0      }, /* 0x3a */
    {0     , 0      }, /* 0x3b */
    {0     , 0      }, /* 0x3c */
    {0     , 0      }, /* 0x3d */
    {0     , 0      }, /* 0x3e */
    {0     , 0      }, /* 0x3f */
    {0     , 0      }, /* 0x40 */
    {0     , 0      }, /* 0x41 */
    {0     , 0      }, /* 0x42 */
    {0     , 0      }, /* 0x43 */
    {0     , 0      }, /* 0x44 */
    {0     , 0      }, /* 0x45 */
    {0     , 0      }, /* 0x46 */
    {0     , 0      }, /* 0x47 */
    {0     , 0      }, /* 0x48 */
    {0     , 0      }, /* 0x49 */
    {0     , 0      }, /* 0x4a */
    {0     , 0      }, /* 0x4b */
    {0     , 0      }, /* 0x4c */
    {0     , 0      }, /* 0x4d */
    {0     , 0      }, /* 0x4e */
    {0     , 0      }, /* 0x4f */
    {0     , 0      }, /* 0x50 */
    {0     , 0      }, /* 0x51 */
    {0     , 0      }, /* 0x52 */
    {0     , 0      }, /* 0x53 */
                                
    {'/'   , '/'    }, /* 0x54 */
    {'*'   , '*'    }, /* 0x55 */
    {'-'   , '-'    }, /* 0x56 */
    {'+'   , '+'    }, /* 0x57 */
    {0     , 0      }, /* 0x58 */
    {'1'   , 0      }, /* 0x59 */
    {'2'   , 0      }, /* 0x5a */
    {'3'   , 0      }, /* 0x5b */
    {'4'   , 0      }, /* 0x5c */
    {'5'   , '5'    }, /* 0x5d */
    {'6'   , 0      }, /* 0x5e */
    {'7'   , 0      }, /* 0x5f */
    {'8'   , 0      }, /* 0x60 */
    {'9'   , 0      }, /* 0x61 */
    {'0'   , 0      }, /* 0x62 */
    {'.'   , 0      }, /* 0x63 */
    {0     , 0      }, /* 0x64 */
    {0     , 0      }, /* 0x65 */
    {0     , 0      }, /* 0x66 */
    {'='   , '='    }, /* 0x67 */
};

static const uint8_t cc_keymap[256] = {
    0, 0, 0, 0, 30, 48, 46, 32,                 // 00
    18, 33, 34, 35, 23, 36, 37, 38,             // 08
    50, 49, 24, 25, 16, 19, 31, 20,             // 10
    22, 47, 17, 45, 21, 44, 2, 3,               // 18
    4, 5, 6, 7, 8, 9, 10, 11,                   // 20
    28, 1, 14, 15, 57, 12, 13, 26,              // 28
    27, 43, 0, 39, 40, 41, 51, 52,              // 30
    43, 58, 59, 60, 61, 62, 63, 64,             // 38
    65, 66, 67, 68, 87, 88, 196, 70,            // 40
    197, 198, 199, 201, 211, 207, 209, 205,     // 48
    203, 208, 200, 69, 181, 55, 74, 78,         // 50
    156, 79, 80, 81, 75, 76, 77, 71,            // 58
    72, 73, 82, 83, 43, 0, 0, 0,                // 60
    0, 0, 0, 0, 0, 0, 0, 0,                     // 68
    0, 0, 0, 0, 0, 0, 0, 0,                     // 70
    0, 0, 0, 0, 0, 0, 0, 0,                     // 78
    0, 0, 0, 0, 0, 179, 141, 0,                 // 80
    112, 125, 121, 123, 0, 0, 0, 0,             // 88
    0, 0, 0, 0, 0, 0, 0, 0,                     // 90
    0, 0, 0, 0, 0, 0, 0, 0,                     // 98
    0, 0, 0, 0, 0, 0, 0, 0,                     // A0
    0, 0, 0, 0, 0, 0, 0, 0,                     // A8
    0, 0, 0, 0, 0, 0, 0, 0,                     // B0
    0, 0, 0, 0, 0, 0, 0, 0,                     // B8
    0, 0, 0, 0, 0, 0, 0, 0,                     // C0
    0, 0, 0, 0, 0, 0, 0, 0,                     // C8
    0, 0, 0, 0, 0, 0, 0, 0,                     // D0
    0, 0, 0, 0, 0, 0, 0, 0,                     // D8
    29, 42, 56, 91, 157, 54, 184, 92,           // E0
    0, 0, 0, 0, 0, 0, 0, 0,                     // E8
    0, 0, 0, 0, 0, 0, 0, 0,                     // F0
    0, 0, 0, 0, 0, 0, 0, 0,                     // F8
};
static const uint8_t cc_modifiers[8] = {29, 42, 56, 91, 157, 54, 184, 92};
static const char *hid_proto_name_str[] = {
    "NONE",
    "KEYBOARD",
    "MOUSE"
};

static uint32_t keypress_time[256] = {0};
static hid_keyboard_input_report_boot_t prev_report = {.modifier.val = 0, .reserved = 0, .key = {0}};

static IRAM_ATTR void key_cb(uint8_t key, bool isHeld) {
    event_t event;
    event.type = EVENT_TYPE_KEY;
    event.key.keycode = key;
    event.key.repeat = isHeld;
    event_push(&event);
}

static IRAM_ATTR void keyUp_cb(uint8_t key) {
    event_t event;
    event.type = EVENT_TYPE_KEY_UP;
    event.key.keycode = key;
    event_push(&event);
}

static IRAM_ATTR void char_cb(char c) {
    event_t event;
    event.type = EVENT_TYPE_CHAR;
    event.character.c = c;
    event_push(&event);
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_input_report_boot_t const *report, uint8_t keycode) {
    for (uint8_t i=0; i<6; i++) {
        if (report->key[i] == keycode) return true;
    }

    return false;
}

static void hid_host_keyboard_report_callback(const uint8_t *const data, const int length) {
    hid_keyboard_input_report_boot_t *report = (hid_keyboard_input_report_boot_t *)data;

    if (length < sizeof(hid_keyboard_input_report_boot_t)) return;

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t key = report->key[i];
        if (key) {
            if (keypress_time[key]) {
                if (esp_log_timestamp() - keypress_time[key] > 1000) {
                    key_cb(cc_keymap[key], true);
                    if (keycode2ascii[key][0])
                        char_cb(keycode2ascii[key][(report->modifier.val & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) ? 1 : 0]);
                }
            } else {
                key_cb(cc_keymap[key], false);
                if (keycode2ascii[key][0])
                    char_cb(keycode2ascii[key][(report->modifier.val & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) ? 1 : 0]);
                keypress_time[key] = esp_log_timestamp();
            }
        }
    }

    uint8_t mods = report->modifier.val ^ prev_report.modifier.val;
    for (uint8_t i = 0; i < 8; i++) {
        if (mods & (1 << i)) {
            if (report->modifier.val & (1 << i)) key_cb(cc_modifiers[i], false);
            else keyUp_cb(cc_modifiers[i]);
        }
    }

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t key = prev_report.key[i];
        if (key && !find_key_in_report(report, key)) {
            keyUp_cb(cc_keymap[key]);
            keypress_time[key] = 0;
        }
    }

    prev_report = *report;
}

static void hid_host_mouse_report_callback(const uint8_t *const data, const int length) {
    // TODO; do nothing
}

static void hid_host_generic_report_callback(const uint8_t *const data, const int length) {
    // do nothing
}

/**
 * @brief USB HID Host interface callback
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host interface event
 * @param[in] arg                Pointer to arguments, does not used
 */
void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                 const hid_host_interface_event_t event,
                                 void *arg)
{
    uint8_t data[64] = { 0 };
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle,
                                                                  data,
                                                                  64,
                                                                  &data_length));

        if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
            if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
                hid_host_keyboard_report_callback(data, data_length);
            } else if (HID_PROTOCOL_MOUSE == dev_params.proto) {
                hid_host_mouse_report_callback(data, data_length);
            }
        } else {
            hid_host_generic_report_callback(data, data_length);
        }

        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HID Device, protocol '%s' DISCONNECTED",
                 hid_proto_name_str[dev_params.proto]);
        ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGI(TAG, "HID Device, protocol '%s' TRANSFER_ERROR",
                 hid_proto_name_str[dev_params.proto]);
        break;
    default:
        ESP_LOGE(TAG, "HID Device, protocol '%s' Unhandled event",
                 hid_proto_name_str[dev_params.proto]);
        break;
    }
}

/**
 * @brief USB HID Host Device event
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host Device event
 * @param[in] arg                Pointer to arguments, does not used
 */
void hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                           const hid_host_driver_event_t event,
                           void *arg)
{
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        ESP_LOGI(TAG, "HID Device, protocol '%s' CONNECTED",
                 hid_proto_name_str[dev_params.proto]);

        const hid_host_device_config_t dev_config = {
            .callback = hid_host_interface_callback,
            .callback_arg = NULL
        };

        ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
        if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
            ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
            if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
                ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
            }
        }
        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        break;
    default:
        ESP_LOGD(TAG, "Unknown HID event %d received", event);
        break;
    }
}

/**
 * @brief HID Host Device callback
 *
 * Puts new HID Device event to the queue
 *
 * @param[in] hid_device_handle HID Device handle
 * @param[in] event             HID Device event
 * @param[in] arg               Not used
 */
void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                              const hid_host_driver_event_t event,
                              void *arg)
{
    const hid_event_t evt_queue = {
        // HID Host Device related info
        .handle = hid_device_handle,
        .event = event,
        .arg = arg
    };

    esp_event_post(HID_EVENT, 0, &evt_queue, sizeof(hid_event_t), portMAX_DELAY);
}

/**
 * @brief Start USB Host install and handle common USB host library events while app pin not low
 *
 * @param[in] arg  Not used
 */
static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = 0,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive(arg);

    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        // In this example, there is only one client registered
        // So, once we deregister the client, this call must succeed with ESP_OK
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
            break;
        }
    }

    ESP_LOGI(TAG, "USB shutdown");
    // Clean up USB Host
    vTaskDelay(10); // Short delay to allow clients clean-up
    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskDelete(NULL);
}

static void hid_lib_task(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    hid_event_t* event = event_data;
    hid_host_device_event(event->handle, event->event, event->arg);
}

esp_err_t hid_init(void) {
    esp_err_t err;
    ESP_LOGI(TAG, "Initializing HID module");
    /*
    * Create usb_lib_task to:
    * - initialize USB Host library
    * - Handle USB Host events while APP pin in in HIGH state
    */
    BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task,
                                           "usb_events",
                                           4096,
                                           xTaskGetCurrentTaskHandle(),
                                           2, NULL, 0);
    assert(task_created == pdTRUE);

    // Wait for notification from usb_lib_task to proceed
    ulTaskNotifyTake(false, 1000);
    hid_host_driver_config_t hid_config;
    hid_config.callback = hid_host_device_callback;
    hid_config.callback_arg = NULL;
    hid_config.core_id = tskNO_AFFINITY;
    hid_config.create_background_task = true;
    hid_config.stack_size = 3072;
    hid_config.task_priority = 5;
    CHECK_CALLE(hid_host_install(&hid_config), "Could not initialize HID");
    esp_event_handler_register(HID_EVENT, 0, hid_lib_task, NULL);
    assert(task_created == pdTRUE);
    esp_register_shutdown_handler(hid_deinit);
    return ESP_OK;
}

void hid_deinit(void) {
    esp_event_handler_unregister(HID_EVENT, 0, hid_lib_task);
    hid_host_uninstall();
}
