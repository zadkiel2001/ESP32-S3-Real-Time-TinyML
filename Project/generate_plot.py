import re
from pathlib import Path
import matplotlib.pyplot as plt

FILE_PATH = "timing_results.txt"
DEADLINE_US = 10000  # 10 ms

def extract_samples(text, name):
    pattern = rf"=== BEGIN TIMING FILE: /spiffs/{name}\.txt ===(.*?)--- summary ---"
    match = re.search(pattern, text, re.S)
    if not match:
        raise ValueError(f"Could not find block for {name}")
    block = match.group(1)
    return [int(x) for x in re.findall(r"^\s*(\d+)\s*$", block, re.M)]

def ecdf(data):
    x = sorted(data)
    n = len(x)
    y = [(i + 1) / n for i in range(n)]
    return x, y

text = Path(FILE_PATH).read_text(encoding="utf-8", errors="ignore")
priority_a = extract_samples(text, "priority_A")
priority_b = extract_samples(text, "priority_B")

xa, ya = ecdf(priority_a)
xb, yb = ecdf(priority_b)

plt.figure(figsize=(7, 4.5))
plt.plot(xa, ya, linewidth=2, label="Priority-A")
plt.plot(xb, yb, linewidth=2, label="Priority-B")

# Deadline marker
plt.axvline(DEADLINE_US, linestyle="--", linewidth=1.5, color="black", label="Deadline (10 ms)")

# Logarithmic x-axis
plt.xscale("log")

plt.xlabel("Inference latency (µs, log scale)")
plt.ylabel("Cumulative probability")
plt.grid(True, alpha=0.3, which="both")
plt.legend()
plt.tight_layout()
plt.savefig("latency_ecdf_log_deadline.png", dpi=300)
plt.show()