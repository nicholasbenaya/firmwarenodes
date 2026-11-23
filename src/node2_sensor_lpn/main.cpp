/* =========================================================================
 * NODE 2: SENSOR KELEMBAPAN TANAH (LOW POWER NODE / LPN)
 * Mata Kuliah : Desain dan Aplikasi IoT - ITS Surabaya
 * Peran Mesh  : Low Power Node (Tidur, Bangun Berkala, Friendship dg Node 3)
 * Hardware    : ESP32-S3 DevKit, Capacitive Soil Moisture v1.2, Catu Baterai
 * ========================================================================= */

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_sleep.h>
#include <esp_ble_mesh_defs.h>
#include <esp_ble_mesh_common_api.h>
#include <esp_ble_mesh_networking_api.h>
#include <esp_ble_mesh_provisioning_api.h>
#include <esp_ble_mesh_config_model_api.h>
#include <esp_ble_mesh_low_power_api.h>
#include "mesh_config.h"
#include "mesh_wrapper.h"

extern "C" {
    int bt_mesh_pre_provision(const uint8_t net_key[16], uint16_t net_idx,
                              uint8_t flags, uint32_t iv_index, uint16_t addr,
                              const uint8_t dev_key[16]);
    void bt_mesh_provision(void);
    int bt_mesh_net_create(uint16_t net_idx, uint8_t flags, const uint8_t key[16], uint32_t iv_index);
    struct bt_mesh_app_key *bt_mesh_app_key_alloc(uint16_t app_idx);
    bool bt_mesh_is_provisioned(void);

    void bt_mesh_prov_complete(uint16_t net_idx, const uint8_t net_key[16],
                               uint16_t addr, uint8_t flags, uint32_t iv_index) {
        bt_mesh_net_create(net_idx, flags, net_key, iv_index);
        Serial.printf("[MESH CORE] Node 2 Provisioned (Addr: 0x%04X)\n", addr);
    }
}

// Opcode Vendor Model
#define ESP_BLE_MESH_VND_MODEL_OP_SET       ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_SENSOR    ESP_BLE_MESH_MODEL_OP_3(0x03, CID_ESP)

// Variabel di RTC Memory (tidak hilang saat deep sleep)
RTC_DATA_ATTR int boot_count = 0;
RTC_DATA_ATTR int last_moisture_raw = 0;
RTC_DATA_ATTR uint32_t rtc_mesh_seq = 100;

// Struktur Model Config Server
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .default_ttl = 7,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(vnd_pub, 3 + 1, ROLE_NODE);

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, 0x0002, vnd_op, &vnd_pub, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .pid = 0x0002,
    .vid = 0x0001,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static uint8_t dev_uuid[16] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x02 };
static esp_ble_mesh_prov_t prov = {
    .uuid = dev_uuid,
};

// Fungsi membaca analog kelembapan dengan averaging filter
int read_soil_moisture() {
    long sum = 0;
    const int SAMPLES = 10;
    for (int i = 0; i < SAMPLES; i++) {
        sum += analogRead(PIN_SOIL_ANALOG);
        delay(10);
    }
    return (int)(sum / SAMPLES);
}

// Mengirim perintah aktuasi OnOff ke Node 1 melalui Mesh
void send_mesh_onoff_cmd(uint8_t state) {
    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx = ESP_BLE_MESH_NET_IDX,
        .app_idx = ESP_BLE_MESH_APP_IDX,
        .addr = ADDR_PUMP_NODE, // Target: Node 1 (Pompa)
        .send_ttl = 3,
    };

    esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0], &ctx,
                                                       ESP_BLE_MESH_VND_MODEL_OP_SET,
                                                       sizeof(state), &state);
    if (err == ESP_OK) {
        Serial.printf("[NODE 2 - LPN] Berhasil mengirim perintah OnOff: %d ke Node 1 (Pompa)\n", state);
    } else {
        Serial.printf("[NODE 2 - LPN] Mengirim paket mesh (code: %d)\n", err);
    }
}

// Mengirim data telemetri ke Node 4 (Gateway)
void send_mesh_sensor_telemetry(int raw_val) {
    uint16_t val16 = (uint16_t)raw_val;
    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx = ESP_BLE_MESH_NET_IDX,
        .app_idx = ESP_BLE_MESH_APP_IDX,
        .addr = ADDR_GATEWAY_NODE, // Target: Node 4 (Gateway)
        .send_ttl = 3,
    };

    esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0], &ctx,
                                                       ESP_BLE_MESH_VND_MODEL_OP_SENSOR,
                                                       sizeof(val16), (uint8_t*)&val16);
    if (err == ESP_OK) {
        Serial.printf("[NODE 2 - LPN] Data kelembapan (%d) dikirim ke Gateway.\n", raw_val);
    }
}

// Konfigurasi Low Power Node (LPN) & Deep Sleep
void enter_deep_sleep() {
    Serial.println("[NODE 2 - LPN] Menyiapkan mode Deep Sleep untuk menghemat baterai...");
    Serial.printf("[NODE 2 - LPN] ESP32 akan tidur selama %d detik.\n", LPN_SLEEP_SEC);
    Serial.println("==================================================\n");
    Serial.flush();

    esp_sleep_enable_timer_wakeup((uint64_t)LPN_SLEEP_SEC * 1000000ULL);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    boot_count++;
    Serial.println("\n--------------------------------------------------");
    Serial.printf("DESIOT ITS - NODE 2: SENSOR KELEMBAPAN (LOW POWER NODE)\n");
    Serial.printf("Siklus Bangun (Boot Count): #%d\n", boot_count);
    Serial.println("--------------------------------------------------");

    // 1. Konfigurasi ADC Pin Sensor Kelembapan
    pinMode(PIN_SOIL_ANALOG, INPUT);
    analogReadResolution(12);

    // 2. Baca Sensor Kelembapan Tanah
    int moisture_raw = read_soil_moisture();
    last_moisture_raw = moisture_raw;

    const char* status_tanah;
    uint8_t command_pompa = 0;

    if (moisture_raw >= SOIL_DRY_THRESHOLD) {
        status_tanah = "KERING (Butuh Penyiraman)";
        command_pompa = 1;
    } else if (moisture_raw <= SOIL_WET_THRESHOLD) {
        status_tanah = "BASAH (Cukup Air)";
        command_pompa = 0;
    } else {
        status_tanah = "OPTIMAL (Normal)";
        command_pompa = 0;
    }

    Serial.printf("[NODE 2 - SENSOR] Nilai Analog: %d | Status: %s\n", moisture_raw, status_tanah);

    // 3. Inisialisasi Stack BLE Mesh
    BLEDevice::init("Node2-LPN");
    esp_err_t err = esp_ble_mesh_init(&prov, &composition);
    if (err == ESP_OK) {
        if (!bt_mesh_is_provisioned()) {
            int prov_res = bt_mesh_pre_provision(dev_net_key, ESP_BLE_MESH_NET_IDX, 0, 0, ADDR_SENSOR_NODE, dev_net_key);
            Serial.printf("[NODE 2] Static Pre-Provisioning (Addr: 0x%04X, res: %d)\n", ADDR_SENSOR_NODE, prov_res);
            bt_mesh_provision();
        }
        bt_mesh_net_create(ESP_BLE_MESH_NET_IDX, 0, dev_net_key, 0);
        struct bt_mesh_app_key *k = bt_mesh_app_key_alloc(ESP_BLE_MESH_APP_IDX);
        if (k) {
            k->net_idx = ESP_BLE_MESH_NET_IDX;
            k->app_idx = ESP_BLE_MESH_APP_IDX;
            memcpy(k->keys[0].val, dev_app_key, 16);
        }
        vnd_models[0].keys[0] = ESP_BLE_MESH_APP_IDX;
        bt_mesh.seq = rtc_mesh_seq;
        rtc_mesh_seq += 10;

        Serial.println("[NODE 2 - LPN] BLE Mesh diinisialisasi & terhubung.");
        delay(100);

        // Kirim status OnOff ke Node 1 (Pompa)
        send_mesh_onoff_cmd(command_pompa);
        delay(300);

        // Kirim telemetri ke Node 4 (Gateway)
        send_mesh_sensor_telemetry(moisture_raw);
        delay(1000);
    } else {
        Serial.printf("[NODE 2 - LPN] Init BLE Mesh gagal: %d\n", err);
    }

    // 4. Masuk ke Deep Sleep
    enter_deep_sleep();
}

void loop() {
    // Tidak digunakan karena masuk deep sleep
}
