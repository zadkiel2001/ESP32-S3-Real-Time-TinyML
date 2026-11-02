#pragma once
#include "freertos/FreeRTOS.h"
void start_load_tasks(UBaseType_t sensor_prio,
                      UBaseType_t comm_prio,
                      UBaseType_t io_prio);
void stop_load_tasks();