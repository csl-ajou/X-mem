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
    uname -a > ${LOG_DIR}/uname.txt
    touch ${LOG_DIR}/history.log

    if [[ `uname -a` == *"cached"* ]]; then
        # echo 1048576 | sudo tee /proc/sys/vm/nr_pages_of_dram
        cat /proc/sys/vm/nr_pages_of_dram > ${LOG_DIR}/config.txt
        cat /proc/sys/vm/min_nr_chunks >> ${LOG_DIR}/config.txt
    fi

    # Memory allocation on each thread
    # echo 524288 | sudo tee /proc/sys/vm/nr_pages_of_dram

    # MSR
    sudo modprobe msr

    # Set CPU freq at max
    sudo bash $HOME/scripts/set_max_freq.sh

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
        cat /proc/sys/vm/nr_pages_of_dram > ${LOG_DIR}/config.txt
        cat /proc/sys/vm/min_nr_chunks >> ${LOG_DIR}/config.txt
    fi
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
    touch $LOG_DIR/${title}.log
    for wss in ${ARR_WSS_KB[@]}; do
        echo "[$wss] pattern $1 $pattern / mode $2 $mode... / benchmark $3 $bench_type" | tee -a ${LOG_DIR}/history.log
        echo "$policy_option" | tee -a ${LOG_DIR}/history.log
        PERF_TEMP="$LOG_DIR/perf_temp.txt"
        XMEM_TEMP="$LOG_DIR/xmem_w${wss}_j${NUM_THREADS}_${title}"

        touch ${XMEM_TEMP}.txt

        latency=0
        throughput=0
        local_hit=0
        local_miss=0
        remote_hit=0
        remote_miss=0
        local_ratio=0
        remote_ratio=0
        local_mpki=0
        remote_mpki=0
        inst=0

        # Run
        clean
        perf stat -a -e mem_load_l3_miss_retired.local_dram,mem_load_retired.local_pmm,mem_load_l3_miss_retired.remote_dram,mem_load_l3_miss_retired.remote_pmm,inst_retired.any -o $PERF_TEMP -x, -- numactl $policy_option -- $XMEM_DIR/bin/xmem-linux-x64 $bench_type -C0 -c 64 $mode $pattern -v -u -n $NUM_ITER -w${wss} -j${NUM_THREADS} -f ${XMEM_TEMP}.csv >> ${XMEM_TEMP}.txt 2>&1

        if [[ "latency" == $3 ]]; then
            latency=`awk -F',' 'NR==2 {print $22}' ${XMEM_TEMP}.csv`
            throughput=`awk -F',' 'NR==2 {print $12}' ${XMEM_TEMP}.csv`
        elif [[ "throughput" == $3 ]]; then
            throughput=`awk -F',' 'NR==2 {print $12}' ${XMEM_TEMP}.csv`
        fi

        local_hit=`grep "local_dram" $PERF_TEMP | awk -F',' '{print $1}'`
        local_miss=`grep "local_pmm" $PERF_TEMP | awk -F',' '{print $1}'`
        remote_hit=`grep "remote_dram" $PERF_TEMP | awk -F',' '{print $1}'`
        remote_miss=`grep "remote_pmm" $PERF_TEMP | awk -F',' '{print $1}'`
        inst=`grep "inst" $PERF_TEMP | awk -F',' '{print $1}'`

        echo "$local_hit $local_miss $remote_hit $remote_miss $inst"| tee -a ${LOG_DIR}/history.log

        local_ratio=$(echo "scale=4; $local_hit * 100/ ($local_hit+$local_miss)" | bc -l | awk '{printf "%.4f", $0}')
        remote_ratio=$(echo "scale=4; $remote_hit * 100/ ($remote_hit+$remote_miss)" | bc -l | awk '{printf "%.4f", $0}')
        local_mpki=$(echo "scale=4; $local_miss * 1000/ $inst" | bc -l | awk '{printf "%.4f", $0}')
        remote_mpki=$(echo "scale=4; $remote_miss * 1000/ $inst" | bc -l | awk '{printf "%.4f", $0}')
        echo "[$wss] $latency $throughput $local_ratio $remote_ratio $local_mpki $remote_mpki" | tee -a ${LOG_DIR}/history.log
        echo "$wss $latency $throughput $local_ratio $remote_ratio $local_mpki $remote_mpki" | tee -a ${LOG_DIR}/${title}.log

        rm $PERF_TEMP
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

run "seq" "read" "latency"
run "seq" "write" "latency"
run "rand" "read" "latency"
run "rand" "write" "latency"
end
