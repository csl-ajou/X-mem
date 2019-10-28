/* The MIT License (MIT)
 *
 * Copyright (c) 2014 Microsoft
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Author: Mark Gottscho <mgottscho@ucla.edu>
 */

/**
 * @file
 * 
 * @brief Implementation file for the LatencyWorker class.
 */

//Headers
#include <LatencyWorker.h>
#include <benchmark_kernels.h>
#include <common.h>

//Libraries
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#endif

#ifdef __gnu_linux__
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#endif

#define GiB (1024*1024*1024UL)

/*
 * AHN: perf measurement
 */
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
		int cpu, int group_fd, unsigned long flags)
{
	int ret;

	ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
			group_fd, flags);
	return ret;
}

using namespace xmem;

LatencyWorker::LatencyWorker(
        void* mem_array,
        size_t len,
        RandomFunction kernel_fptr,
        RandomFunction kernel_dummy_fptr,
        ChaseFunction chase_fptr,
        int32_t cpu_affinity,
        int use_sequential_kernel_fptr
    ) :
        MemoryWorker(
            mem_array,
            len,
            cpu_affinity
        ),
        use_sequential_kernel_fptr_(use_sequential_kernel_fptr),
        kernel_fptr_(kernel_fptr),
        kernel_dummy_fptr_(kernel_dummy_fptr),
        chase_fptr_(chase_fptr)
    {
}

LatencyWorker::~LatencyWorker() {
}

void LatencyWorker::run() {
    //Set up relevant state -- localized to this thread's stack
    int32_t cpu_affinity = 0;
    int use_sequential_kernel_fptr = 0;
    RandomFunction kernel_fptr = NULL;
    RandomFunction kernel_dummy_fptr = NULL;
    ChaseFunction chase_fptr = NULL;
    void* prime_start_address = NULL;
    void* prime_end_address = NULL;
    uint64_t bytes_per_pass = 0;
    uint64_t passes = 0;
    uint64_t p = 0;
    tick_t start_tick = 0;
    tick_t stop_tick = 0;
    tick_t elapsed_ticks = 0;
    tick_t elapsed_dummy_ticks = 0;
    tick_t adjusted_ticks = 0;
    bool full_touch = false;

    bool warning = false;
    void* mem_array = NULL;
    size_t len = 0;
    tick_t target_ticks = g_ticks_per_ms * BENCHMARK_DURATION_MS; //Rough target run duration in ticks

    // AHN: perf related data structures
    struct perf_event_attr pe[NUM_COUNTERS];
    uint64_t perf_count[NUM_COUNTERS];
	int perf_fd[NUM_COUNTERS];

    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
        memset(&pe[i], 0, sizeof(struct perf_event_attr));
        pe[i].type = PERF_TYPE_RAW;
        pe[i].size = sizeof(struct perf_event_attr);
        pe[i].disabled = 1;
        pe[i].inherit = 1;
        pe[i].exclude_kernel = 0;
        pe[i].exclude_hv = 1;

        if ( i == LOCAL_DRAM_ACCESS ) {
            pe[i].config = 0x1d3;
        } else if ( i == LOCAL_PMM_ACCESS ) {
            pe[i].config = 0x80d1;
        } else if ( i == REMOTE_DRAM_ACCESS ) {
            pe[i].config = 0x2d3;
        } else if ( i == REMOTE_PMM_ACCESS ) {
            pe[i].config = 0x10d3;
        }
    }

    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
        perf_fd[i] = perf_event_open(&pe[i], 0, -1, -1, 0);
        if (perf_fd[i] == -1) {
            fprintf(stderr, "Error opening leader %llx\n", pe[i].config);
            fprintf(stderr, "You may not run X-mem with root permission.\n");
            exit(EXIT_FAILURE);
        }
    }
    
    //Grab relevant setup state thread-safely and keep it local
    if (acquireLock(-1)) {
        mem_array = mem_array_;
        len = len_;
        bytes_per_pass = LATENCY_BENCHMARK_UNROLL_LENGTH * 8;
        cpu_affinity = cpu_affinity_;
        kernel_fptr = kernel_fptr_;
        kernel_dummy_fptr = kernel_dummy_fptr_;
        chase_fptr = chase_fptr_;
        use_sequential_kernel_fptr = use_sequential_kernel_fptr_;
        prime_start_address = mem_array_;
        prime_end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mem_array_) + len_);
        releaseLock();
    }

    //Set processor affinity
    bool locked = lock_thread_to_cpu(cpu_affinity);
    if (!locked)
        std::cerr << "WARNING: Failed to lock thread to logical CPU " << cpu_affinity << "! Results may not be correct." << std::endl;

    //Increase scheduling priority
#ifdef _WIN32
    DWORD original_priority_class;
    DWORD original_priority;
    if (!boost_scheduling_priority(original_priority_class, original_priority))
#endif
#ifdef __gnu_linux__
    if (!boost_scheduling_priority())
#endif
        std::cerr << "WARNING: Failed to boost scheduling priority. Perhaps running in Administrator mode would help." << std::endl;

    //Prime memory
    for (uint32_t i = 0; i < 4; i++) {
        forwSequentialRead_Word32(prime_start_address, prime_end_address); //dependent reads on the memory, make sure caches are ready, coherence, etc...
    }

    // target_ticks now should be changed to measure a big memory size
    // if ((len >= 2 * GiB) && (use_sequential_kernel_fptr == SEQUENTIAL)) {
    if ((len >= 2 * GiB)) {
        target_ticks = UINT64_MAX;
        full_touch = true;
    }

    std::cout << "target_ticks " << target_ticks << std::endl;

    // AHN: Start the measurement
    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
        ioctl(perf_fd[i], PERF_EVENT_IOC_RESET, 0);
        ioctl(perf_fd[i], PERF_EVENT_IOC_ENABLE, 0);
    }

    system("numastat -p xmem");

    //Run benchmark
    //Run actual version of function and loop overhead
    uintptr_t* next_address = static_cast<uintptr_t*>(mem_array); 
    uintptr_t* base_address = next_address;
    uint64_t offset = 0;
    while (elapsed_ticks < target_ticks) {
        if (use_sequential_kernel_fptr == SEQUENTIAL) {
            start_tick = start_timer();
            UNROLL256((*chase_fptr)(next_address, &next_address, use_sequential_kernel_fptr,
                        NULL, NULL);)
            stop_tick = stop_timer();
            elapsed_ticks += (stop_tick - start_tick);
            passes += 256;
            if ((full_touch == true) && (next_address == static_cast<uintptr_t*>(mem_array)))
                break;
        } else if (use_sequential_kernel_fptr == RANDOM) {
            start_tick = start_timer();
            (*chase_fptr)(next_address, &next_address, len/8, base_address, &offset);
            stop_tick = stop_timer();
            elapsed_ticks += (stop_tick - start_tick);
            passes += 4096;
            if ((full_touch == true) && (len <= passes * 64))
                break;
        }
    }


    // AHN: Stop the measurement
    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
	    ioctl(perf_fd[i], PERF_EVENT_IOC_DISABLE, 0);
	    read(perf_fd[i], &perf_count[i], sizeof(long long));

        if ( i == LOCAL_DRAM_ACCESS ) {
            std::cout << "LOCAL_DRAM_ACCESS: ";
        } else if ( i == LOCAL_PMM_ACCESS ) {
            std::cout << "LOCAL_PMM_ACCESS: ";
        } else if ( i == REMOTE_DRAM_ACCESS ) {
            std::cout << "REMOTE_DRAM_ACCESS: ";
        } else if ( i == REMOTE_PMM_ACCESS ) {
            std::cout << "REMOTE_PMM_ACCESS: ";
        }

        std::cout << perf_count[i] << std::endl;
    }

    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
        close(perf_fd[i]);
    }

    //Run dummy version of function and loop overhead
    // XXX Should we need `next_address` in this stage? (Think in Random)
    next_address = static_cast<uintptr_t*>(mem_array);
    while (p < passes) {
        start_tick = start_timer();
        UNROLL256((*kernel_dummy_fptr)(next_address, &next_address, use_sequential_kernel_fptr);)
        stop_tick = stop_timer();
        elapsed_dummy_ticks += (stop_tick - start_tick);
        p+=256;
    }

    adjusted_ticks = elapsed_ticks - elapsed_dummy_ticks;

    //Warn if something looks fishy
    if (elapsed_dummy_ticks >= elapsed_ticks || elapsed_ticks < MIN_ELAPSED_TICKS || adjusted_ticks < 0.5 * elapsed_ticks)
        warning = true;

    //Unset processor affinity
    if (locked)
        unlock_thread_to_numa_node();

    //Revert thread priority
#ifdef _WIN32
    if (!revert_scheduling_priority(original_priority_class, original_priority))
#endif
#ifdef __gnu_linux__
    if (!revert_scheduling_priority())
#endif
        std::cerr << "WARNING: Failed to revert scheduling priority. Perhaps running in Administrator mode would help." << std::endl;

    //Update the object state thread-safely
    if (acquireLock(-1)) {
        adjusted_ticks_ = adjusted_ticks;
        elapsed_ticks_ = elapsed_ticks;
        elapsed_dummy_ticks_ = elapsed_dummy_ticks;
        warning_ = warning;
        bytes_per_pass_ = bytes_per_pass;
        completed_ = true;
        passes_ = passes;
        for (uint32_t i = 0; i < NUM_COUNTERS; i++)
            event_stat_[i] = perf_count[i];
        releaseLock();
    }
}
