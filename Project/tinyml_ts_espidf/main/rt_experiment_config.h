#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
typedef struct {
    bool enable_sensor_load;
    bool enable_comm_load;
    bool enable_io_load;
    // Sensor-like periodic CPU load
    uint32_t sensor_period_ms;
    uint32_t sensor_work_us;
    UBaseType_t sensor_priority;
    uint32_t sensor_stack;
    // Communication-like load (CPU / crypto heavy)
    uint32_t comm_period_ms;
    uint32_t comm_work_us;
    uint32_t comm_payload_bytes;
    uint32_t comm_sha_rounds;
    bool comm_yield_during_work;
    UBaseType_t comm_priority;
    uint32_t comm_stack;
    // SPIFFS I/O load
    uint32_t io_period_ms;
    uint32_t io_write_bytes;
    bool io_fsync_each_write;
    const char* io_file_path;
    UBaseType_t io_priority;
    uint32_t io_stack;
} rt_experiment_config_t;
static inline rt_experiment_config_t rt_experiment_default_config()
{
    rt_experiment_config_t c = {};
    c.enable_sensor_load = true;
    c.enable_comm_load   = true;
    c.enable_io_load     = true;
    c.sensor_period_ms = 10;
    c.sensor_work_us   = 2000;
    c.sensor_priority  = 8;
    c.sensor_stack     = 4096;
    c.comm_period_ms         = 200;
    c.comm_work_us           = 1000;
    c.comm_payload_bytes     = 256;
    c.comm_sha_rounds        = 3;
    c.comm_yield_during_work = true;
    c.comm_priority          = 10;
    c.comm_stack             = 6144;
    c.io_period_ms        = 100;
    c.io_write_bytes      = 1024;
    c.io_fsync_each_write = false;
    c.io_file_path        = "/spiffs/io_load.bin";
    c.io_priority         = 6;
    c.io_stack            = 4096;
    return c;
}