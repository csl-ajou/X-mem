#!/bin/bash

ARGC=$#

xmem_path=$HOME/research/sdhm/benchmark/X-Mem
script_path=$xmem_path/scripts_runtime

if [[ "$ARGC" != 1 ]]; then
    echo "USAGE: run_sdhm.sh <Memory Type>"
    exit 1
fi

# Set CPU freqeuncy at max
sudo bash $HOME/scripts/set_max_freq.sh

# No randomization. Everything is static
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

echo 0 | sudo tee /proc/sys/kernel/numa_balancing

memory_type=$1
MiB=$((1024))
GiB=$((1024*$MiB))

case $memory_type in
    dram)
        # array_working_set_size_KB=( $((1*$GiB)) $((1500*$MiB)) $((2*$GiB)) $((2500*$MiB)) $((3*$GiB)) $((3500*$MiB)));;
        array_working_set_size_KB=( $((1*$GiB)) $((1500*$MiB)) $((2*$GiB)) $((2500*$MiB)) $((3*$GiB)) $((3500*$MiB)));;
    cached)
        # array_working_set_size_KB=( $((1*$GiB)) $((1500*$MiB)) $((2*$GiB)) $((2500*$MiB)) $((3*$GiB)) $((3500*$MiB)));;
        # array_working_set_size_KB=( $((4375*$MiB)) $((5*$GiB)) $((6250*$MiB)) $((7*$GiB)) $((8*$GiB)) $((8750*$MiB)) );;
        array_working_set_size_KB=( $((1*$GiB)) $((1500*$MiB)) $((2*$GiB)) $((2500*$MiB)) $((3*$GiB)) $((3500*$MiB)));;
    uncached)
        # array_working_set_size_KB=( $((512*$MiB)) $((1*$GiB)) $((1500*$MiB)) $((1875*$MiB)) $((3*$GiB)) $((4*$GiB)) $((5*$GiB)) $((6*$GiB)) $((7*$GiB)) $((8*$GiB)) $((9*GiB)) $((9375*MiB)) );;
        # array_working_set_size_KB=( $((4375*$MiB)) $((5*$GiB)) $((6250*$MiB)) $((7*$GiB)) $((8*$GiB)) $((8750*$MiB)) );;
        array_working_set_size_KB=( $((1*$GiB)) $((1500*$MiB)) $((2*$GiB)) $((2500*$MiB)) $((3*$GiB)) $((3500*$MiB)));;
    *)
        echo "type: dram | cached | uncached"
        exit 1;;
esac
output_dir=$xmem_path/logs/$1-$(date +%Y%m%d-%H%M%S)

export array_working_set_size_KB
. $script_path/run_xmem_exhaustive_linux.sh $xmem_path x64 16 67108864 $output_dir

