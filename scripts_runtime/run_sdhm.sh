#!/bin/bash
# Date: Oct 14, 2019
# Author: Won-Kyo Choe # Description: X-Mem evalution script 
# Global Variable
MiB=$((1024))
GiB=$((1024*$MiB))

ARGC=$#
XMEM_DIR=$HOME/research/sdhm/benchmark/X-Mem
DATE=$(date +%Y%m%d-%H%M%S)
ARR_WSS_KB=()
NUM_THREADS=1

script_path=$xmem_path/scripts_runtime

function usage() {
    echo "USAGE: $0 [-t <Memory Type>][-n <# of iteration][-p <Memory Policy>" 1>&2
    exit 1
}

function check_policy() {
    case $1 in
        interleave)
            ;&
        preferred)
            echo "Policy: $1"
            ;;
        *)
            echo "-p) Memory Policy should be"
            echo "[interleave preferred]"
            exit 1
            ;;
    esac
}

function check_type() {
    case $1 in
        dram)
            ;&
        cached)
            ;&
        uncached)
            echo "Memory Type: $1"
            ;;
        *)
            echo "-t) Memory type should be"
            echo "[dram cached uncached]"
            exit 1
            ;;
    esac
}

function prepare() {
    if [[ ! -d $LOG_DIR ]]; then
        mkdir -p $LOG_DIR
    fi
    echo $LOG_DIR
    uname -a > ${LOG_DIR}/uname.txt
    touch ${LOG_DIR}/history

    # Memory allocation on each thread
    # echo 524288 | sudo tee /proc/sys/vm/nr_pages_of_dram

    # MSR
    sudo modprobe msr

    # Set CPU freq at max
    f_max_freq=$HOME/scripts/set_max_freq.sh
    f_prefetch=$HOME/scripts/prefetchers.sh
    if [ ! -f $f_max_freq  ] | [ ! -f $f_prefetch ]; then
        echo "There are no those files. Contact with Superv"
        exit 1
    fi
    sudo bash $f_max_freq
    sudo bash $f_prefetch disable &> /dev/null

    # No Randomization
    echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
    echo 0 | sudo tee /proc/sys/kernel/numa_balancing

    # Intel Vtune configuration
    source $HOME/intel/vtune_amplifier/amplxe-vars.sh
    echo 0 | sudo tee /proc/sys/kernel/perf_event_paranoid
    echo 0 | sudo tee /proc/sys/kernel/kptr_restrict

    # chunk interleave?
    if [[ $chunk -ge 1 ]]; then
        echo "Enable Chunk interleaving..."
        echo $chunk | sudo tee /proc/sys/vm/min_nr_chunks
    fi

    if [[ `uname -a` == *"cached"* ]]; then
        # echo 1048576 | sudo tee /proc/sys/vm/nr_pages_of_dram
        cat /proc/sys/vm/nr_pages_of_dram >> ${LOG_DIR}/config
        cat /proc/sys/vm/min_nr_chunks >> ${LOG_DIR}/config
    fi
    sudo sysctl kernel >> ${LOG_DIR}/kernel_opt
    sudo sysctl vm >> ${LOG_DIR}/vm_opt
}

function clean() {
    echo 3 | sudo tee /proc/sys/vm/drop_caches
}

function run() {
    local pattern=""
    local mode=""
    local bench_type=""
    local index=""
    local title="$1-$2-$3"

    if [[ "rand" == $1 ]]; then
        pattern="-r"
    elif [[ "seq" == $1 ]]; then
        pattern="-s"
    fi

    if [[ "read" == $2 ]]; then
        mode="-R"
    elif [[ "write" == $2 ]]; then
        mode="-W"
    fi

    if [[ "interleave" == $policy ]]; then
        policy_option="--interleave=all"
    elif [[ "preferred" == $policy ]]; then
        policy_option="--preferred=0"
    fi

    if [[ "latency" == $3 ]]; then
        bench_type="-l"
        index="22"
    elif [[ "throughput" == $3 ]]; then
        bench_type="-t"
        index="22"
    fi

    for wss in ${ARR_WSS_KB[@]}; do
        echo "[$wss] pattern $1 $pattern / mode $2 $mode... / benchmark $3 $bench_type" | tee -a ${LOG_DIR}/history
        echo "$policy" | tee -a ${LOG_DIR}/config
        PERF_TEMP="$LOG_DIR/perf_w${wss}_j${NUM_THREADS}_$3"
        XMEM_TEMP="$LOG_DIR/xmem_w${wss}_j${NUM_THREADS}_$3"

        # Run
        clean
        sleep 10
        sudo numactl $policy_option -N 0 -- $XMEM_DIR/bin/xmem-linux-x64 $bench_type $mode $pattern -v -n $NUM_ITER -w${wss} -j${NUM_THREADS} -f ${XMEM_TEMP}.csv -p ${PERF_TEMP} >> ${XMEM_TEMP}.txt 2>&1

        cat $PERF_TEMP | awk -F';' -v wss=$wss '{print wss ";" $0}' >> $LOG_DIR/perf_stats.log
        cat $XMEM_TEMP.csv | tail -n 1 | awk -F ',' '{print $3 " " $22 " " $12}' >> $LOG_DIR/$title.log

        sleep 10
    done
}

function end() {
    if [[ $chunk -ge 1 ]]; then
        echo "Disable Chunk interleaving..."
        echo 0 | sudo tee /proc/sys/vm/min_nr_chunks
    fi
    echo "Done"
}

#########################
## Scrtip Logic starts ##
#########################
while getopts ":t:n:c:p:" arg; do
    case "${arg}" in
        n)
            NUM_ITER=${OPTARG}
            ;;
        t)
            check_type ${OPTARG}
            MEM_TYPE=${OPTARG}
            ;;
        c)
            chunk=${OPTARG}
            ;;
        p)
            check_policy ${OPTARG}
            policy=${OPTARG}
            ;;
        *)
            usage
            ;;
    esac
done
shift $((OPTIND-1))

if [ -z $MEM_TYPE ] | [ -z $NUM_ITER ] | [ -z $policy ]; then
    usage
fi

LOG_DIR=${XMEM_DIR}/logs/${MEM_TYPE}-${DATE}
ARR_WSS_KB=($((16*$GiB)) $((32*$GiB)) $((64*$GiB)) $((128*$GiB)) $((256*$GiB)) $((384*$GiB)))
prepare

# run "seq" "read" "latency"
# run "seq" "write" "latency"
run "rand" "read" "latency"
# run "rand" "write" "latency"
end
