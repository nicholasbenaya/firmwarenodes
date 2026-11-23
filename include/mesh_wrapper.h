#ifndef MESH_WRAPPER_H
#define MESH_WRAPPER_H

#include <Arduino.h>
#include <esp_ble_mesh_defs.h>
#include "mesh_config.h"

// Forward declaration of bt_mesh struct from ESP-IDF BLE Mesh core
extern "C" {
    extern struct bt_mesh_net bt_mesh;

    int bt_mesh_k4(const uint8_t n[16], uint8_t out[1]);

    static struct bt_mesh_app_key static_app_key;
    static bool static_app_key_inited = false;

    static void ensure_app_key_inited() {
        if (!static_app_key_inited) {
            static_app_key.net_idx = 0x0000;
            static_app_key.app_idx = 0x0000;
            static_app_key.updated = false;
            // Gunakan dev_app_key dari mesh_config.h secara konsisten
            memcpy(static_app_key.keys[0].val, dev_app_key, 16);
            bt_mesh_k4(dev_app_key, &static_app_key.keys[0].id);
            static_app_key_inited = true;
        }
    }

    // Bypass replay protection check untuk mengakomodasi node deep sleep yang reset sequence number
    bool __wrap_bt_mesh_rpl_check(struct bt_mesh_net_rx *rx, struct bt_mesh_rpl **match) {
        return false;
    }

    // Linker wrap interceptor for bt_mesh_subnet_get
    struct bt_mesh_subnet* __wrap_bt_mesh_subnet_get(uint16_t net_idx) {
        return &bt_mesh.sub[0];
    }

    // Linker wrap interceptor for bt_mesh_app_key_get
    struct bt_mesh_app_key* __wrap_bt_mesh_app_key_get(uint16_t app_idx) {
        ensure_app_key_inited();
        return &static_app_key;
    }

    // Linker wrap interceptor for bt_mesh_upper_key_get (called by transport.c during TX)
    int __wrap_bt_mesh_upper_key_get(const struct bt_mesh_subnet *subnet, uint16_t app_idx,
                                     const uint8_t **key, uint8_t *aid, uint16_t dst)
    {
        ensure_app_key_inited();
        if (app_idx == 0xFFFF) { // BLE_MESH_KEY_DEV
            *key = bt_mesh.dev_key;
            *aid = 0U;
            return 0;
        }

        *key = static_app_key.keys[0].val;
        *aid = static_app_key.keys[0].id;
        return 0;
    }

    size_t __wrap_bt_mesh_rx_netkey_size(void) {
        return 1;
    }

    struct bt_mesh_subnet* __wrap_bt_mesh_rx_netkey_get(size_t index) {
        return &bt_mesh.sub[0];
    }

    size_t __wrap_bt_mesh_rx_appkey_size(void) {
        return 1;
    }

    struct bt_mesh_app_key* __wrap_bt_mesh_rx_appkey_get(size_t index) {
        ensure_app_key_inited();
        return &static_app_key;
    }

    size_t __wrap_bt_mesh_rx_devkey_size(void) {
        return 1;
    }

    const uint8_t* __wrap_bt_mesh_rx_devkey_get(size_t index, uint16_t src) {
        return bt_mesh.dev_key;
    }

    const uint8_t* __wrap_bt_mesh_dev_key_get(uint16_t dst) {
        return bt_mesh.dev_key;
    }

    // Forward declarations from ESP-IDF BLE Mesh transport and composition
    int bt_mesh_trans_send(struct bt_mesh_net_tx *tx, struct net_buf_simple *msg,
                           const struct bt_mesh_send_cb *cb, void *cb_data);
    struct bt_mesh_elem *bt_mesh_model_elem(const struct bt_mesh_model *mod);
    uint8_t bt_mesh_net_transmit_get(void);

    // Linker wrap interceptor for bt_mesh_model_send
    // Bypasses static ready_to_send() which fails because CONFIG_BLE_MESH_NODE was 0 in libbt.a
    int __wrap_bt_mesh_model_send(const struct bt_mesh_model *model,
                                  struct bt_mesh_msg_ctx *ctx,
                                  struct net_buf_simple *msg,
                                  const struct bt_mesh_send_cb *cb, void *cb_data)
    {
        struct bt_mesh_net_tx tx = {
            .sub = &bt_mesh.sub[0],
            .ctx = ctx,
            .src = bt_mesh_model_elem(model)->addr,
            .xmit = bt_mesh_net_transmit_get(),
        };

        return bt_mesh_trans_send(&tx, msg, cb, cb_data);
    }
}

#endif // MESH_WRAPPER_H
