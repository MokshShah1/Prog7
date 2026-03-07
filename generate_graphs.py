import pandas as pd
import matplotlib.pyplot as plt
import os

os.makedirs("graphs", exist_ok=True)

avg_util_files = {
    "FIRST_FIT": "util_first_fit.csv",
    "BEST_FIT": "util_best_fit.csv",
    "WORST_FIT": "util_worst_fit.csv",
    "BUDDY": "util_buddy.csv",
    "MIXED": "util_mixed.csv"
}

strategies = list(avg_util_files.keys())
avg_utils = []
for s in strategies:
    df = pd.read_csv(avg_util_files[s])
    avg_util = df['utilization'].mean() * 100
    avg_utils.append(avg_util)

plt.figure(figsize=(8,5))
plt.bar(strategies, avg_utils, color='skyblue')
plt.ylabel("Average Memory Utilization (%)")
plt.title("Average Memory Utilization by Allocation Strategy")
plt.ylim(0, 100)
plt.tight_layout()
plt.savefig("graphs/average_memory_utilization.png")
plt.close()

peak_utils = []
for s in strategies:
    df = pd.read_csv(avg_util_files[s])
    peak_util = df['utilization'].max() * 100
    peak_utils.append(peak_util)

plt.figure(figsize=(8,5))
plt.bar(strategies, peak_utils, color='lightgreen')
plt.ylabel("Peak Memory Utilization (%)")
plt.title("Peak Memory Utilization by Allocation Strategy")
plt.ylim(0, 100)
plt.tight_layout()
plt.savefig("graphs/peak_memory_utilization.png")
plt.close()

speed_file = "speed_results.csv"
speed_df = pd.read_csv(speed_file)

alloc_sizes_bytes = [1, 4096, 8*1024*1024]
alloc_labels = ['1 Byte', '4 KB', '8 MB']

plt.figure(figsize=(10,6))
for s in strategies:
    subset = speed_df[speed_df['strategy'].str.upper() == s]
    malloc_times = []
    free_times = []
    for size in alloc_sizes_bytes:
        row = subset[subset['size_bytes'] == size]
        malloc_times.append(row['avg_malloc_ns'].values[0])
        free_times.append(row['avg_free_ns'].values[0])
    plt.plot(alloc_labels, malloc_times, marker='o', label=f"{s} malloc")
    plt.plot(alloc_labels, free_times, marker='x', linestyle='--', label=f"{s} free")

plt.ylabel("Time (ns)")
plt.title("Allocation and Free Times by Strategy")
plt.legend()
plt.tight_layout()
plt.savefig("graphs/speed_measurements.png")
plt.close()

print("All graphs saved in the 'graphs' folder.")