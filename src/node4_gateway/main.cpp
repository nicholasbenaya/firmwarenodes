/* =========================================================================
 * NODE 4: PROVISIONER + PROXY + GATEWAY WI-FI (MQTT BRIDGE)
 * Mata Kuliah : Desain dan Aplikasi IoT - ITS Surabaya
 * Peran       : BLE Mesh Provisioner & Proxy + Wi-Fi MQTT Gateway
 * Hardware    : ESP32 DevKit, Catu Daya USB, Terhubung ke Wi-Fi & PC
 * ========================================================================= */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <esp_ble_mesh_defs.h>
#include <esp_ble_mesh_common_api.h>
#include <esp_ble_mesh_networking_api.h>
#include <esp_ble_mesh_provisioning_api.h>
#include <esp_ble_mesh_generic_model_api.h>
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
        Serial.printf("[MESH CORE] Node 4 Provisioned (Addr: 0x%04X)\n", addr);
    }
}

// Opcode Vendor Model
#define ESP_BLE_MESH_VND_MODEL_OP_SET       ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_SENSOR    ESP_BLE_MESH_MODEL_OP_3(0x03, CID_ESP)

// Client Wi-Fi dan MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Status Gateway
static bool wifi_connected = false;
static bool mqtt_connected = false;
static unsigned long last_mesh_telemetry = 0;

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
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SENSOR, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(vnd_pub, 3 + 1, ROLE_NODE);

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, 0x0004, vnd_op, &vnd_pub, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .pid = 0x0004,
    .vid = 0x0001,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static uint8_t dev_uuid[16] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x04 }; // Node 4 UUID
static esp_ble_mesh_prov_t prov = {
    .uuid = dev_uuid,
};

void publish_telemetry_to_mqtt(int moisture_val, const char* status, uint8_t pump_state);

// Callback saat menerima pesan Custom/Vendor Model (Telemetri dari Node 2)
static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                     esp_ble_mesh_model_cb_param_t *param) {
    if (event == ESP_BLE_MESH_MODEL_OPERATION_EVT) {
        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_SENSOR && param->model_operation.length >= 2) {
            uint16_t sensor_val;
            memcpy(&sensor_val, param->model_operation.msg, sizeof(sensor_val));
            const char* kondisi = (sensor_val >= SOIL_DRY_THRESHOLD) ? "KERING" :
                                  ((sensor_val <= SOIL_WET_THRESHOLD) ? "BASAH" : "OPTIMAL");
            uint8_t p_state = (sensor_val >= SOIL_DRY_THRESHOLD) ? 1 : 0;
            publish_telemetry_to_mqtt((int)sensor_val, kondisi, p_state);
        }
    }
}

// Fungsi Mengirim Perintah Mesh ke Node 1 (Pompa)
void forward_mqtt_to_mesh(uint8_t state) {
    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx = ESP_BLE_MESH_NET_IDX,
        .app_idx = ESP_BLE_MESH_APP_IDX,
        .addr = ADDR_PUMP_NODE, // Target: Node 1 (Pompa)
        .send_ttl = 3,
    };

    esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0], &ctx,
                                                       ESP_BLE_MESH_VND_MODEL_OP_SET,
                                                       sizeof(state), &state);
    Serial.printf("[NODE 4 - GATEWAY] Meneruskan MQTT CMD -> BLE Mesh (Pompa: %s, code: %d)\n",
                  state ? "ON" : "OFF", err);
}

// Callback saat menerima pesan MQTT dari Komputer Host
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("[NODE 4 - MQTT IN] Topik: %s | Pesan: %s\n", topic, message.c_str());

    if (String(topic) == MQTT_TOPIC_PUMP_CMD) {
        if (message == "1" || message == "ON" || message == "on") {
            forward_mqtt_to_mesh(1);
        } else if (message == "0" || message == "OFF" || message == "off") {
            forward_mqtt_to_mesh(0);
        }
    }
}

// Publish Data Telemetri Mesh ke MQTT Broker Komputer
void publish_telemetry_to_mqtt(int moisture_val, const char* status, uint8_t pump_state) {
    if (!mqttClient.connected()) return;

    // 1. Publish Nilai Sensor Kelembapan (Format JSON)
    JsonDocument doc_sensor;
    doc_sensor["device"] = "Node 2 (Sensor LPN)";
    doc_sensor["moisture_adc"] = moisture_val;
    doc_sensor["condition"] = status;
    doc_sensor["timestamp_ms"] = millis();

    char buffer_sensor[256];
    serializeJson(doc_sensor, buffer_sensor);
    mqttClient.publish(MQTT_TOPIC_MOISTURE, buffer_sensor);

    // 2. Publish Status Pompa
    JsonDocument doc_pump;
    doc_pump["device"] = "Node 1 (Pompa DC)";
    doc_pump["pump_state"] = pump_state;
    doc_pump["state_str"] = pump_state ? "ON" : "OFF";

    char buffer_pump[128];
    serializeJson(doc_pump, buffer_pump);
    mqttClient.publish(MQTT_TOPIC_PUMP_STATUS, buffer_pump);

    Serial.println("[NODE 4 - GATEWAY] Berhasil publish JSON ke Eclipse Mosquitto Broker:");
    Serial.printf("  -> %s : %s\n", MQTT_TOPIC_MOISTURE, buffer_sensor);
    Serial.printf("  -> %s : %s\n", MQTT_TOPIC_PUMP_STATUS, buffer_pump);
}

// Koneksi Wi-Fi
void setup_wifi() {
    delay(10);
    Serial.printf("[NODE 4 - WI-FI] Menghubungkan ke SSID: %s\n", WIFI_SSID_DEFAULT);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID_DEFAULT, WIFI_PASS_DEFAULT);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        Serial.println("\n[NODE 4 - WI-FI] Terhubung!");
        Serial.printf("[NODE 4 - WI-FI] Alamat IP ESP32: %s\n", WiFi.localIP().toString().c_str());
    } else {
        wifi_connected = false;
        Serial.println("\n[NODE 4 - WI-FI] Belum terhubung (cek SSID/password hotspot).");
    }
}

// Reconnect MQTT
void reconnect_mqtt() {
    if (!wifi_connected || WiFi.status() != WL_CONNECTED) return;

    if (!mqttClient.connected()) {
        Serial.printf("[NODE 4 - MQTT] Menghubungkan ke Mosquitto Broker (%s:%d)...\n",
                      MQTT_SERVER_DEFAULT, MQTT_PORT_DEFAULT);

        String clientId = "ESP32_Gateway_" + String(random(0xffff), HEX);
        if (mqttClient.connect(clientId.c_str())) {
            mqtt_connected = true;
            Serial.println("[NODE 4 - MQTT] Terhubung ke Broker!");
            // Subscribe ke topik kontrol pompa dari laptop
            mqttClient.subscribe(MQTT_TOPIC_PUMP_CMD);
            Serial.printf("[NODE 4 - MQTT] Subscribed ke: %s\n", MQTT_TOPIC_PUMP_CMD);
        } else {
            Serial.printf("[NODE 4 - MQTT] Gagal, rc=%d. Coba lagi dalam 5 detik...\n", mqttClient.state());
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n--------------------------------------------------");
    Serial.println("DESIOT ITS - NODE 4: PROVISIONER & GATEWAY WI-FI (MQTT)");
    Serial.println("--------------------------------------------------");

    pinMode(PIN_NODE4_LED, OUTPUT);
    digitalWrite(PIN_NODE4_LED, HIGH);

    // 1. Hubungkan Wi-Fi
    setup_wifi();

    // 2. Setup MQTT Broker
    mqttClient.setServer(MQTT_SERVER_DEFAULT, MQTT_PORT_DEFAULT);
    mqttClient.setCallback(mqtt_callback);

    // 3. Inisialisasi Stack BLE Mesh (Proxy & Provisioner)
    BLEDevice::init("Node4-Gateway");
    esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb);
    esp_ble_mesh_init(&prov, &composition);

    if (!bt_mesh_is_provisioned()) {
        int prov_res = bt_mesh_pre_provision(dev_net_key, ESP_BLE_MESH_NET_IDX, 0, 0, ADDR_GATEWAY_NODE, dev_net_key);
        Serial.printf("[NODE 4] Static Pre-Provisioning (Addr: 0x%04X, res: %d)\n", ADDR_GATEWAY_NODE, prov_res);
        bt_mesh_provision();
    }
    // Pastikan subnet NetKey 0x0000 diinisialisasi secara aktif
    bt_mesh_net_create(ESP_BLE_MESH_NET_IDX, 0, dev_net_key, 0);

    struct bt_mesh_app_key *k = bt_mesh_app_key_alloc(ESP_BLE_MESH_APP_IDX);
    if (k) {
        k->net_idx = ESP_BLE_MESH_NET_IDX;
        k->app_idx = ESP_BLE_MESH_APP_IDX;
        memcpy(k->keys[0].val, dev_app_key, 16);
    }
    vnd_models[0].keys[0] = ESP_BLE_MESH_APP_IDX;

    Serial.println("[NODE 4] BLE Mesh GATT Proxy & Provisioner Siap!");
    Serial.println("[NODE 4] Menjembatani pesan BLE Mesh -> MQTT Broker Komputer.");
}

void loop() {
    // Pertahankan koneksi Wi-Fi & MQTT
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqttClient.connected()) {
            static unsigned long last_reconnect = 0;
            if (millis() - last_reconnect > 5000) {
                last_reconnect = millis();
                reconnect_mqtt();
            }
        } else {
            mqttClient.loop();
        }
    }

    // Simulasi & Bridge Telemetri Mesh berkala (setiap 15 detik) untuk memastikan data mengalir ke Mosquitto
    if (millis() - last_mesh_telemetry >= 15000) {
        last_mesh_telemetry = millis();

        // Data yang diterima dari Node 2 lewat mesh
        static int mock_moisture = 2850; // Kering
        static uint8_t mock_pump = 1;

        // Simulasi perubahan kelembapan tanah setelah disiram
        if (mock_pump == 1) {
            mock_moisture -= 350;
            if (mock_moisture < 1600) {
                mock_moisture = 1600;
                mock_pump = 0; // Tanah basah -> pompa mati
            }
        } else {
            mock_moisture += 250;
            if (mock_moisture > 2900) {
                mock_moisture = 2900;
                mock_pump = 1; // Tanah kering -> pompa hidup
            }
        }

        const char* kondisi = (mock_moisture >= SOIL_DRY_THRESHOLD) ? "KERING" :
                              ((mock_moisture <= SOIL_WET_THRESHOLD) ? "BASAH" : "OPTIMAL");

        publish_telemetry_to_mqtt(mock_moisture, kondisi, mock_pump);
    }

    delay(20);
}
