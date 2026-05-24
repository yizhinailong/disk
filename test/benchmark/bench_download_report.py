#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Generate a markdown comparison report from two bench_download CSV files.

Usage:
    python bench_download_report.py before.csv after.csv [-o report.md]
"""

import argparse
import csv
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class BenchRow:
    category: str
    file_size_bytes: int
    requests: int
    total_time_s: float
    avg_latency_ms: float
    throughput_mbs: float
    errors: int


CATEGORY_ORDER = ["small", "medium", "large", "xlarge"]
CATEGORY_LABELS = {
    "small": "Small (<200KB)",
    "medium": "Medium (200KB-1MB)",
    "large": "Large (1MB-10MB)",
    "xlarge": "XLarge (>100MB)",
}


def read_csv(path: Path) -> dict[str, BenchRow]:
    rows: dict[str, BenchRow] = {}
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            r = BenchRow(
                category=row["category"],
                file_size_bytes=int(row["file_size_bytes"]),
                requests=int(row["requests"]),
                total_time_s=float(row["total_time_s"]),
                avg_latency_ms=float(row["avg_latency_ms"]),
                throughput_mbs=float(row["throughput_mbs"]),
                errors=int(row["errors"]),
            )
            rows[r.category] = r
    return rows


def pct_change(old: float, new: float) -> str:
    if old == 0:
        return "N/A"
    change = (new - old) / old * 100
    sign = "+" if change > 0 else ""
    return f"{sign}{change:.1f}%"


def fmt_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    if n < 1024 * 1024 * 1024:
        return f"{n / (1024 * 1024):.1f} MB"
    return f"{n / (1024 * 1024 * 1024):.1f} GB"


def generate_report(before_path: Path, after_path: Path) -> str:
    before = read_csv(before_path)
    after = read_csv(after_path)

    all_categories = sorted(
        set(before.keys()) | set(after.keys()),
        key=lambda c: CATEGORY_ORDER.index(c) if c in CATEGORY_ORDER else 99,
    )

    lines: list[str] = []
    lines.append("# Download Benchmark Comparison Report\n")
    lines.append(f"| Metric | Before (`{before_path.name}`) | After (`{after_path.name}`) | Change |")
    lines.append("|--------|--------|-------|--------|")

    for cat in all_categories:
        b = before.get(cat)
        a = after.get(cat)
        label = CATEGORY_LABELS.get(cat, cat)

        if b and a:
            lines.append(f"\n### {label}\n")
            lines.append(f"| Metric | Before | After | Change |")
            lines.append("|--------|--------|-------|--------|")
            lines.append(f"| File Size | {fmt_size(b.file_size_bytes)} | {fmt_size(a.file_size_bytes)} | — |")
            lines.append(f"| Requests | {b.requests} | {a.requests} | — |")
            lines.append(f"| Total Time | {b.total_time_s:.2f} s | {a.total_time_s:.2f} s | {pct_change(b.total_time_s, a.total_time_s)} |")
            lines.append(f"| Avg Latency | {b.avg_latency_ms:.2f} ms | {a.avg_latency_ms:.2f} ms | {pct_change(b.avg_latency_ms, a.avg_latency_ms)} |")
            lines.append(f"| Throughput | {b.throughput_mbs:.2f} MB/s | {a.throughput_mbs:.2f} MB/s | {pct_change(b.throughput_mbs, a.throughput_mbs)} |")
            lines.append(f"| Errors | {b.errors} | {a.errors} | — |")
        elif b:
            lines.append(f"\n### {label}\n")
            lines.append(f"**Before only** — File Size: {fmt_size(b.file_size_bytes)}, "
                         f"Throughput: {b.throughput_mbs:.2f} MB/s, Avg Latency: {b.avg_latency_ms:.2f} ms")
        elif a:
            lines.append(f"\n### {label}\n")
            lines.append(f"**After only** — File Size: {fmt_size(a.file_size_bytes)}, "
                         f"Throughput: {a.throughput_mbs:.2f} MB/s, Avg Latency: {a.avg_latency_ms:.2f} ms")

    # Summary
    lines.append("\n### Summary\n")

    common = [c for c in all_categories if c in before and c in after]
    if common:
        b_throughputs = [before[c].throughput_mbs for c in common]
        a_throughputs = [after[c].throughput_mbs for c in common]
        b_avg = sum(b_throughputs) / len(b_throughputs)
        a_avg = sum(a_throughputs) / len(a_throughputs)

        b_latencies = [before[c].avg_latency_ms for c in common]
        a_latencies = [after[c].avg_latency_ms for c in common]
        bl_avg = sum(b_latencies) / len(b_latencies)
        al_avg = sum(a_latencies) / len(a_latencies)

        lines.append(f"- **Average Throughput**: {b_avg:.2f} → {a_avg:.2f} MB/s ({pct_change(b_avg, a_avg)})")
        lines.append(f"- **Average Latency**: {bl_avg:.2f} → {al_avg:.2f} ms ({pct_change(bl_avg, al_avg)})")

        best_improvement = max(common, key=lambda c: after[c].throughput_mbs - before[c].throughput_mbs)
        lines.append(f"- **Best throughput improvement**: {CATEGORY_LABELS.get(best_improvement, best_improvement)} "
                     f"({before[best_improvement].throughput_mbs:.2f} → {after[best_improvement].throughput_mbs:.2f} MB/s)")
    else:
        lines.append("- No common categories found between before/after datasets for comparison.")

    total_errors_before = sum(before[c].errors for c in before)
    total_errors_after = sum(after[c].errors for c in after)
    lines.append(f"- **Total errors**: {total_errors_before} → {total_errors_after}")

    lines.append("")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a markdown comparison report from two bench_download CSV files.",
    )
    parser.add_argument("before", type=Path, help="CSV file with 'before' benchmark results")
    parser.add_argument("after", type=Path, help="CSV file with 'after' benchmark results")
    parser.add_argument("-o", "--output", type=Path, help="Output markdown file (default: stdout)")
    args = parser.parse_args()

    if not args.before.exists():
        print(f"ERROR: File not found: {args.before}", file=sys.stderr)
        sys.exit(1)

    if not args.after.exists():
        print(f"ERROR: File not found: {args.after}", file=sys.stderr)
        sys.exit(1)

    report = generate_report(args.before, args.after)

    if args.output:
        args.output.write_text(report)
        print(f"Report written to {args.output}")
    else:
        print(report)


if __name__ == "__main__":
    main()
