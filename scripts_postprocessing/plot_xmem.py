#!/usr/bin/python3

import json
import matplotlib.pyplot as plt
import numpy as np
import sys

DATA_JSON = "refined_data.json"
MiB = (1024 * 1024)
NUM_THREAD = 32
pattern = sys.argv[1]   # "seq" or "rand"
mode = sys.argv[2]      # "r" or "w"


def make_plot(x, y1, y2, ly, ry, mem, color, m1, m2):
    x = [int(int(a) * NUM_THREAD / MiB) for a in x]
    y1 = [int(float(num)) for num in y1]
    y2 = [int(float(num)) for num in y2]
    ly.plot(x, y1, label=mem + " tp", color=color, marker=m1,
            linestyle="--")
    ry.plot(x, y2, label=mem + " lat", color=color, marker=m2,
            linestyle="-")


def main():
    data = {}
    with open(DATA_JSON, "r") as fp:
        data = json.load(fp)

    fig, tp = plt.subplots()
    tp.set_xlabel("WSS size(GiB)")
    tp.set_ylabel("Throughput (MiB/s)")

    lat = tp.twinx()
    lat.set_ylabel("Latency (ns)")
    lat.set_yticks([1000, 2000, 3000, 4000, 5000, 6000])

    make_plot(data["dram"]["size"],
              data["dram"]["throughput"][pattern][mode],
              data["dram"]["latency"][pattern][mode],
              tp, lat, "dram", "black", "+", "*")
    make_plot(data["cached"]["size"],
              data["cached"]["throughput"][pattern][mode],
              data["cached"]["latency"][pattern][mode],
              tp, lat, "cached", "red", "+", "*")
    make_plot(data["uncached"]["size"],
              data["uncached"]["throughput"][pattern][mode],
              data["uncached"]["latency"][pattern][mode],
              tp, lat, "uncached", "green", "+", "*")
    fig.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
