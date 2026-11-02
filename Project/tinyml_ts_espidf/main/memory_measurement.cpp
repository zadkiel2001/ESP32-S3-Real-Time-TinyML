#include "esp_heap_caps.h"
#include "esp_log.h"

static const char* TAG = "MEM";

void log_memory(const char* stage)
{
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t min_free  = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    ESP_LOGI(TAG,
        "[%s] free_heap=%u bytes, min_free_heap=%u bytes",
        stage,
        (unsigned)free_heap,
        (unsigned)min_free);
}