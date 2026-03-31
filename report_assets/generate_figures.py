from __future__ import annotations

import csv
import signal
import subprocess
import sys
import time
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


ASSET_DIR = Path(__file__).resolve().parent
CODE_DIR = ASSET_DIR.parent
TRACE_SRC = ASSET_DIR / "probe_trace.c"
TRACE_BIN = ASSET_DIR / "probe_trace"
TRACE_CSV = ASSET_DIR / "probe_trace.csv"
TRACE_PNG = ASSET_DIR / "probe_trace.png"
BANDWIDTH_CSV = ASSET_DIR / "bandwidth_sweep.csv"
BANDWIDTH_PNG = ASSET_DIR / "bandwidth_sweep.png"

TRACE_MESSAGE = "Packetized covert channels survive duplicates and CRC checks."
TRACE_PACKET_BYTES = 16
TRACE_REPETITIONS = 1


def run(cmd: list[str], cwd: Path | None = None) -> None:
    subprocess.run(cmd, cwd=cwd, check=True)


def build_trace_binary() -> None:
    run(
        [
            "gcc",
            "-O2",
            "-Wall",
            "-Wextra",
            "-march=native",
            "-o",
            str(TRACE_BIN),
            str(TRACE_SRC),
            "-lm",
        ]
    )


def generate_probe_trace(bit_ms: int = 5, threshold: int = 600, slots: int = 120) -> None:
    sender = subprocess.Popen(
        [
            str(CODE_DIR / "sender"),
            "-m",
            TRACE_MESSAGE,
            "-d",
            str(bit_ms),
            "-p",
            str(TRACE_PACKET_BYTES),
            "-r",
            str(TRACE_REPETITIONS),
        ],
        cwd=CODE_DIR,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(0.3)
        run(
            [
                str(TRACE_BIN),
                "-t",
                str(threshold),
                "-d",
                str(bit_ms),
                "-n",
                str(slots),
                "-m",
                TRACE_MESSAGE,
                "-p",
                str(TRACE_PACKET_BYTES),
                "-r",
                str(TRACE_REPETITIONS),
                "-o",
                str(TRACE_CSV),
            ]
        )
    finally:
        sender.send_signal(signal.SIGINT)
        try:
            sender.wait(timeout=2)
        except subprocess.TimeoutExpired:
            sender.kill()


def plot_probe_trace() -> None:
    df = pd.read_csv(TRACE_CSV)
    x = df["index"]
    expected_one = df["expected_bit"] == 1
    expected_zero = df["expected_bit"] == 0
    uncertain = df["decided_bit"] < 0

    plt.figure(figsize=(9.2, 4.8))
    plt.scatter(x[expected_one], df.loc[expected_one, "avg_cycles"], s=18, color="#d62728", label="Sender bit = 1")
    plt.scatter(
        x[expected_zero],
        df.loc[expected_zero, "avg_cycles"],
        s=18,
        facecolors="none",
        edgecolors="#1f77b4",
        linewidths=1.0,
        label="Sender bit = 0",
    )
    if uncertain.any():
        plt.scatter(
            x[uncertain],
            df.loc[uncertain, "avg_cycles"],
            s=70,
            marker="s",
            color="#2ca02c",
            label="Uncertain slot",
            zorder=5,
        )

    threshold = 600
    plt.axhline(threshold, color="0.55", linestyle=(0, (1.5, 2.2)), linewidth=1.2)
    plt.text(8, threshold + 18, "Threshold", color="0.25", fontsize=11)
    plt.xlabel("Time Slot Number")
    plt.ylabel("Average Probe Time (cycles)")
    plt.xlim(0, max(x))
    ymin = max(0, df["avg_cycles"].min() * 0.85)
    ymax = df["avg_cycles"].max() * 1.08
    plt.ylim(ymin, ymax)
    plt.legend(frameon=False, loc="upper right")
    plt.tight_layout()
    plt.savefig(TRACE_PNG, dpi=220)
    plt.close()


def write_bandwidth_csv() -> None:
    rows = [
        {"bit_ms": 10, "ideal_bps": 55.963, "completion_bps": 53.102, "note": "stable"},
        {"bit_ms": 5, "ideal_bps": 111.927, "completion_bps": 104.768, "note": "stable"},
        {"bit_ms": 2, "ideal_bps": 279.817, "completion_bps": 230.854, "note": "works well"},
        {"bit_ms": 1, "ideal_bps": 559.633, "completion_bps": 288.793, "note": "higher variance"},
    ]
    with BANDWIDTH_CSV.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def plot_bandwidth() -> None:
    df = pd.read_csv(BANDWIDTH_CSV)

    plt.figure(figsize=(7.2, 4.4))
    plt.plot(df["bit_ms"], df["ideal_bps"], marker="o", color="#444444", label="Ideal cycle-limited goodput")
    plt.plot(df["bit_ms"], df["completion_bps"], marker="o", color="#d62728", label="Observed completion rate")
    for _, row in df.iterrows():
        plt.annotate(
            row["note"],
            (row["bit_ms"], row["completion_bps"]),
            xytext=(6, 8),
            textcoords="offset points",
            fontsize=9,
            color="#444444",
        )
    plt.gca().invert_xaxis()
    plt.xlabel("Bit Duration (ms)")
    plt.ylabel("Payload Rate (bps)")
    plt.grid(True, axis="y", linestyle=":", linewidth=0.7, color="0.8")
    plt.legend(frameon=False, loc="upper left")
    plt.tight_layout()
    plt.savefig(BANDWIDTH_PNG, dpi=220)
    plt.close()


def main() -> None:
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    build_trace_binary()
    generate_probe_trace()
    plot_probe_trace()
    write_bandwidth_csv()
    plot_bandwidth()


if __name__ == "__main__":
    sys.exit(main())
