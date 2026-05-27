#!/usr/bin/env python3
"""
CDF 延迟分布可视化
用法：python3 plot_cdf.py results_dir/

输出：4 张图
1. Collector 不同 QPS 下的 CDF 对比
2. 有/无长尾注入的 CDF 对比
3. 不同 payload 大小的吞吐对比
4. P99 延迟热力图
"""
import os
import sys
import json
import glob
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def load_cdf(csv_file):
    """从 CSV 加载 CDF 数据"""
    latencies = []
    percentiles = []
    with open(csv_file) as f:
        next(f)  # skip header
        for line in f:
            parts = line.strip().split(",")
            if len(parts) == 2:
                latencies.append(float(parts[0]))
                percentiles.append(float(parts[1]))
    return latencies, percentiles


def plot_qps_comparison(results_dir, output):
    """不同 QPS 下的 CDF 对比"""
    fig, ax = plt.subplots(figsize=(10, 6))
    colors = ["#2ecc71", "#3498db", "#e74c3c", "#f39c12"]

    for i, qps in enumerate([1000, 5000, 10000, 15000]):
        csv_file = os.path.join(results_dir, f"collector_q{qps}_notail.csv")
        if os.path.exists(csv_file):
            lat, pct = load_cdf(csv_file)
            lat_ms = [l / 1000 for l in lat]
            ax.plot(lat_ms, pct, label=f"{qps} QPS", color=colors[i], linewidth=2)

    ax.set_xlabel("Latency (ms)", fontsize=12)
    ax.set_ylabel("Percentile", fontsize=12)
    ax.set_title("Collector Latency CDF — Different QPS", fontsize=14)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(0, None)
    plt.tight_layout()
    plt.savefig(output, dpi=150)
    print(f"Saved: {output}")
    plt.close()


def plot_tail_comparison(results_dir, output):
    """有/无长尾注入的 CDF 对比"""
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    for idx, qps in enumerate([5000, 10000]):
        ax = axes[idx]
        for suffix, label, color in [("notail", "No tail", "#3498db"), ("tail", "1% tail 5ms", "#e74c3c")]:
            csv_file = os.path.join(results_dir, f"collector_q{qps}_{suffix}.csv")
            if os.path.exists(csv_file):
                lat, pct = load_cdf(csv_file)
                lat_ms = [l / 1000 for l in lat]
                ax.plot(lat_ms, pct, label=label, color=color, linewidth=2)

        ax.set_xlabel("Latency (ms)", fontsize=11)
        ax.set_ylabel("Percentile", fontsize=11)
        ax.set_title(f"QPS={qps} — Tail Injection Impact", fontsize=12)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)
        ax.set_xlim(0, None)

    plt.tight_layout()
    plt.savefig(output, dpi=150)
    print(f"Saved: {output}")
    plt.close()


def plot_payload_scaling(results_dir, output):
    """不同 payload 大小的 P50/P99 对比"""
    sizes = []
    p50s = []
    p99s = []
    qps_actuals = []

    for size in [50, 200, 1024, 4096, 16384]:
        json_file = os.path.join(results_dir, f"collector_size{size}.json")
        if os.path.exists(json_file):
            with open(json_file) as f:
                r = json.load(f)
            sizes.append(size)
            p50s.append(r.get("p50", 0) / 1000)
            p99s.append(r.get("p99", 0) / 1000)
            qps_actuals.append(r.get("qps_actual", 0))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    ax1.plot(sizes, p50s, "o-", label="P50", color="#3498db", linewidth=2)
    ax1.plot(sizes, p99s, "s-", label="P99", color="#e74c3c", linewidth=2)
    ax1.set_xlabel("Payload Size (bytes)", fontsize=11)
    ax1.set_ylabel("Latency (ms)", fontsize=11)
    ax1.set_title("Latency vs Payload Size (QPS=5000)", fontsize=12)
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xscale("log")

    ax2.plot(sizes, qps_actuals, "o-", color="#2ecc71", linewidth=2)
    ax2.set_xlabel("Payload Size (bytes)", fontsize=11)
    ax2.set_ylabel("Actual QPS", fontsize=11)
    ax2.set_title("Throughput vs Payload Size", fontsize=12)
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale("log")

    plt.tight_layout()
    plt.savefig(output, dpi=150)
    print(f"Saved: {output}")
    plt.close()


def plot_p99_heatmap(results_dir, output):
    """P99 延迟热力图（QPS × Tail）"""
    qps_values = [1000, 5000, 10000, 15000]
    tail_values = ["notail", "tail"]
    p99_matrix = []

    for suffix in tail_values:
        row = []
        for qps in qps_values:
            json_file = os.path.join(results_dir, f"collector_q{qps}_{suffix}.json")
            if os.path.exists(json_file):
                with open(json_file) as f:
                    r = json.load(f)
                row.append(r.get("p99", 0) / 1000)
            else:
                row.append(0)
        p99_matrix.append(row)

    fig, ax = plt.subplots(figsize=(8, 4))
    im = ax.imshow(p99_matrix, cmap="YlOrRd", aspect="auto")

    ax.set_xticks(range(len(qps_values)))
    ax.set_xticklabels([f"{q} QPS" for q in qps_values])
    ax.set_yticks(range(len(tail_values)))
    ax.set_yticklabels(["No tail", "1% tail 5ms"])

    for i in range(len(tail_values)):
        for j in range(len(qps_values)):
            val = p99_matrix[i][j]
            color = "white" if val > np.max(p99_matrix) * 0.6 else "black"
            ax.text(j, i, f"{val:.1f}ms", ha="center", va="center", color=color, fontsize=11, fontweight="bold")

    ax.set_title("P99 Latency Heatmap (ms) — QPS vs Tail Injection", fontsize=13)
    plt.colorbar(im, ax=ax, label="P99 (ms)")
    plt.tight_layout()
    plt.savefig(output, dpi=150)
    print(f"Saved: {output}")
    plt.close()


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 plot_cdf.py <results_dir>")
        sys.exit(1)

    results_dir = sys.argv[1]
    if not os.path.isdir(results_dir):
        print(f"Directory not found: {results_dir}")
        sys.exit(1)

    print(f"Generating CDF plots from {results_dir}/...")

    plot_qps_comparison(results_dir, os.path.join(results_dir, "cdf_qps_comparison.png"))
    plot_tail_comparison(results_dir, os.path.join(results_dir, "cdf_tail_comparison.png"))
    plot_payload_scaling(results_dir, os.path.join(results_dir, "cdf_payload_scaling.png"))
    plot_p99_heatmap(results_dir, os.path.join(results_dir, "cdf_p99_heatmap.png"))

    print(f"\nAll plots saved to {results_dir}/")


if __name__ == "__main__":
    main()
