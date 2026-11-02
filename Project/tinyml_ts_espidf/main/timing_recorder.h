#pragma once
#include <stdbool.h>
#include <stdint.h>
bool timing_recorder_init();                       // mounts SPIFFS once
void timing_recorder_set_file(const char* path);   // choose output file
void timing_recorder_begin(bool clear_first);      // reset stats + write header
void timing_recorder_add_sample(int64_t us);       // append sample + update stats
void timing_recorder_end();                        // write summary
void timing_recorder_dump_to_stdout();