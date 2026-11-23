/* =========================================================================
 * NODE 3: RELAY + FRIEND NODE (MESH BACKBONE)
 * Mata Kuliah : Desain dan Aplikasi IoT - ITS Surabaya
 * Peran Mesh  : Relay Node + Friend Node (Wajib Terjaga & Wajib Berlistrik)
 * Hardware    : ESP32-S3 DevKit, Kabel USB 5V
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
        Serial.printf("[MESH CORE] Node 3 Provisioned (Addr: 0x%04X)\n", addr);
    }
}

// Statistik Routing Paket
static unsigned long packets_relayed = 0;
static bool friend_established = false;

// Konfigurasi Configuration Server (Mengaktifkan Relay dan Friend)
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED, // Wajib diaktifkan eksplisit
    .default_ttl = 7,
};

// Root Model
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .pid = 0x0003,
    .vid = 0x0001,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static uint8_t dev_uuid[16] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x03 };
static esp_ble_mesh_prov_t prov = {
    .uuid = dev_uuid,
};

// Callback Event Jaringan & Friendship
static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                     esp_ble_mesh_prov_cb_param_t *param) {
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        Serial.printf("[NODE 3] Terhubung ke Mesh! Unicast Address: 0x%04x\n",
                      param->node_prov_complete.addr);
        break;
    case ESP_BLE_MESH_FRIEND_FRIENDSHIP_ESTABLISH_EVT:
        friend_established = true;
        Serial.printf("\n>>> [NODE 3 - FRIEND] Friendship TERBENTUK dengan Node LPN (Addr: 0x%04x)! <<<\n\n",
                      param->frnd_friendship_establish.lpn_addr);
        break;
    case ESP_BLE_MESH_FRIEND_FRIENDSHIP_TERMINATE_EVT:
        friend_established = false;
        Serial.printf("\n>>> [NODE 3 - FRIEND] Friendship TERPUTUS dengan LPN (Addr: 0x%04x), alasan: %d <<<\n\n",
                      param->frnd_friendship_terminate.lpn_addr,
                      param->frnd_friendship_terminate.reason);
        break;
    default:
        break;
    }
}

void init_relay_friend_mesh() {
    esp_err_t err;

    err = esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    if (err != ESP_OK) {
        Serial.printf("[NODE 3] Gagal register callback: %d\n", err);
        return;
    }

    // Inisialisasi Bluetooth Controller
    BLEDevice::init("Node3-RelayFriend");

    // Inisialisasi Stack BLE Mesh
    err = esp_ble_mesh_init(&prov, &composition);
    if (err != ESP_OK) {
        Serial.printf("[NODE 3] Gagal inisialisasi BLE Mesh: %d\n", err);
        return;
    }

    // Provisioning Statis (Pre-shared key)
    if (!bt_mesh_is_provisioned()) {
        int prov_res = bt_mesh_pre_provision(dev_net_key, ESP_BLE_MESH_NET_IDX, 0, 0, ADDR_RELAY_FRIEND_NODE, dev_net_key);
        Serial.printf("[NODE 3] Static Pre-Provisioning (Addr: 0x%04X, res: %d)\n", ADDR_RELAY_FRIEND_NODE, prov_res);
        bt_mesh_provision();
    }
    bt_mesh_net_create(ESP_BLE_MESH_NET_IDX, 0, dev_net_key, 0);

    struct bt_mesh_app_key *k = bt_mesh_app_key_alloc(ESP_BLE_MESH_APP_IDX);
    if (k) {
        k->net_idx = ESP_BLE_MESH_NET_IDX;
        k->app_idx = ESP_BLE_MESH_APP_IDX;
        memcpy(k->keys[0].val, dev_app_key, 16);
    }

    Serial.println("[NODE 3] Fitur Aktif:");
    Serial.println("  -> [V] RELAY NODE  : Meneruskan paket mesh antar node.");
    Serial.println("  -> [V] FRIEND NODE : Siaga menerima permintaan Friendship dari Node 2 (LPN).");
    Serial.println("  -> [!] STATUS      : Wajib siaga berlistrik terus-menerus.");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PIN_NODE3_LED, OUTPUT);
    digitalWrite(PIN_NODE3_LED, HIGH);

    Serial.println("\n--------------------------------------------------");
    Serial.println("DESIOT ITS - NODE 3: RELAY + FRIEND NODE");
    Serial.println("--------------------------------------------------");

    init_relay_friend_mesh();
}

void loop() {
    static unsigned long last_heartbeat = 0;
    if (millis() - last_heartbeat >= 10000) {
        last_heartbeat = millis();

        // Kedipkan LED heartbeat
        digitalWrite(PIN_NODE3_LED, LOW);
        delay(80);
        digitalWrite(PIN_NODE3_LED, HIGH);

        Serial.println("--------------------------------------------------");
        Serial.printf("[NODE 3 - RELAY+FRIEND] Uptime: %lu detik\n", millis() / 1000);
        Serial.printf("  * Status Radio        : RX Scanning Selalu Aktif\n");
        Serial.printf("  * Status Friendship   : %s\n", friend_established ? "TERHUBUNG (Node 2)" : "MENUNGGU LPN");
        Serial.println("--------------------------------------------------");
    }
    delay(50);
}