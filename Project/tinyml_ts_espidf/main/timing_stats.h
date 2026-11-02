#pragma once
#include <stdint.h>
#include <math.h>
struct TimingStats {
  uint32_t n = 0;
  double mean = 0.0;   // running mean
  double m2 = 0.0;     // running sum of squares of diffs
  int64_t min_us = INT64_MAX;
  int64_t max_us = 0;
  void reset() {
    n = 0; mean = 0.0; m2 = 0.0;
    min_us = INT64_MAX; max_us = 0;
  }
  void add(int64_t x_us) {
    n++;
    if (x_us < min_us) min_us = x_us;
    if (x_us > max_us) max_us = x_us;
    // Welford's algorithm (stable running variance)
    double x = (double)x_us;
    double delta = x - mean;
    mean += delta / (double)n;
    double delta2 = x - mean;
    m2 += delta * delta2;
  }
  double variance() const {
    return (n > 1) ? (m2 / (double)(n - 1)) : 0.0;
  }
  double stddev() const {
    return sqrt(variance());
  }
  int64_t wcet_us() const { return max_us; } // observed max
};