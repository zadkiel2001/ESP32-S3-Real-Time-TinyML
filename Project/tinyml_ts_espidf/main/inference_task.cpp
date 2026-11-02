// inference_task.cpp
//
// Purpose:
// - Run TensorFlow Lite Micro INT8 HAR inference on ESP32 as a FreeRTOS task
// - Measure inference execution time in microseconds using esp_timer_get_time()
// - Save raw timing samples and summary statistics to SPIFFS
// - Execute experiments for:
//   1) Baseline repeated inference
//   2) Periodic inference
//   3) Priority/interference scenarios
//
// Design notes:
// - Only model invocation time is recorded as the core inference execution time
// - Deadline miss detection is approximated from actual release to job completion
// - Load tasks are created and stopped per priority experiment so A/B/C runs stay isolated

#include "inference_task.h"
#include "load_tasks.h"
#include "timing_recorder.h"
#include "memory_measurement.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

// Quantized HAR model data generated for TensorFlow Lite Micro.
#include "har_int8_model_data.h"

namespace
{

    static const char *TAG = "INFER";

    /* -------------------------------------------------------------------------- */
    /* Experiment constants                                                       */
    /* -------------------------------------------------------------------------- */

    constexpr int kTensorArenaSize = 40 * 1024;
    constexpr int kNumSamplesPerExperiment = 1000;
    constexpr int64_t kDeadlineUs = 10 * 1000;
    constexpr TickType_t kPeriodTicks = pdMS_TO_TICKS(10);
    constexpr TickType_t kPostExperimentDelayTicks = pdMS_TO_TICKS(200);
    constexpr BaseType_t kInferenceCore = 1;
    constexpr uint32_t kInferenceStackSize = 12288;

    /* -------------------------------------------------------------------------- */
    /* TensorFlow Lite Micro static memory                                        */
    /* -------------------------------------------------------------------------- */

    // A fixed tensor arena is used to avoid dynamic allocation at runtime.
    static uint8_t tensor_arena[kTensorArenaSize];

    /* -------------------------------------------------------------------------- */
    /* Global TFLM runtime objects                                                */
    /* -------------------------------------------------------------------------- */

    // These objects are initialized once and then reused by all experiments.
    static const tflite::Model *g_model = nullptr;
    static tflite::MicroInterpreter *g_interpreter = nullptr;
    static TfLiteTensor *g_input = nullptr;

    /**
     * @brief Result summary for a periodic-style experiment.
     *
     * The raw per-sample inference times are still recorded through timing_recorder.
     * This struct only stores deadline-related summary information.
     */
    struct DeadlineStats
    {
        int misses = 0;
        int64_t worst_lateness_us = 0;
    };

    /**
     * @brief Run one inference and return only the measured invoke duration.
     */
    static int64_t invoke_and_measure_us()
    {
        const int64_t t0 = esp_timer_get_time();

        if (g_interpreter->Invoke() != kTfLiteOk)
        {
            MicroPrintf("Invoke failed");
        }

        const int64_t t1 = esp_timer_get_time();
        return (t1 - t0);
    }

    /**
     * @brief Run one periodic-style inference iteration and update deadline stats.
     *
     * The release time is approximated immediately after vTaskDelayUntil returns.
     */
    static void run_periodic_iteration(DeadlineStats &stats)
    {
        const int64_t release_us = esp_timer_get_time();
        const int64_t t0 = esp_timer_get_time();

        if (g_interpreter->Invoke() != kTfLiteOk)
        {
            MicroPrintf("Invoke failed");
        }

        const int64_t t1 = esp_timer_get_time();

        timing_recorder_add_sample(t1 - t0);

        const int64_t lateness_us = (t1 - release_us) - kDeadlineUs;
        if (lateness_us > 0)
        {
            ++stats.misses;
            if (lateness_us > stats.worst_lateness_us)
            {
                stats.worst_lateness_us = lateness_us;
            }
        }
    }

    /**
     * @brief Print deadline summary in a consistent format.
     */
    static void log_deadline_summary(const DeadlineStats &stats)
    {
        ESP_LOGI(TAG,
                 "Deadline: %lld us, misses=%d/%d (%.2f%%), worst_lateness=%lld us",
                 static_cast<long long>(kDeadlineUs),
                 stats.misses,
                 kNumSamplesPerExperiment,
                 100.0 * static_cast<double>(stats.misses) / static_cast<double>(kNumSamplesPerExperiment),
                 static_cast<long long>(stats.worst_lateness_us));
    }

    /**
     * @brief Initialize TensorFlow Lite Micro runtime state.
     *
     * This loads the model, registers the required operators, allocates tensors,
     * and fills the input tensor with zeros for deterministic baseline behavior.
     */
    static bool tflm_init()
    {
        g_model = tflite::GetModel(g_model_data);
        if (g_model == nullptr)
        {
            MicroPrintf("GetModel failed");
            return false;
        }

        if (g_model->version() != TFLITE_SCHEMA_VERSION)
        {
            MicroPrintf("Schema mismatch");
            return false;
        }

        // Register only operators required by the model.
        static tflite::MicroMutableOpResolver<20> resolver;
        resolver.AddConv2D();
        resolver.AddDepthwiseConv2D();
        resolver.AddFullyConnected();
        resolver.AddReshape();
        resolver.AddSoftmax();
        resolver.AddExpandDims();
        resolver.AddAveragePool2D();
        resolver.AddMaxPool2D();
        resolver.AddQuantize();
        resolver.AddDequantize();

        // Keep interpreter storage static so its lifetime spans the entire task.
        static tflite::MicroInterpreter static_interpreter(
            g_model,
            resolver,
            tensor_arena,
            kTensorArenaSize);

        g_interpreter = &static_interpreter;

        if (g_interpreter->AllocateTensors() != kTfLiteOk)
        {
            MicroPrintf("AllocateTensors failed");
            return false;
        }

        g_input = g_interpreter->input(0);
        if (g_input == nullptr)
        {
            MicroPrintf("Input tensor is null");
            return false;
        }

        // Zero input gives a deterministic and repeatable synthetic input condition.
        for (int i = 0; i < g_input->bytes; ++i)
        {
            g_input->data.int8[i] = 0;
        }

        ESP_LOGI(TAG, "TFLM initialization successful");
        return true;
    }

    /**
     * @brief Run the baseline experiment.
     *
     * This executes inference back-to-back with no intentional periodic delay.
     */
    static void run_baseline_experiment()
    {
        timing_recorder_set_file("/spiffs/inference_baseline.txt");
        timing_recorder_begin(true);

        for (int i = 0; i < kNumSamplesPerExperiment; ++i)
        {
            timing_recorder_add_sample(invoke_and_measure_us());
        }

        timing_recorder_end();
        timing_recorder_dump_to_stdout();

        ESP_LOGI(TAG, "Baseline timing saved to /spiffs/inference_baseline.txt");
    }

    /**
     * @brief Run the periodic experiment without additional interference tasks.
     */
    static void run_periodic_experiment()
    {
        // 10 ms period corresponds to 100 Hz, so the output filename reflects that rate.
        timing_recorder_set_file("/spiffs/inference_periodic_100hz.txt");
        timing_recorder_begin(true);

        TickType_t last_wake = xTaskGetTickCount();
        DeadlineStats stats{};

        for (int i = 0; i < kNumSamplesPerExperiment; ++i)
        {
            vTaskDelayUntil(&last_wake, kPeriodTicks);
            run_periodic_iteration(stats);
            if (i % 500 == 0)
            {
                log_memory("priority_run");
            }
        }

        timing_recorder_end();
        timing_recorder_dump_to_stdout();

        ESP_LOGI(TAG, "Periodic timing saved to /spiffs/inference_periodic_100hz.txt");
        log_deadline_summary(stats);
    }

    /**
     * @brief Run one interference / priority experiment.
     *
     * @param name        Human-readable experiment label for log output.
     * @param filepath    SPIFFS path for raw timing output.
     * @param sensor_prio Priority of the synthetic sensor task.
     * @param comm_prio   Priority of the synthetic communication task.
     * @param io_prio     Priority of the synthetic I/O task.
     */
    static void run_priority_experiment(const char *name,
                                        const char *filepath,
                                        UBaseType_t sensor_prio,
                                        UBaseType_t comm_prio,
                                        UBaseType_t io_prio)
    {
        ESP_LOGI(TAG, "Experiment: %s", name);

        start_load_tasks(sensor_prio, comm_prio, io_prio);

        timing_recorder_set_file(filepath);
        timing_recorder_begin(true);

        TickType_t last_wake = xTaskGetTickCount();
        DeadlineStats stats{};

        for (int i = 0; i < kNumSamplesPerExperiment; ++i)
        {
            vTaskDelayUntil(&last_wake, kPeriodTicks);
            run_periodic_iteration(stats);
        }

        timing_recorder_end();
        timing_recorder_dump_to_stdout();

        ESP_LOGI(TAG, "%s saved to %s", name, filepath);
        log_deadline_summary(stats);

        stop_load_tasks();
        log_memory("after_priority_experiment");
        // Allow a short settling interval before the next scenario begins.
        vTaskDelay(kPostExperimentDelayTicks);
    }

    /**
     * @brief FreeRTOS task entry point for all inference experiments.
     *
     * Execution flow:
     * 1) Initialize TensorFlow Lite Micro
     * 2) Initialize timing recorder
     * 3) Run baseline and periodic experiments
     * 4) Run three priority/interference scenarios
     * 5) Delete this task when the full experiment sequence is complete
     */
    static void inference_task(void *arg)
    {
        (void)arg;
        log_memory("boot");
        if (!timing_recorder_init())
        {
            ESP_LOGE(TAG, "Timing recorder init failed");
            vTaskDelete(nullptr);
            return;
        }
        log_memory("before_tflm");
        if (!tflm_init())
        {
            ESP_LOGE(TAG, "TFLM initialization failed");
            vTaskDelete(nullptr);
            return;
        }
        log_memory("after_tflm_alloc");

        // run_baseline_experiment();
        // run_periodic_experiment();

        // Priority A:
        // Inference task priority is higher than all background load tasks.
        //vTaskPrioritySet(nullptr, 5);
        //log_memory("before_priority_A");
        //run_priority_experiment("PRIORITY_A", "/spiffs/priority_A.txt", 3, 3, 2);

        // Priority B:
        // Inference task priority is reduced while sensor and communication tasks
        // are promoted to increase scheduling competition.
        //vTaskPrioritySet(nullptr, 3);
        //run_priority_experiment("PRIORITY_B", "/spiffs/priority_B.txt", 4, 4, 2);

        // Priority C:
        // Inference task priority is lowest among the tested configurations and
        // I/O interference is also strengthened.
        vTaskPrioritySet(nullptr, 2);
        run_priority_experiment("PRIORITY_C", "/spiffs/priority_C.txt", 2, 4, 3);

        vTaskDelete(nullptr);
    }

} // namespace

void start_inference_task(void)
{
    xTaskCreatePinnedToCore(
        inference_task,
        "inference_task",
        kInferenceStackSize,
        nullptr,
        5,
        nullptr,
        kInferenceCore);
}