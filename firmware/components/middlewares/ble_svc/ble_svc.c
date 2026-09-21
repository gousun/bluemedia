#include "ble_svc.h"
#include "app_state.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

static const char *TAG = "ble_svc";

#define BLE_DEV_NAME   "BlueMedia"
#define SVC_UUID       0xFFE0
#define CHR_RX_UUID    0xFFE1    /*写命令：上位机→设备*/
#define CHR_TX_UUID    0xFFE2    /*通知：设备→上位机*/

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_attr_handle;
static uint8_t s_own_addr_type;

int gap_event_cb(struct ble_gap_event *event, void *arg);

/*---------------- 状态与通知 ----------------*/

static int build_status_json(char *buf, int cap)
{
    app_state_t st;
    app_state_get(&st);
    return snprintf(buf, cap,
                    "{\"mode\":%u,\"name\":\"%s\",\"vol\":%u,\"volok\":%d}",
                    (unsigned)(st.mode + 1), app_state_mode_name(st.mode),
                    (unsigned)st.volume, st.volume_valid ? 1 : 0);
}

/*底层通知发送（ble_att_tx 内部自持host锁，可从任意任务调用）*/
static void notify_text(const char *text, int len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(text, len);
    if (om == NULL) return;
    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om);
    if (rc != 0)
    {
        ESP_LOGD(TAG, "notify rc=%d", rc);
    }
}

void ble_svc_notify_event(const char *event)
{
    notify_text(event, (int)strlen(event));
}

void ble_svc_notify_status(void)
{
    char json[80];
    int len = build_status_json(json, sizeof(json));
    notify_text(json, len);
}

static void notify_error(void)
{
    notify_text("{\"err\":\"CMD\"}", 13);
}

/*---------------- FFE1 命令解析 ----------------
  VOL:<0-100>        上位机回报系统音量（OLED音量条）
  MODE:<1-n>         上位机直接切换模式
  NAME:<1-n>:<name>  同步自定义模式名（槽位3/4，ASCII<=6字符，OLED显示）
  STATUS             即时回报状态
  （扩展：在此加命令分支，上位机对应实现发送即可）*/

static void handle_command(char *cmd)
{
    /*去掉行尾空白/换行*/
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == '\n' ||
                       cmd[len - 1] == ' ' || cmd[len - 1] == '\t'))
    {
        cmd[--len] = '\0';
    }
    if (len == 0) return;

    ESP_LOGI(TAG, "cmd: %s", cmd);

    if (strncasecmp(cmd, "VOL:", 4) == 0)
    {
        int v = atoi(cmd + 4);
        if (v < 0 || v > 100) { notify_error(); return; }
        app_state_set_volume((uint8_t)v, true);
    }
    else if (strncasecmp(cmd, "MODE:", 5) == 0)
    {
        long m = atol(cmd + 5);
        if (m < 1 || m > MEDIA_MODE_COUNT) { notify_error(); return; }
        app_state_set_mode((uint8_t)(m - 1));
    }
    else if (strncasecmp(cmd, "NAME:", 5) == 0)
    {
        int m = atoi(cmd + 5);
        const char *sep = strchr(cmd + 5, ':');
        if (m < 1 || m > MEDIA_MODE_COUNT || sep == NULL) { notify_error(); return; }
        app_state_set_mode_name((uint8_t)(m - 1), sep + 1);
        ESP_LOGI(TAG, "mode %d name set: %s", m, app_state_mode_name((uint8_t)(m - 1)));
    }
    else if (strcasecmp(cmd, "STATUS") == 0)
    {
        /*仅要求即时回报*/
    }
    else
    {
        notify_error();
        return;
    }
    ble_svc_notify_status();
}

/*---------------- GATT ----------------*/

static int chr_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    char cmd[32] = {0};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > (uint16_t)(sizeof(cmd) - 1)) len = (uint16_t)(sizeof(cmd) - 1);
    ble_hs_mbuf_to_flat(ctxt->om, cmd, (int)sizeof(cmd) - 1, &len);
    handle_command(cmd);
    return 0;
}

static int chr_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    char json[80];
    int len = build_status_json(json, sizeof(json));
    os_mbuf_append(ctxt->om, json, len);
    return 0;
}

static const ble_uuid16_t uuid_svc = BLE_UUID16_INIT(SVC_UUID);
static const ble_uuid16_t uuid_rx  = BLE_UUID16_INIT(CHR_RX_UUID);
static const ble_uuid16_t uuid_tx  = BLE_UUID16_INIT(CHR_TX_UUID);

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &uuid_rx.u,
                .access_cb = chr_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &uuid_tx.u,
                .access_cb = chr_read_cb,
                .val_handle = &s_tx_attr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },    //特征结束
        },
    },
    { 0 },        //服务结束
};

/*---------------- GAP ----------------*/

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16 = (ble_uuid16_t[]) { BLE_UUID16_INIT(SVC_UUID) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    const char *name = ble_svc_gap_device_name();
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "adv set fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv;
    memset(&adv, 0, sizeof(adv));
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event_cb, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "adv start rc=%d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "advertising as %s", name);
    }
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "id infer rc=%d", rc);
        return;
    }
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset, reason=%d", reason);
}

int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            s_conn_handle = event->connect.conn_handle;
            app_state_set_ble_connected(true);
            ESP_LOGI(TAG, "client connected");
            ble_svc_notify_status();
        }
        else
        {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        app_state_set_ble_connected(false);
        ESP_LOGI(TAG, "client disconnected, reason=%d", event->disconnect.reason);
        start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: %d", event->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

/*---------------- NimBLE 启动 ----------------*/

static void ble_host_task(void *param)
{
    nimble_port_run();               //阻塞至 nimble_port_stop
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

void ble_svc_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svcs));

    int rc = ble_svc_gap_device_name_set(BLE_DEV_NAME);
    ESP_ERROR_CHECK(rc);

    nimble_port_freertos_init(ble_host_task);    //异步启动host，sync后开始广播
    ESP_LOGI(TAG, "BLE init done");
}
