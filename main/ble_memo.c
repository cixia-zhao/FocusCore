#include "ble_memo.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nimble/ble.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/*
 * Memo characteristic payload (not handled as TIME:...):
 * Optional pipe-separated list of up to BLE_MEMO_MAX_TODO_LINES rows.
 * Each row may use Obsidian task prefixes: "[ ]" incomplete, "[x]" completed
 * (strikethrough applied on the home screen).
 */

static const char *TAG = "BLE_MEMO";
static QueueHandle_t s_memo_queue = NULL;
static uint8_t s_addr_type;
/** When true, gap events may re-apply advertising after disconnect / adv-complete. */
static volatile bool s_adv_wanted = false;

void ble_memo_split_todo_payload(const char *memo, char out_lines[BLE_MEMO_MAX_TODO_LINES][BLE_MEMO_MAX_LEN])
{
    for(int i = 0; i < BLE_MEMO_MAX_TODO_LINES; ++i) {
        out_lines[i][0] = '\0';
    }
    if(memo == NULL || memo[0] == '\0') {
        return;
    }

    char local_buf[BLE_MEMO_MAX_LEN];
    strncpy(local_buf, memo, sizeof(local_buf) - 1);
    local_buf[sizeof(local_buf) - 1] = '\0';

    char *ctx = NULL;
    char *token = strtok_r(local_buf, "|", &ctx);
    int idx = 0;
    while(token != NULL && idx < BLE_MEMO_MAX_TODO_LINES) {
        strncpy(out_lines[idx], token, BLE_MEMO_MAX_LEN - 1);
        out_lines[idx][BLE_MEMO_MAX_LEN - 1] = '\0';
        ++idx;
        token = strtok_r(NULL, "|", &ctx);
    }
}

static const ble_uuid128_t kMemoSvcUuid =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xcd, 0xab);
static const ble_uuid128_t kMemoChrUuid =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xcd, 0xab);

static int GapEventCb(struct ble_gap_event *event, void *arg);

static int MemoCharAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t pkt_len = OS_MBUF_PKTLEN(ctxt->om);
    if (pkt_len == 0) {
        return 0;
    }

    ble_memo_msg_t msg = {0};
    uint16_t copy_len = pkt_len;
    if (copy_len >= BLE_MEMO_MAX_LEN) {
        copy_len = BLE_MEMO_MAX_LEN - 1;
    }

    int rc = ble_hs_mbuf_to_flat(ctxt->om, msg.text, copy_len, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_mbuf_to_flat failed: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }
    msg.len = copy_len;
    msg.text[copy_len] = '\0';

    if (s_memo_queue != NULL) {
        if (xQueueSend(s_memo_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "memo queue full, drop message");
        }
    }

    ESP_LOGI(TAG, "memo write received len=%u", (unsigned)copy_len);
    return 0;
}

static const struct ble_gatt_chr_def kMemoChrDefs[] = {
    {
        &kMemoChrUuid.u,                                   /* uuid */
        MemoCharAccessCb,                                  /* access_cb */
        NULL,                                              /* arg */
        NULL,                                              /* descriptors */
        BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP, /* flags */
        0,                                                 /* min_key_size */
        NULL,                                              /* val_handle */
        NULL                                               /* cpfd */
    },
    {0}
};

static const struct ble_gatt_svc_def kGattSvcs[] = {
    {
        BLE_GATT_SVC_TYPE_PRIMARY, /* type */
        &kMemoSvcUuid.u,           /* uuid */
        NULL,                      /* includes */
        kMemoChrDefs               /* characteristics */
    },
    {0}
};

/** Apply ADV + scan response and start advertising (caller ensures !ble_gap_adv_active()). */
static int ApplyAdvFieldsAndStart(void)
{
    /* Advertising payload max is 31 bytes; keep UUIDs in ADV and put name in scan response. */
    struct ble_hs_adv_fields adv_fields;
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.uuids128 = &kMemoSvcUuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return rc;
    }

    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (const uint8_t *)"FocusCore_BLE";
    rsp_fields.name_len = (uint8_t)strlen("FocusCore_BLE");
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return rc;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv_params, GapEventCb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return rc;
    }
    ESP_LOGI(TAG, "Advertising started");
    return 0;
}

static int GapEventCb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                if (s_adv_wanted && !ble_gap_adv_active() && !ble_gap_conn_active()) {
                    (void)ApplyAdvFieldsAndStart();
                }
            } else {
                ESP_LOGI(TAG, "BLE connected");
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            if (s_adv_wanted && !ble_gap_adv_active() && !ble_gap_conn_active()) {
                (void)ApplyAdvFieldsAndStart();
            }
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (s_adv_wanted && !ble_gap_adv_active() && !ble_gap_conn_active()) {
                (void)ApplyAdvFieldsAndStart();
            }
            return 0;
        default:
            return 0;
    }
}

static void OnSync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    /* On-demand advertising only (ble_memo_start_adv); do not advertise at boot. */
    ESP_LOGI(TAG, "NimBLE host synced; advertising idle until ble_memo_start_adv()");
}

esp_err_t ble_memo_start_adv(void)
{
    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "ble_memo_start_adv: host not synced yet");
        return ESP_ERR_INVALID_STATE;
    }
    s_adv_wanted = true;
    if (ble_gap_conn_active()) {
        ESP_LOGI(TAG, "ble_memo_start_adv: connection active (adv resumes on disconnect if still wanted)");
        return ESP_OK;
    }
    if (ble_gap_adv_active()) {
        return ESP_OK;
    }
    const int rc = ApplyAdvFieldsAndStart();
    if (rc != 0) {
        s_adv_wanted = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ble_memo_stop_adv(void)
{
    s_adv_wanted = false;
    if (!ble_gap_adv_active()) {
        return ESP_OK;
    }
    const int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_adv_stop returned %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Advertising stopped");
    return ESP_OK;
}

static void HostTask(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_memo_init(QueueHandle_t memo_queue)
{
    if (memo_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_memo_queue = memo_queue;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb = OnSync;
    ble_att_set_preferred_mtu(256);

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ret = ble_svc_gap_device_name_set("FocusCore_BLE");
    if (ret != ESP_OK) {
        return ret;
    }

    int rc = ble_gatts_count_cfg(kGattSvcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(kGattSvcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(HostTask);
    ESP_LOGI(TAG, "NimBLE memo service initialized");
    return ESP_OK;
}
