/* =========================================================================
 * NODE 1: POMPA AIR DC (VENDOR SERVER MODEL - ACTIVE-LOW RELAY)
 * Mata Kuliah : Desain dan Aplikasi IoT - ITS Surabaya
 * Peran Mesh  : BLE Mesh Node Siaga (Always Listening)
 * Hardware    : ESP32-S3 DevKit, Modul Relay 1-Channel 5V, Pompa Air DC 3-5V
 * ========================================================================= */

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_ble_mesh_defs.h>
#include <esp_ble_mesh_common_api.h>
#include <esp_ble_mesh_networking_api.h>
#include <esp_ble_mesh_provisioning_api.h>
#include <esp_ble_mesh_config_model_api.h>
#include "mesh_config.h"
#include "mesh_wrapper.h"

// Opcode Vendor Model untuk Kontrol Pompa & Telemetri
#define ESP_BLE_MESH_VND_MODEL_OP_SET       ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_SENSOR    ESP_BLE_MESH_MODEL_OP_3(0x03, CID_ESP)

// State pompa (0 = MATI, 1 = HIDUP)
static uint8_t pump_state = 0;

// Deklarasi fungsi
void set_pump(uint8_t state, const char* source);
void init_ble_mesh();

// Model Config Server (Standar SIG)
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

// Model Vendor Server untuk Node 1 (Menerima perintah Set On/Off)
static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SET, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(vnd_pub, 3 + 1, ROLE_NODE);

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, 0x0001, vnd_op, &vnd_pub, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .pid = 0x0001,
    .vid = 0x0001,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

// Konfigurasi Provisioning
static uint8_t dev_uuid[16] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x01 };
static esp_ble_mesh_prov_t prov = {
    .uuid = dev_uuid,
};

// Callback saat ada event provisioning
static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                     esp_ble_mesh_prov_cb_param_t *param) {
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        Serial.printf("[NODE 1] Provisioning Selesai! Unicast Addr: 0x%04x\n",
                      param->node_prov_complete.addr);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        Serial.println("[NODE 1] Node di-reset dari jaringan Mesh.");
        break;
    default:
        break;
    }
}

// Callback saat menerima pesan Custom/Vendor Model
static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                     esp_ble_mesh_model_cb_param_t *param) {
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_SET) {
            uint8_t new_state = param->model_operation.msg[0];
            set_pump(new_state, "BLE Mesh Vendor Command");
        }
        break;
    default:
        break;
    }
}

void set_pump(uint8_t state, const char* source) {
    pump_state = state ? 1 : 0;
    
    // MODUL RELAY ACTIVE-LOW:
    // pump_state = 1 (ON)  --> Kirim LOW  (Relay Menarik / Menyala)
    // pump_state = 0 (OFF) --> Kirim HIGH (Relay Lepas / Mati)
    digitalWrite(PIN_RELAY_PUMP, pump_state ? LOW : HIGH);
    digitalWrite(PIN_NODE1_LED, pump_state ? HIGH : LOW);

    Serial.println("==================================================");
    Serial.printf("[NODE 1 - POMPA] Status Pompa: %s (Sumber: %s)\n",
                  pump_state ? ">>> MENYALA (ON) <<<" : ">>> PADAM (OFF) <<<", source);
    Serial.printf("[NODE 1 - POMPA] GPIO %d Signal Level: %s (Active-LOW Relay)\n",
                  PIN_RELAY_PUMP, pump_state ? "LOW (0V)" : "HIGH (3.3V)");
    Serial.println("==================================================");
}

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
        Serial.printf("[MESH CORE] Provisioning Complete Event (Addr: 0x%04X)\n", addr);
    }
}

void init_ble_mesh() {
    esp_err_t err;

    // 1. Inisialisasi Stack Bluetooth (Controller & Bluedroid) via BLEDevice
    BLEDevice::init("Node1-Pump");

    // 2. Inisialisasi BLE Mesh
    err = esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    if (err != ESP_OK) {
        Serial.printf("[NODE 1] Gagal register prov callback: %d\n", err);
        return;
    }

    err = esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb);
    if (err != ESP_OK) {
        Serial.printf("[NODE 1] Gagal register custom model callback: %d\n", err);
        return;
    }

    err = esp_ble_mesh_init(&prov, &composition);
    if (err != ESP_OK) {
        Serial.printf("[NODE 1] Gagal inisialisasi BLE Mesh: %d\n", err);
        return;
    }

    if (!bt_mesh_is_provisioned()) {
        int prov_res = bt_mesh_pre_provision(dev_net_key, ESP_BLE_MESH_NET_IDX, 0, 0, ADDR_PUMP_NODE, dev_net_key);
        Serial.printf("[NODE 1] Static Pre-Provisioning (Addr: 0x%04X, res: %d)\n", ADDR_PUMP_NODE, prov_res);
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

    Serial.println("[NODE 1] BLE Mesh Node 1 (Pompa Server) Berhasil Dimulai!");
    Serial.println("[NODE 1] Node siaga mendengar perintah On/Off dari Mesh.");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n--------------------------------------------------");
    Serial.println("DESIOT ITS - NODE 1: POMPA AIR DC (ONOFF SERVER)");
    Serial.println("Modul Relay: Active-LOW Mode (1 = Nyala, 0 = Mati)");
    Serial.println("--------------------------------------------------");

    // Konfigurasi Pin Relay dan LED
    pinMode(PIN_RELAY_PUMP, OUTPUT);
    pinMode(PIN_NODE1_LED, OUTPUT);
    
    // Awal: Pompa Mati (Active-LOW butuh HIGH untuk mati)
    digitalWrite(PIN_RELAY_PUMP, HIGH);
    digitalWrite(PIN_NODE1_LED, LOW);

    // Inisialisasi Stack Bluetooth & Mesh
    init_ble_mesh();

    Serial.println("[NODE 1] Tip Debug: Ketik '1' (ON) atau '0' (OFF) di Serial Monitor untuk uji manual relay.");
}

void loop() {
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == '1') {
            set_pump(1, "Serial Manual Override");
        } else if (ch == '0') {
            set_pump(0, "Serial Manual Override");
        }
    }
    delay(50);
}
