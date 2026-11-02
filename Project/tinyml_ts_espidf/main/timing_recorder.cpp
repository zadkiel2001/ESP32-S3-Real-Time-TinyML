// timing_recorder.cpp
//
// Purpose:
// - Provide a buffered timing recorder for inference experiments
// - Store raw timing samples in RAM during the hot path
// - Write samples and summary statistics to SPIFFS only once at the end
// - Reduce SPIFFS overhead and avoid watchdog issues during experiments
// - Optionally dump the saved timing file to the serial console so the PC
//   can capture it into a local text file
//
// Notes:
// - This recorder is designed for a single active experiment at a time
// - The selected output file is stored globally through g_file
// - SPIFFS is expected to be mounted before this module is used
// - Raw samples are buffered in memory to keep the measurement path lightweight

#include "timing_recorder.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "timing_stats.h"

static const char* TAG = "TIMING";

// Active output file path for the current timing run.
static const char* g_file = "/spiffs/inference_times.txt";

// In-memory statistics accumulated during the run.
static TimingStats g_stats;

// Maximum number of timing samples stored in RAM.
// Adjust if your experiments use more than 1000 samples.
constexpr size_t kMaxSamples = 2000;

// Raw timing samples buffered in RAM.
// This avoids SPIFFS writes inside the timing-critical loop.
static int64_t g_samples[kMaxSamples];
static size_t g_sample_count = 0;

/**
 * @brief Verify that SPIFFS is available before logging begins.
 *
 * This module assumes SPIFFS has already been mounted in app_main().
 *
 * @return true if SPIFFS appears usable, false otherwise
 */
bool timing_recorder_init()
{
    FILE* f = fopen("/spiffs/.probe", "a");
    if (!f)
    {
        ESP_LOGE(TAG, "SPIFFS not mounted (expected mounted in app_main)");
        return false;
    }

    fclose(f);
    return true;
}

/**
 * @brief Select the output file used by subsequent timing operations.
 *
 * @param path Path to the SPIFFS log file
 */
void timing_recorder_set_file(const char* path)
{
    if (path != nullptr)
    {
        g_file = path;
    }
}

/**
 * @brief Begin a new timing run.
 *
 * This resets in-memory statistics and clears the buffered sample list.
 * If requested, the output file is also truncated so the next run starts fresh.
 *
 * @param clear_first If true, clear the file before starting the new run
 */
void timing_recorder_begin(bool clear_first)
{
    g_stats.reset();
    g_sample_count = 0;

    if (clear_first)
    {
        FILE* f = fopen(g_file, "w");
        if (!f)
        {
            ESP_LOGE(TAG, "Failed to clear %s", g_file);
            return;
        }
        fclose(f);
    }
}

/**
 * @brief Add one timing sample in microseconds.
 *
 * The value is stored in RAM and accumulated into summary statistics.
 * No SPIFFS I/O is performed here so the hot path stays lightweight.
 *
 * @param us Measured execution time in microseconds
 */
void timing_recorder_add_sample(int64_t us)
{
    g_stats.add(us);

    if (g_sample_count < kMaxSamples)
    {
        g_samples[g_sample_count++] = us;
    }
    else
    {
        // Keep statistics even if raw sample storage is exhausted.
        // Warn only once when the buffer first overflows.
        if (g_sample_count == kMaxSamples)
        {
            ESP_LOGW(TAG, "Sample buffer full; additional raw samples will be dropped");
            g_sample_count++;
        }
    }
}

/**
 * @brief Finalize the timing run and write all data to the file.
 *
 * The output file contains:
 * - a column header for raw timing samples
 * - all buffered raw samples
 * - a summary block with count, mean, standard deviation, minimum, and maximum
 */
void timing_recorder_end()
{
    FILE* f = fopen(g_file, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open %s for final write", g_file);
        return;
    }

    fprintf(f, "sample_us\n");

    const size_t samples_to_write =
        (g_sample_count > kMaxSamples) ? kMaxSamples : g_sample_count;

    for (size_t i = 0; i < samples_to_write; ++i)
    {
        fprintf(f, "%lld\n", (long long)g_samples[i]);
    }

    fprintf(f,
            "\n--- summary ---\n"
            "count=%lu\n"
            "mean_us=%.2f\n"
            "stddev_us=%.2f\n"
            "min_us=%lld\n"
            "max_us(observed_wcet)=%lld\n",
            (unsigned long)g_stats.n,
            g_stats.mean,
            g_stats.stddev(),
            (long long)g_stats.min_us,
            (long long)g_stats.max_us);

    if (g_sample_count > kMaxSamples)
    {
        fprintf(f,
                "warning=raw sample buffer overflowed; only first %u samples saved\n",
                (unsigned)kMaxSamples);
    }

    fclose(f);
    ESP_LOGI(TAG, "Timing saved to %s", g_file);
}

/**
 * @brief Print the current timing file to stdout with delimiters.
 *
 * This format is intended for PC-side serial capture. The BEGIN/END markers
 * make it easier to extract the experiment log from monitor output.
 */
void timing_recorder_dump_to_stdout()
{
    FILE* f = fopen(g_file, "r");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open %s for dump", g_file);
        return;
    }

    printf("\n=== BEGIN TIMING FILE: %s ===\n", g_file);

    char buf[256];
    while (fgets(buf, sizeof(buf), f) != nullptr)
    {
        printf("%s", buf);

        // If the line read does not end with '\n', add one to keep
        // the serial output line-oriented and easier to parse.
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] != '\n')
        {
            printf("\n");
        }
    }

    printf("=== END TIMING FILE: %s ===\n\n", g_file);

    fclose(f);
}