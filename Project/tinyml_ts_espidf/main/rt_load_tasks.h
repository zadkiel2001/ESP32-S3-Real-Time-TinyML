#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rt_experiment_config.h"
#ifdef __cplusplus
extern "C" {
#endif
bool rt_load_start_all(const rt_experiment_config_t* cfg);
void rt_load_stop_all(void);
bool rt_load_start_sensor(const rt_experiment_config_t* cfg);
bool rt_load_start_comm(const rt_experiment_config_t* cfg);
bool rt_load_start_io(const rt_experiment_config_t* cfg);
#ifdef __cplusplus
}
#endif