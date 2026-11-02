from pathlib import Path
import re
import statistics
import math
import matplotlib.pyplot as plt


def load_samples(path: str):
    p = Path(path)
    if not p.exists():
        return None

    samples = []
    with p.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line == "sample_us":
                continue
            if line.startswith("--- summary ---"):
                break
            try:
                samples.append(int(line))
            except ValueError:
                continue

    return samples if samples else None


def parse_watchdog_log(path: str):
    p = Path(path)
    if not p.exists():
        return []

    events = []
    current = {}

    time_re = re.compile(r"E \((\d+)\) task_wdt:")
    cpu1_re = re.compile(r"CPU 1:\s+(.+)")
    cpu0_re = re.compile(r"CPU 0:\s+(.+)")

    with p.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = time_re.search(line)
            if m:
                if current:
                    events.append(current)
                current = {"time_ms": int(m.group(1)), "cpu0": None, "cpu1": None}
                continue

            m = cpu0_re.search(line)
            if m and current:
                current["cpu0"] = m.group(1).strip()
                continue

            m = cpu1_re.search(line)
            if m and current:
                current["cpu1"] = m.group(1).strip()
                continue

    if current:
        events.append(current)

    return events


def stats(samples):
    return {
        "count": len(samples),
        "mean": statistics.mean(samples),
        "std": statistics.stdev(samples) if len(samples) > 1 else 0.0,
        "min": min(samples),
        "max": max(samples),
    }


def save_ab_comparison(a_samples, b_samples, out_dir: Path):
    labels = ["PRIORITY_A", "PRIORITY_B", "PRIORITY_C"]
    means = [
        statistics.mean(a_samples),
        statistics.mean(b_samples),
        0.0,
    ]
    stds = [
        statistics.stdev(a_samples) if len(a_samples) > 1 else 0.0,
        statistics.stdev(b_samples) if len(b_samples) > 1 else 0.0,
        0.0,
    ]

    plt.figure(figsize=(8, 5))
    x = [0, 1, 2]
    plt.bar(x, means, yerr=stds, capsize=5)
    plt.xticks(x, labels)
    plt.ylabel("Execution time (us)")
    plt.title("Mean Execution Time Comparison")
    plt.grid(True, axis="y")

    ymax = max(means[:2]) + max(stds[:2]) + 20000
    plt.ylim(0, ymax)

    plt.text(
        2,
        ymax * 0.55,
        "Starvation /\nNo completed run",
        ha="center",
        va="center",
        fontsize=10,
        bbox=dict(boxstyle="round", facecolor="white", alpha=0.8),
    )

    plt.tight_layout()
    plt.savefig(out_dir / "comparison_with_starvation.png", dpi=200)
    plt.close()


def save_boxplot_ab(a_samples, b_samples, out_dir: Path):
    plt.figure(figsize=(8, 5))
    plt.boxplot([a_samples, b_samples], tick_labels=["PRIORITY_A", "PRIORITY_B"], showfliers=True)
    plt.ylabel("Execution time (us)")
    plt.title("Execution Time Distribution (Completed Runs Only)")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(out_dir / "boxplot_ab_only.png", dpi=200)
    plt.close()


def save_watchdog_timeline(events, out_dir: Path):
    if not events:
        return

    times = [e["time_ms"] / 1000.0 for e in events]

    task_map = {}
    next_y = 0

    def get_y(task_name):
        nonlocal next_y
        if task_name not in task_map:
            task_map[task_name] = next_y
            next_y += 1
        return task_map[task_name]

    ys = [get_y(e["cpu1"] if e["cpu1"] else "UNKNOWN") for e in events]

    plt.figure(figsize=(10, 5))
    plt.scatter(times, ys)
    plt.xlabel("Time since boot (s)")
    plt.ylabel("CPU1 task at watchdog")
    plt.title("Priority C Watchdog Timeline")
    plt.grid(True)

    yticks = list(task_map.values())
    ylabels = list(task_map.keys())
    plt.yticks(yticks, ylabels)

    plt.tight_layout()
    plt.savefig(out_dir / "priority_c_watchdog_timeline.png", dpi=200)
    plt.close()


def save_watchdog_counts(events, out_dir: Path):
    if not events:
        return

    counts = {}
    for e in events:
        task = e["cpu1"] if e["cpu1"] else "UNKNOWN"
        counts[task] = counts.get(task, 0) + 1

    labels = list(counts.keys())
    values = [counts[k] for k in labels]

    plt.figure(figsize=(8, 5))
    plt.bar(labels, values)
    plt.ylabel("Watchdog event count")
    plt.title("Priority C: CPU1 Task During Watchdog Events")
    plt.grid(True, axis="y")
    plt.tight_layout()
    plt.savefig(out_dir / "priority_c_watchdog_counts.png", dpi=200)
    plt.close()


def main():
    out_dir = Path("timing_plots")
    out_dir.mkdir(exist_ok=True)

    a_samples = load_samples("priority_A.txt")
    b_samples = load_samples("priority_B.txt")
    wd_events = parse_watchdog_log("outputfile.txt")

    if a_samples:
        print("PRIORITY_A:", stats(a_samples))
    if b_samples:
        print("PRIORITY_B:", stats(b_samples))
    print(f"PRIORITY_C watchdog events: {len(wd_events)}")

    if a_samples and b_samples:
        save_ab_comparison(a_samples, b_samples, out_dir)
        save_boxplot_ab(a_samples, b_samples, out_dir)

    save_watchdog_timeline(wd_events, out_dir)
    save_watchdog_counts(wd_events, out_dir)

    print(f"Saved plots to: {out_dir.resolve()}")


if __name__ == "__main__":
    main()