#include "rt_load_tasks.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "mbedtls/sha256.h"
static const char* TAG = "RTLOAD";
static TaskHandle_t s_sensor_th = nullptr;
static TaskHandle_t s_comm_th   = nullptr;
static TaskHandle_t s_io_th     = nullptr;
static rt_experiment_config_t s_cfg = {};
static inline uint64_t now_us()
{
    return (uint64_t)esp_timer_get_time();
}
static void busy_wait_us(uint32_t work_us, bool yield_during_work)
{
    if (work_us == 0) return;
    const uint64_t start = now_us();
    while ((uint64_t)(now_us() - start) < (uint64_t)work_us) {
        asm volatile("nop");
        if (yield_during_work) {
            if (((uint32_t)(now_us() - start) & 0x3FFu) == 0x200u) {
                taskYIELD();
            }
        }
    }
}
static bool ensure_spiffs_mounted()
{
    FILE* f = fopen("/spiffs/.probe", "a");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}
/* ---------------- Sensor Load ---------------- */
static void sensor_load_task(void*)
{
    const uint32_t period_ms = s_cfg.sensor_period_ms;
    const uint32_t work_us   = s_cfg.sensor_work_us;
    ESP_LOGI(TAG, "SensorLoad: period=%u ms work=%u us prio=%u",
             (unsigned)period_ms,
             (unsigned)work_us,
             (unsigned)s_cfg.sensor_priority);
    TickType_t last = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
    while (true) {
        vTaskDelayUntil(&last, period_ticks);
        busy_wait_us(work_us, false);
    }
}
/* ---------------- Communication Load ---------------- */
static void comm_load_task(void*)
{
    const uint32_t period_ms   = s_cfg.comm_period_ms;
    const uint32_t payload_len = (s_cfg.comm_payload_bytes == 0) ? 256 : s_cfg.comm_payload_bytes;
    const uint32_t rounds      = (s_cfg.comm_sha_rounds == 0) ? 1 : s_cfg.comm_sha_rounds;
    ESP_LOGI(TAG,
             "CommLoad: period=%u ms payload=%u B rounds=%u extra_busy=%u us prio=%u",
             (unsigned)period_ms,
             (unsigned)payload_len,
             (unsigned)rounds,
             (unsigned)s_cfg.comm_work_us,
             (unsigned)s_cfg.comm_priority);
    uint8_t* buf = (uint8_t*)heap_caps_malloc(payload_len, MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "CommLoad malloc failed");
        vTaskDelete(nullptr);
        return;
    }
    for (uint32_t i = 0; i < payload_len; i++) {
        buf[i] = (uint8_t)(i ^ 0xA5);
    }
    TickType_t last = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
    uint8_t out[32] = {0};
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    while (true) {
        vTaskDelayUntil(&last, period_ticks);
        for (uint32_t r = 0; r < rounds; r++) {
            mbedtls_sha256_starts(&ctx, 0);
            mbedtls_sha256_update(&ctx, buf, payload_len);
            mbedtls_sha256_finish(&ctx, out);
            buf[r % payload_len] ^= out[r % sizeof(out)];
            if (s_cfg.comm_yield_during_work) {
                taskYIELD();
            }
        }
        busy_wait_us(s_cfg.comm_work_us, s_cfg.comm_yield_during_work);
    }
    mbedtls_sha256_free(&ctx);
    free(buf);
    vTaskDelete(nullptr);
}
/* ---------------- I/O Load ---------------- */
static void io_load_task(void*)
{
    ESP_LOGI(TAG,
             "IOLoad: period=%u ms write=%u B fsync=%d prio=%u",
             (unsigned)s_cfg.io_period_ms,
             (unsigned)s_cfg.io_write_bytes,
             (int)s_cfg.io_fsync_each_write,
             (unsigned)s_cfg.io_priority);
    if (!ensure_spiffs_mounted()) {
        ESP_LOGE(TAG, "SPIFFS not available");
        vTaskDelete(nullptr);
        return;
    }
    const char* path = s_cfg.io_file_path
                         ? s_cfg.io_file_path
                         : "/spiffs/io_load.bin";
    remove(path);
    const uint32_t nbytes = (s_cfg.io_write_bytes == 0)
                              ? 512
                              : s_cfg.io_write_bytes;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(nbytes, MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "IOLoad malloc failed");
        vTaskDelete(nullptr);
        return;
    }
    uint32_t seed = (uint32_t)esp_random();
    for (uint32_t i = 0; i < nbytes; i++) {
        seed = seed * 1664525u + 1013904223u;
        buf[i] = (uint8_t)(seed >> 24);
    }
    TickType_t last = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(s_cfg.io_period_ms);
    while (true) {
        vTaskDelayUntil(&last, period_ticks);
        FILE* f = fopen(path, "ab");
        if (!f)
        {
            ESP_LOGW(TAG, "IOLoad fopen failed: errno=%d (%s)", errno, strerror(errno));
            continue;
        }
        size_t w = fwrite(buf, 1, nbytes, f);
        if (w != nbytes)
        {
            ESP_LOGW(TAG, "IOLoad short write: %u/%u errno=%d (%s)",
                     (unsigned)w, (unsigned)nbytes, errno, strerror(errno));
        }
        if (s_cfg.io_fsync_each_write) {
            fflush(f);
            int fd = fileno(f);
            if (fd >= 0) fsync(fd);
        }
        fclose(f);
    }
}
/* ---------------- Public API ---------------- */
bool rt_load_start_sensor(const rt_experiment_config_t* cfg)
{
    if (s_sensor_th || !cfg || !cfg->enable_sensor_load)
        return true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        sensor_load_task,
        "SensorLoad",
        cfg->sensor_stack ? cfg->sensor_stack : 4096,
        nullptr,
        cfg->sensor_priority,
        &s_sensor_th,
        tskNO_AFFINITY
    );
    if (ok != pdPASS) {
        s_sensor_th = nullptr;
        ESP_LOGE(TAG, "SensorLoad create failed");
        return false;
    }
    return true;
}
bool rt_load_start_comm(const rt_experiment_config_t* cfg)
{
    if (s_comm_th || !cfg || !cfg->enable_comm_load)
        return true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        comm_load_task,
        "CommLoad",
        cfg->comm_stack ? cfg->comm_stack : 6144,
        nullptr,
        cfg->comm_priority,
        &s_comm_th,
        tskNO_AFFINITY
    );
    if (ok != pdPASS) {
        s_comm_th = nullptr;
        ESP_LOGE(TAG, "CommLoad create failed");
        return false;
    }
    return true;
}
bool rt_load_start_io(const rt_experiment_config_t* cfg)
{
    if (s_io_th || !cfg || !cfg->enable_io_load)
        return true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        io_load_task,
        "IOLoad",
        cfg->io_stack ? cfg->io_stack : 4096,
        nullptr,
        cfg->io_priority,
        &s_io_th,
        tskNO_AFFINITY
    );
    if (ok != pdPASS) {
        s_io_th = nullptr;
        ESP_LOGE(TAG, "IOLoad create failed");
        return false;
    }
    return true;
}
bool rt_load_start_all(const rt_experiment_config_t* cfg)
{
    if (!cfg)
        return false;
    s_cfg = *cfg;
    bool ok = true;
    ok = rt_load_start_sensor(cfg) && ok;
    ok = rt_load_start_comm(cfg) && ok;
    ok = rt_load_start_io(cfg) && ok;
    return ok;
}
void rt_load_stop_all(void)
{
    if (s_sensor_th) {
        vTaskDelete(s_sensor_th);
        s_sensor_th = nullptr;
    }
    if (s_comm_th) {
        vTaskDelete(s_comm_th);
        s_comm_th = nullptr;
    }
    if (s_io_th) {
        vTaskDelete(s_io_th);
        s_io_th = nullptr;
    }
    ESP_LOGI(TAG, "RT load tasks stopped");
}