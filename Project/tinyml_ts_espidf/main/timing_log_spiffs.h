#pragma once
#include <stdio.h>
#include "esp_spiffs.h"
#include "esp_err.h"
inline esp_err_t timing_init_spiffs() {
  esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = true
  };
  return esp_vfs_spiffs_register(&conf);
}
inline void timing_append_line(const char* path, const char* line) {
  FILE* f = fopen(path, "a");
  if (!f) return;
  fputs(line, f);
  fclose(f);
}
inline void timing_clear_file(const char* path) {
  FILE* f = fopen(path, "w");
  if (!f) return;
  fclose(f);
}