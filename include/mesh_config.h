#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

#include <Arduino.h>

/* =========================================================================
 * DESIOT - DESAIN DAN APLIKASI IOT (ITS SURABAYA)
 * KONFIGURASI JARINGAN BLE MESH & MQTT GATEWAY
 * ========================================================================= */

// --- BLE Mesh Identifiers ---
#define CID_ESP                 0x02E5 // Espressif Company ID
#define ESP_BLE_MESH_NET_IDX    0x0000
#define ESP_BLE_MESH_APP_IDX    0x0000

// --- Unicast & Group Addresses ---
#define ADDR_GATEWAY_NODE       0x0001 // Node 4: Provisioner & Proxy Gateway
#define ADDR_PUMP_NODE          0x0002 // Node 1: Generic OnOff Server (Pompa DC)
#define ADDR_SENSOR_NODE        0x0003 // Node 2: Low Power Node (Sensor Kelembapan)
#define ADDR_RELAY_FRIEND_NODE  0x0004 // Node 3: Relay + Friend Node
#define ADDR_ALL_NODES_GROUP    0xC001 // Group Address untuk broadcast

// --- Kunci Keamanan Jaringan (Pre-shared NetKey & AppKey) ---
// Catatan: Kunci 128-bit (16 bytes) standar BLE Mesh
static const uint8_t dev_net_key[16] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
};

static const uint8_t dev_app_key[16] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99
};

// --- Pinout Hardware Tiap Node (Disesuaikan untuk ESP32-S3) ---
// Node 1: Pompa Air DC
#define PIN_RELAY_PUMP          10 // Output GPIO pemicu Modul Relay 5V (GPIO 10 pada ESP32-S3)
#define PIN_NODE1_LED           2  // Indikator status onboard LED

// Node 2: Sensor Kelembapan Tanah (LPN)
// Catatan: Pada ESP32-S3, channel ADC1 adalah GPIO 1 - 10. GPIO 4 adalah ADC1_CH3.
#define PIN_SOIL_ANALOG         4    // Input ADC1_CH3 membaca Capacitive Sensor v1.2
#define SOIL_DRY_THRESHOLD      2100 // ADC > 2100 = Kering (Sensor di udara terbaca ~2520, butuh disiram)
#define SOIL_WET_THRESHOLD      1800 // ADC < 1800 = Basah (Dicelup air, cukup air)
#define LPN_SLEEP_SEC           15   // Durasi deep-sleep berkala (15 detik untuk demo, 5 detik untuk quick test)

// Node 3: Relay & Friend Node
#define PIN_NODE3_LED           2  // Indikator aktif selalu terjaga

// Node 4: Gateway Wi-Fi & MQTT
#define PIN_NODE4_LED           2

// --- Konfigurasi Jaringan Wi-Fi & MQTT (Node 4) ---
#define WIFI_SSID_DEFAULT       "athahahaha"
#define WIFI_PASS_DEFAULT       "rizkiinrahim"
#define MQTT_SERVER_DEFAULT     "10.189.144.245" // Ganti dengan IP Laptop Host
#define MQTT_PORT_DEFAULT       1883

// Topik MQTT untuk Komputer Host (Eclipse Mosquitto)
#define MQTT_TOPIC_MOISTURE     "desiot/soil/moisture"
#define MQTT_TOPIC_PUMP_STATUS  "desiot/pump/status"
#define MQTT_TOPIC_PUMP_CMD     "desiot/pump/command"
#define MQTT_TOPIC_TELEMETRY    "desiot/mesh/telemetry"

#endif // MESH_CONFIG_H
