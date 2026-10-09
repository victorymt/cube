# /// script
# requires-python = ">=3.11"
# dependencies = ["matplotlib>=3.8,<4", "numpy>=2,<3"]
# ///
"""Plot the measured CSV exported by `geology history` (no simulated data)."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("csv", type=Path)
parser.add_argument("--output", type=Path, default=Path("volcano-history.png"))
args = parser.parse_args()
with args.csv.open() as source:
    rows = list(csv.DictReader(source))
events = sorted({int(row["event"]) for row in rows})
xs = sorted({float(row["x"]) for row in rows})
ys = sorted({float(row["y"]) for row in rows})
xx, yy = np.meshgrid(xs, ys)
fig, axes = plt.subplots(2, len(events), figsize=(5 * len(events), 9), constrained_layout=True, squeeze=False)
max_depth = max(float(row["depth"]) for row in rows)
previous = np.zeros_like(xx)
for col, event in enumerate(events):
    selected = {(float(r["x"]), float(r["y"])): r for r in rows if int(r["event"]) == event}
    depth = np.array([[float(selected[x, y]["depth"]) for x in xs] for y in ys])
    ground = np.array([[float(selected[x, y]["ground"]) for x in xs] for y in ys])
    for ax in axes[:, col]:
        ax.contour(xx, yy, ground, levels=12, colors="#9ca3af", linewidths=.65)
        ax.plot(0, 0, "+", color="#111827", markersize=9)
        ax.set(xlabel="East offset (blocks)", ylabel="North offset (blocks)", aspect="equal")
    layer = axes[0, col].pcolormesh(xx, yy, np.ma.masked_less(depth, .01),
        cmap="magma", vmin=0, vmax=max_depth, shading="nearest")
    axes[0, col].set_title(f"After eruption {event + 1}: cumulative thickness")
    delta = depth - previous
    difference = axes[1, col].pcolormesh(xx, yy, np.ma.masked_less(delta, .01),
        cmap="viridis", vmin=0, vmax=max_depth, shading="nearest")
    axes[1, col].set_title(f"Eruption {event + 1}: thickness change")
    previous = depth
fig.colorbar(layer, ax=list(axes[0]), label="Solid deposit thickness (blocks)", shrink=.8)
fig.colorbar(difference, ax=list(axes[1]), label="Added thickness (blocks)", shrink=.8)
fig.suptitle("Historical lava deposition on sampled terrain — central source marked +", fontsize=15)
fig.savefig(args.output, dpi=150)
print(args.output)
