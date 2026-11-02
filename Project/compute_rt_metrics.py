from pathlib import Path
import math
import statistics
import csv


DEADLINE_US = 10000


def load_samples(path: str):
    p = Path(path)
    if not p.exists():
        print(f"[WARN] File not found: {path}")
        return None

    samples = []
    with p.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()

            if not line:
                continue
            if line == "sample_us":
                continue
            if line.startswith("--- summary ---"):
                break

            try:
                samples.append(int(line))
            except ValueError:
                continue

    if not samples:
        print(f"[WARN] No samples found in: {path}")
        return None

    return samples


def percentile(sorted_data, p):
    if not sorted_data:
        return None
    if len(sorted_data) == 1:
        return sorted_data[0]

    k = (len(sorted_data) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)

    if f == c:
        return sorted_data[int(k)]

    d0 = sorted_data[f] * (c - k)
    d1 = sorted_data[c] * (k - f)
    return d0 + d1


def compute_metrics(samples, deadline_us):
    n = len(samples)
    sorted_samples = sorted(samples)

    mean_us = statistics.mean(samples)
    stddev_us = statistics.stdev(samples) if n > 1 else 0.0
    min_us = min(samples)
    max_us = max(samples)

    lateness_list = [s - deadline_us for s in samples]
    missed_lateness = [x for x in lateness_list if x > 0]
    misses = len(missed_lateness)
    miss_rate = (misses / n) * 100.0 if n else 0.0

    avg_lateness_missed = statistics.mean(missed_lateness) if missed_lateness else 0.0
    worst_lateness = max(missed_lateness) if missed_lateness else 0.0

    metrics = {
        "count": n,
        "deadline_us": deadline_us,
        "mean_us": mean_us,
        "stddev_us": stddev_us,
        "min_us": min_us,
        "max_us": max_us,
        "deadline_misses": misses,
        "miss_rate_percent": miss_rate,
        "avg_lateness_missed_us": avg_lateness_missed,
        "worst_lateness_us": worst_lateness,
        "p90_us": percentile(sorted_samples, 90),
        "p95_us": percentile(sorted_samples, 95),
        "p99_us": percentile(sorted_samples, 99),
        "p99_9_us": percentile(sorted_samples, 99.9),
    }

    return metrics, lateness_list, missed_lateness


def print_metrics(name, metrics):
    print(f"\n=== {name} ===")
    for k, v in metrics.items():
        if isinstance(v, float):
            print(f"{k:24s} = {v:.2f}")
        else:
            print(f"{k:24s} = {v}")


def save_summary_csv(results, out_path: Path):
    fieldnames = [
        "experiment",
        "count",
        "deadline_us",
        "mean_us",
        "stddev_us",
        "min_us",
        "max_us",
        "deadline_misses",
        "miss_rate_percent",
        "avg_lateness_missed_us",
        "worst_lateness_us",
        "p90_us",
        "p95_us",
        "p99_us",
        "p99_9_us",
    ]

    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for experiment, metrics in results.items():
            row = {"experiment": experiment}
            row.update(metrics)
            writer.writerow(row)


def save_lateness_csv(experiment_name, lateness_list, out_dir: Path):
    out_path = out_dir / f"{experiment_name.lower()}_lateness.csv"
    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["sample_index", "lateness_us"])
        for i, value in enumerate(lateness_list, start=1):
            writer.writerow([i, value])


def save_missed_jobs_csv(experiment_name, samples, deadline_us, out_dir: Path):
    out_path = out_dir / f"{experiment_name.lower()}_missed_jobs.csv"
    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["sample_index", "execution_time_us", "lateness_us"])

        for i, s in enumerate(samples, start=1):
            lateness = s - deadline_us
            if lateness > 0:
                writer.writerow([i, s, lateness])


def main():
    files = {
        "PRIORITY_A": "priority_A.txt",
        "PRIORITY_B": "priority_B.txt",
        "PRIORITY_C": "priority_C.txt",  # optional
    }

    out_dir = Path("rt_metrics")
    out_dir.mkdir(exist_ok=True)

    results = {}

    for name, path in files.items():
        samples = load_samples(path)
        if samples is None:
            continue

        metrics, lateness_list, missed_lateness = compute_metrics(samples, DEADLINE_US)
        results[name] = metrics

        print_metrics(name, metrics)
        save_lateness_csv(name, lateness_list, out_dir)
        save_missed_jobs_csv(name, samples, DEADLINE_US, out_dir)

    if results:
        save_summary_csv(results, out_dir / "rt_summary_table.csv")
        print(f"\nSaved CSV outputs to: {out_dir.resolve()}")
    else:
        print("\nNo valid timing files loaded.")


if __name__ == "__main__":
    main()