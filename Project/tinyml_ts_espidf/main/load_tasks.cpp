// load_tasks.cpp
//
// Purpose:
// - Create background FreeRTOS tasks that introduce controlled system load
// - Provide simple interference sources for timing and deadline-miss experiments
// - Allow different priority configurations to be tested against the inference task
//
// Design notes:
// - Each task is pinned to the same core as the inference task so it can interfere
//   with scheduling in a meaningful way
// - Task handles are stored globally so they can be stopped cleanly after each run
// - Frequent logging inside the load tasks is intentionally avoided because it adds
//   timing noise and distorts the experiment

#include "load_tasks.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace
{

constexpr BaseType_t kExperimentCore = 1;
constexpr uint32_t kTaskStackSize = 4096;

static const char* TAG = "LOAD";

static TaskHandle_t sensor_handle = nullptr;
static TaskHandle_t comm_handle   = nullptr;
static TaskHandle_t io_handle     = nullptr;

/**
 * @brief Simulated sensor-processing task.
 *
 * This task wakes at a short interval and performs a small CPU-bound loop to
 * emulate recurring sensor-side processing overhead.
 */
static void sensor_task(void* arg)
{
    (void)arg;

    volatile int dummy = 0;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10);

    while (true)
    {
        vTaskDelayUntil(&last_wake, period);

        for (int i = 0; i < 50000; ++i)
        {
            dummy += (i % 7);
        }

        // Block briefly after the compute phase so the idle task and other
        // lower-priority system work can run, preventing watchdog starvation.
        vTaskDelay(1);
    }
}

/**
 * @brief Simulated communication-processing task.
 *
 * This task wakes less frequently than the sensor task and performs a larger
 * amount of CPU work to emulate communication stack or packet-handling load.
 */
static void comm_task(void* arg)
{
    (void)arg;

    volatile int dummy = 0;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(20);

    while (true)
    {
        vTaskDelayUntil(&last_wake, period);

        for (int i = 0; i < 80000; ++i)
        {
            dummy += (i % 11);
        }

        // Block briefly after the compute phase so this task does not monopolize
        // the CPU when running at higher priorities during experiments.
        vTaskDelay(1);
    }
}

/**
 * @brief Simulated I/O-related task.
 *
 * This task represents lower-frequency background activity that still competes
 * for CPU time. A computation loop is used instead of frequent logging to keep
 * timing distortion low during the experiment.
 */
static void io_task(void* arg)
{
    (void)arg;

    volatile int dummy = 0;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(50);

    while (true)
    {
        vTaskDelayUntil(&last_wake, period);

        for (int i = 0; i < 30000; ++i)
        {
            dummy += (i % 13);
        }

        // Block briefly after the compute phase so that lower-priority system tasks
        // (including the idle task) still receive CPU time.
        vTaskDelay(1);
    }
}

/**
 * @brief Delete a task if its handle is valid, then clear the handle.
 */
static void delete_task_if_running(TaskHandle_t& handle)
{
    if (handle != nullptr)
    {
        vTaskDelete(handle);
        handle = nullptr;
    }
}

}  // namespace

void start_load_tasks(UBaseType_t sensor_prio,
                      UBaseType_t comm_prio,
                      UBaseType_t io_prio)
{
    // Prevent accidental duplicate task creation in the same experiment run.
    if ((sensor_handle != nullptr) || (comm_handle != nullptr) || (io_handle != nullptr))
    {
        ESP_LOGW(TAG, "Load tasks already started");
        return;
    }

    const BaseType_t sensor_ok = xTaskCreatePinnedToCore(
        sensor_task,
        "sensor_task",
        kTaskStackSize,
        nullptr,
        sensor_prio,
        &sensor_handle,
        kExperimentCore);

    const BaseType_t comm_ok = xTaskCreatePinnedToCore(
        comm_task,
        "comm_task",
        kTaskStackSize,
        nullptr,
        comm_prio,
        &comm_handle,
        kExperimentCore);

    const BaseType_t io_ok = xTaskCreatePinnedToCore(
        io_task,
        "io_task",
        kTaskStackSize,
        nullptr,
        io_prio,
        &io_handle,
        kExperimentCore);

    // If any creation fails, clean up the partial state so later runs remain valid.
    if ((sensor_ok != pdPASS) || (comm_ok != pdPASS) || (io_ok != pdPASS))
    {
        ESP_LOGE(TAG, "Failed to create one or more load tasks");

        delete_task_if_running(sensor_handle);
        delete_task_if_running(comm_handle);
        delete_task_if_running(io_handle);

        return;
    }

    ESP_LOGI(TAG,
             "Load tasks started: sensor=%u comm=%u io=%u",
             static_cast<unsigned>(sensor_prio),
             static_cast<unsigned>(comm_prio),
             static_cast<unsigned>(io_prio));
}

void stop_load_tasks(void)
{
    delete_task_if_running(sensor_handle);
    delete_task_if_running(comm_handle);
    delete_task_if_running(io_handle);

    ESP_LOGI(TAG, "Load tasks stopped");
}