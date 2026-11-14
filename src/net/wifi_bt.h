#ifndef FLUX_WIFI_BT_H
#define FLUX_WIFI_BT_H

#include "types.h"

typedef enum {
    WIFI_STATE_FIRMWARE_READY = 0, /* Intel iwlwifi & Qualcomm Atheros ath11k: works immediately */
    WIFI_STATE_NEEDS_DKMS     = 1, /* Realtek USB & Broadcom STA: requires DKMS package */
    WIFI_STATE_CONNECTED      = 2  /* Associated with WPA3-SAE / WPA2-PSK 5GHz/6GHz AP */
} wifi_adapter_state_t;

typedef struct {
    char                 ifname[16];
    char                 chip_model[48];
    char                 driver_name[24];
    char                 connected_ssid[32];
    uint16_t             vendor_id;
    uint16_t             device_id;
    uint16_t             freq_mhz;
    int8_t               rssi_dbm;
    uint32_t             tx_rate_mbps;
    bool                 builtin_kernel;
    wifi_adapter_state_t state;
} wifi_adapter_info_t;

typedef struct {
    char     mac_str[20];
    char     name[32];
    char     profile[16]; /* "A2DP Sink", "BLE HID", "LE Audio" */
    uint16_t class_of_device;
    bool     paired;
    bool     connected;
    int8_t   rssi_dbm;
} bt_device_info_t;

void   wifi_bt_init(void);
size_t wifi_adapter_count(void);
bool   wifi_get_adapter(size_t idx, wifi_adapter_info_t *out);
bool   wifi_activate_dkms_adapters(void);
size_t bt_device_count(void);
bool   bt_get_device(size_t idx, bt_device_info_t *out);
bool   bt_connect_device(size_t idx);
size_t wifi_regdomain_channel_count(void);
bool   wifi_bt_selftest(void);

#endif
