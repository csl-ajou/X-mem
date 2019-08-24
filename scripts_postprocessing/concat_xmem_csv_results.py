import csv
import json

XMEM = "/home/wonkyoc/sdhm/benchmark/X-Mem/"
LOG = XMEM + "logs/"

# Manual configuration
# You should change your log dirs below
DRAM_LOG_DIR = LOG + "dram-20190823-010746/"
CACHED_LOG_DIR = LOG + "cached-20190823-015232/"
UNCACHED_LOG_DIR = LOG + "uncached-20190821-092814/"

dram_range = ["1920000"]
cached_range = dram_range + ["7340032"]
# uncached_range = cached_range + [9600000]
uncached_range = cached_range

# Refined data for plot
header = []


def make_dict(mem):
    if mem == "dram":
        ran = dram_range
    elif mem == "cached":
        ran = cached_range
    elif mem == "uncached":
        ran = uncached_range
    return {
        "latency": {
            "seq": {
                "r": [],
                "w": []
            },
            "rand": {
                "r": [],
                "w": []
            },
        },
        "throughput": {
            "seq": {
                "r": [],
                "w": []
            },
            "rand": {
                "r": [],
                "w": []
            },
        },
        "size": ran
    }


def read_file(path, prefix, size, postfix):
    global header
    filename = path + prefix + size + postfix
    data = []
    with open(filename, newline="") as csvfile:
        line = csv.reader(csvfile, delimiter=",")
        for entry in line:
            if len(header) == 0:
                header = entry
            else:
                data.append(entry)
    return data


def parse(raw, mem):
    global header
    global refined_data

    test_idx = header.index("Test Name")
    ac_pattern_idx = header.index("Load Access Pattern")
    rw_idx = header.index("Load Read/Write Mix")

    for data in raw:
        if "Throughput" in data[test_idx]:
            idx = header.index("Mean Load Throughput")
            value = data[idx]
            if data[ac_pattern_idx] == "SEQUENTIAL" and data[rw_idx] == "READ":
                refined_data[mem]["throughput"]["seq"]["r"].append(value)
            elif data[ac_pattern_idx] == "SEQUENTIAL" and data[rw_idx] == "WRITE":
                refined_data[mem]["throughput"]["seq"]["w"].append(value)
            elif data[ac_pattern_idx] == "RANDOM" and data[rw_idx] == "READ":
                refined_data[mem]["throughput"]["rand"]["r"].append(value)
            elif data[ac_pattern_idx] == "RANDOM" and data[rw_idx] == "WRITE":
                refined_data[mem]["throughput"]["rand"]["w"].append(value)
        elif "Latency" in data[test_idx]:
            idx = header.index("Mean Latency")
            value = data[idx]
            if data[ac_pattern_idx] == "SEQUENTIAL" and data[rw_idx] == "READ":
                refined_data[mem]["latency"]["seq"]["r"].append(value)
            elif data[ac_pattern_idx] == "SEQUENTIAL" and data[rw_idx] == "WRITE":
                refined_data[mem]["latency"]["seq"]["w"].append(value)
            elif data[ac_pattern_idx] == "RANDOM" and data[rw_idx] == "READ":
                refined_data[mem]["latency"]["rand"]["r"].append(value)
            elif data[ac_pattern_idx] == "RANDOM" and data[rw_idx] == "WRITE":
                refined_data[mem]["latency"]["rand"]["w"].append(value)


def __get_refine(mem, mem_range, mem_dir):
    for wss_size in mem_range:
        raw_data = read_file(mem_dir, "xmem_w", wss_size, "_j.csv")
        parse(raw_data, mem)


def refine():
    # dram
    __get_refine("dram", dram_range, DRAM_LOG_DIR)
    __get_refine("cached", cached_range, CACHED_LOG_DIR)
    __get_refine("uncached", uncached_range, UNCACHED_LOG_DIR)


refined_data = {
    "dram": make_dict("dram"),
    "cached": make_dict("cached"),
    "uncached": make_dict("uncached"),
}


def main():
    refine()
    with open('refined_data.json', 'w') as fp:
        json.dump(refined_data, fp)


if __name__ == "__main__":
    main()
