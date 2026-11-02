import matplotlib.pyplot as plt

scenarios = ["Priority-A", "Priority-B"]
miss_ratio = [1.50, 59.00]

plt.figure(figsize=(4.6, 3.2))
bars = plt.bar(scenarios, miss_ratio)

plt.ylabel("Deadline miss ratio (%)")
plt.xlabel("Scheduling configuration")
plt.ylim(0, 100)

for bar, value in zip(bars, miss_ratio):
    plt.text(
        bar.get_x() + bar.get_width() / 2,
        value + 2,
        f"{value:.2f}%",
        ha="center",
        va="bottom",
        fontsize=9
    )

plt.tight_layout()
plt.savefig("deadline_miss_ratio.png", dpi=300, bbox_inches="tight")
plt.show()