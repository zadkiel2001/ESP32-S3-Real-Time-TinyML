#include "inference_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rt_load_tasks.h"
#include "rt_experiment_config.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_err.h"
static const char* TAG_MAIN = "MAIN";
/**
 * @brief Mount the SPIFFS filesystem, formatting it if needed.
 *
 * This must succeed before timing logs or experiment output files are written.
 *
 * @return true if SPIFFS was mounted successfully, false otherwise.
 */
static bool mount_spiffs_or_format()
{
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = "spiffs";
    conf.max_files = 8;
    conf.format_if_mount_failed = true;
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_MAIN, "SPIFFS register failed: %s", esp_err_to_name(err));
        return false;
    }
    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info(conf.partition_label, &total, &used);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG_MAIN, "SPIFFS mounted: total=%u used=%u",
                 (unsigned)total, (unsigned)used);
    }
    else
    {
        ESP_LOGW(TAG_MAIN, "SPIFFS info failed: %s", esp_err_to_name(err));
    }
    return true;
}
extern "C" void app_main(void)
{
    if (!mount_spiffs_or_format())
    {
        ESP_LOGE(TAG_MAIN, "Stopping because SPIFFS is unavailable");
        return;
    }
    // Load the default experiment configuration, then override selected fields
    // for the current test scenario.
    rt_experiment_config_t cfg = rt_experiment_default_config();
    // ----------------------------
    // I/O load profile
    // ----------------------------
    // High load:
    cfg.io_write_bytes = 512;
    cfg.io_period_ms = 200;
    /*
    // Low load:
    cfg.io_write_bytes = 256;
    cfg.io_period_ms = 500;
    */
    // ----------------------------
    // Additional synthetic load tuning
    // ----------------------------
    cfg.sensor_work_us = 2000;
    cfg.comm_sha_rounds = 3;
    // Start background load tasks first so inference runs under interference.
    rt_load_start_all(&cfg);
    // Start the inference experiment task.
    start_inference_task();
    // Keep app_main alive; experiment work is handled by created tasks.
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}