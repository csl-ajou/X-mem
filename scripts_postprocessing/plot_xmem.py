#!/usr/bin/python3

import json
import matplotlib.pyplot as plt
import sys
import numpy as np

DATA_JSON = "refined_data.json"
MiB = (1024 * 1024)
NUM_THREAD = 16
pattern = sys.argv[1]   # "seq" or "rand"
mode = sys.argv[2]      # "r" or "w"
IMG_PATH = "plots/default_" + pattern + "_" + mode
PINDEX = 0

# https://matplotlib.org/3.1.0/gallery/color/named_colors.html
bar_colors = ["darkslategray", "seagreen", "springgreen", "palegreen",
              "turquoise", "aquamarine"]
line_colors = ["rosybrown", "lightcoral", "indianred", "brown", "firebrick",
               "tomato"]
# https://matplotlib.org/3.1.1/api/markers_api.html#module-matplotlib.markers
markers = [".", ",", "v", "^", "<", ">", "*", "d", "P"]


def make_plot(x, y1, y2, ly, ry, mem, color, color2, marker, width, offset,
              tick_label=None):
    global PINDEX
    y1 = [int(float(num)) for num in y1]
    y2 = [int(float(num)) for num in y2]
    ly.bar(x + offset, y1, width=width, label=mem + " tp",
           color=color, edgecolor="k")
    ry.plot(x + width * 2, y2, label=mem + " lat", color=color2,
            marker=marker, markersize=10)
    PINDEX += 1


def main():
    data = {}
    with open(DATA_JSON, "r") as fp:
        data = json.load(fp)

    fig, tp = plt.subplots()
    tp.set_xlabel("WSS size(GiB)")
    tp.set_ylabel("Throughput (MiB/s)")
    tp.grid(b=True, axis="y")
    tp.set_axisbelow(True)

    lat = tp.twinx()
    lat.set_ylabel("Latency (ns)")
    # lat.set_yticks([100, 500, 1000, 2000, 3000])

    x = np.arange(len(data["dram"]["size"]))
    gap = 1.0 / len(x)
    size_range = [int(int(a) * NUM_THREAD / MiB) for a in data["dram"]["size"]]
    tp.set_xticks(x + gap * 2)
    tp.set_xticklabels(size_range)

    make_plot(x,
              data["dram"]["throughput"][pattern][mode],
              data["dram"]["latency"][pattern][mode],
              tp, lat, "dram",
              bar_colors[PINDEX], line_colors[PINDEX], markers[PINDEX], gap, 0)
    make_plot(x,
              data["uncached-m"]["throughput"][pattern][mode],
              data["uncached-m"]["latency"][pattern][mode],
              tp, lat, "uncached-m",
              bar_colors[PINDEX], line_colors[PINDEX], markers[PINDEX], gap, gap)
    make_plot(x,
              data["uncached-p"]["throughput"][pattern][mode],
              data["uncached-p"]["latency"][pattern][mode],
              tp, lat, "uncached-p",
              bar_colors[PINDEX], line_colors[PINDEX], markers[PINDEX], gap, gap * 2)
    make_plot(x,
              data["cached-d"]["throughput"][pattern][mode],
              data["cached-d"]["latency"][pattern][mode],
              tp, lat, "cached-d",
              bar_colors[PINDEX], line_colors[PINDEX], markers[PINDEX], gap, gap * 3)
    make_plot(x,
              data["cached"]["throughput"][pattern][mode],
              data["cached"]["latency"][pattern][mode],
              tp, lat, "cached",
              bar_colors[PINDEX], line_colors[PINDEX], markers[PINDEX], gap, gap * 4)
    make_plot(x,
              data["cached-s"]["throughput"][pattern][mode],
              data["cached-s"]["latency"][pattern][mode],
              tp, lat, "cached-s",
              bar_colors[PINDEX], line_colors[PINDEX], markers[PINDEX], gap, gap * 5)

    # Figure size
    plt.subplots_adjust(bottom=0.3)
    # Merge labels
    tp_h, tp_l = tp.get_legend_handles_labels()
    lat_h, lat_l = lat.get_legend_handles_labels()
    plt.legend(tp_h + lat_h, tp_l + lat_l,
               bbox_to_anchor=(0.75, 0), loc="lower right",
               bbox_transform=fig.transFigure, ncol=2)
    plt.title(pattern.upper() + "-" + mode.upper())
    plt.show()
    # plt.savefig(IMG_PATH + ".png")


if __name__ == "__main__":
    main()
