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
 * @brief Implementation file for the LoadWorker class.
 */

//Headers
#include <LoadWorker.h>
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

LoadWorker::LoadWorker(
        void* mem_array,
        size_t len,
        SequentialFunction kernel_fptr,
        SequentialFunction kernel_dummy_fptr,
        int32_t cpu_affinity,
        pthread_barrier_t *barrier
    ) :
        MemoryWorker(
            mem_array,
            len,
            cpu_affinity
        ),
        use_sequential_kernel_fptr_(true),
        kernel_fptr_seq_(kernel_fptr),
        kernel_dummy_fptr_seq_(kernel_dummy_fptr),
        kernel_fptr_ran_(NULL),
        kernel_dummy_fptr_ran_(NULL),
        barrier_(barrier)
    {
}

LoadWorker::LoadWorker(
        void* mem_array,
        size_t len,
        RandomFunction kernel_fptr,
        RandomFunction kernel_dummy_fptr,
        int32_t cpu_affinity,
        pthread_barrier_t *barrier
    ) :
        MemoryWorker(
            mem_array,
            len,
            cpu_affinity
        ),
        use_sequential_kernel_fptr_(false),
        kernel_fptr_seq_(NULL),
        kernel_dummy_fptr_seq_(NULL),
        kernel_fptr_ran_(kernel_fptr),
        kernel_dummy_fptr_ran_(kernel_dummy_fptr),
        barrier_(barrier)
    {
}

LoadWorker::~LoadWorker() {
}

void LoadWorker::run() {
    //Set up relevant state -- localized to this thread's stack
    int32_t cpu_affinity = 0;
    bool use_sequential_kernel_fptr = false;
    SequentialFunction kernel_fptr_seq = NULL;
    SequentialFunction kernel_dummy_fptr_seq = NULL;
    RandomFunction kernel_fptr_ran = NULL;
    RandomFunction kernel_dummy_fptr_ran = NULL;
    void* start_address = NULL;
    void* end_address = NULL;
    void* prime_start_address = NULL;
    void* prime_end_address = NULL;
    uint32_t bytes_per_pass = 0;
    uint64_t passes = 0;
    tick_t start_tick = 0;
    tick_t stop_tick = 0;
    tick_t elapsed_ticks = 0;
    tick_t elapsed_dummy_ticks = 0;
    tick_t adjusted_ticks = 0;
    bool warning = false;
    void* mem_array = NULL;
    size_t len = 0;
    tick_t target_ticks = g_ticks_per_ms * BENCHMARK_DURATION_MS; //Rough target run duration in ticks
    uint64_t p = 0;
    bool full_touch = false;
    bytes_per_pass = THROUGHPUT_BENCHMARK_BYTES_PER_PASS;

    //Grab relevant setup state thread-safely and keep it local
    if (acquireLock(-1)) {
      mem_array = mem_array_;
      len = len_;
      cpu_affinity = cpu_affinity_;
      use_sequential_kernel_fptr = use_sequential_kernel_fptr_;
      kernel_fptr_seq = kernel_fptr_seq_;
      kernel_dummy_fptr_seq = kernel_dummy_fptr_seq_;
      kernel_fptr_ran = kernel_fptr_ran_;
      kernel_dummy_fptr_ran = kernel_dummy_fptr_ran_;
      start_address = mem_array_;
      end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mem_array_)+bytes_per_pass);
      prime_start_address = mem_array_; 
      prime_end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mem_array_) + ((len_) / 2));
      releaseLock();
    }

    if (use_sequential_kernel_fptr == false)
        bytes_per_pass = 64;

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

        switch (i) {
            case LOCAL_DRAM_ACCESS:
                pe[i].config = 0x1d3;
                break;
            case LOCAL_PMM_ACCESS:
                pe[i].config = 0x80d1;
                break;
            case REMOTE_DRAM_ACCESS:
                pe[i].config = 0x2d3;
                break;
            case REMOTE_PMM_ACCESS:
                pe[i].config = 0x10d3;
                break;
            case WPQ_OCCUPANCY:
                pe[i].config = 0x1E4;
                break;
            case RPQ_OCCUPANCY:
                pe[i].config = 0x1E0;
                break;
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

    full_touch = true;
    // std::cout << "target_ticks " << target_ticks << std::endl;

    //Prime memory
    for (uint32_t i = 0; i < 1; i++) {
        forwSequentialRead_Word32(prime_start_address, prime_end_address); //dependent reads on the memory, make sure caches are ready, coherence, etc...
    }

    prime_start_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(prime_end_address));
    prime_end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mem_array_) + (len_));

    std::cout << "Thread " << cpu_affinity << " is warming up..." << std::endl;
    pthread_barrier_wait(barrier_);

    for (uint32_t i = 0; i < 1; i++) {
        forwSequentialRead_Word32(prime_start_address, prime_end_address); //dependent reads on the memory, make sure caches are ready, coherence, etc...
    }

    // AHN: Start the measurement
    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
        ioctl(perf_fd[i], PERF_EVENT_IOC_RESET, 0);
        ioctl(perf_fd[i], PERF_EVENT_IOC_ENABLE, 0);
    }

    std::cout << "Thread " << cpu_affinity << " is waiting..." << std::endl;
    pthread_barrier_wait(barrier_);
    //Run the benchmark!
    uintptr_t* next_address = static_cast<uintptr_t*>(mem_array);
    //Run actual version of function and loop overhead
    // while (elapsed_ticks < target_ticks) {
    while (1) {
        if (use_sequential_kernel_fptr) { //sequential function semantics
            start_tick = start_timer();
            (*kernel_fptr_seq)(start_address, end_address);
                    end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(end_address) + (bytes_per_pass));
                    start_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(start_address) + (bytes_per_pass));
                    // start_address = reinterpret_cast<void*>(
                    //                              reinterpret_cast<uint8_t*>(mem_array)
                    //                                +  (reinterpret_cast<uintptr_t>(start_address) + bytes_per_pass) % len);
                    // end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(start_address) + bytes_per_pass);
                    // std::cout << "start " << start_address << std::endl;
                    // std::cout << "end   " << end_address << std::endl;
            stop_tick = stop_timer();

            passes+=1;
            // std::cout << "based   " << mem_array << std::endl;
            // std::cout << "based+len   " << mem_array + len<< std::endl;
            if ((full_touch == true) &&
                    (start_address ==  (mem_array + len))) {
                std::cout << cpu_affinity << "] " << "base " << mem_array << std::endl;
                std::cout << cpu_affinity << "] " << "base+len " << mem_array + len << std::endl;
                break;
            }
            if (passes * 4096 >= len)
                break;
        } else { //random function semantics
            start_tick = start_timer();
            (*kernel_fptr_ran)(next_address, &next_address, bytes_per_pass);
            stop_tick = stop_timer();
            passes+=4096;

            // std::cout << cpu_affinity << "] " << "next " << next_address << std::endl;
            if (next_address == reinterpret_cast<uintptr_t*>(mem_array + (len))) {
                std::cout << cpu_affinity << "] " << "next " << mem_array+len << std::endl;
                std::cout << cpu_affinity << "] " << "base " << mem_array << std::endl;
                next_address = static_cast<uintptr_t*>(mem_array);
                break;
            }
            if ((full_touch == true) && (passes * 64 >= len))
                break;
        }
        elapsed_ticks += (stop_tick - start_tick);
    }

    // AHN: Stop the measurement
    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
        ioctl(perf_fd[i], PERF_EVENT_IOC_DISABLE, 0);
        read(perf_fd[i], &perf_count[i], sizeof(long long));
    }

    for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
        close(perf_fd[i]);
    }

    //Run dummy version of function and loop overhead
    p = 0;
    start_address = mem_array;
    end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mem_array) + bytes_per_pass);
    next_address = static_cast<uintptr_t*>(mem_array);
    while (p < passes) {
        if (use_sequential_kernel_fptr) { //sequential function semantics
            start_tick = start_timer();
                (*kernel_dummy_fptr_seq)(start_address, end_address);
                start_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mem_array)+(reinterpret_cast<uintptr_t>(start_address)+bytes_per_pass) % len);
                end_address = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(start_address) + bytes_per_pass);
            stop_tick = stop_timer();
            p+=1;
        } else { //random function semantics
            start_tick = start_timer();
            UNROLL256((*kernel_dummy_fptr_ran)(next_address, &next_address, bytes_per_pass);)
            stop_tick = stop_timer();
            p+=256;
        }

        elapsed_dummy_ticks += (stop_tick - start_tick);
    }

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

    adjusted_ticks = elapsed_ticks - elapsed_dummy_ticks;
    std::cout << cpu_affinity << ": " << adjusted_ticks << std::endl; 

    //Warn if something looks fishy
    if (elapsed_dummy_ticks >= elapsed_ticks || elapsed_ticks < MIN_ELAPSED_TICKS || adjusted_ticks < 0.5 * elapsed_ticks)
        warning = true;

    //Update the object state thread-safely
    if (acquireLock(-1)) {
        adjusted_ticks_ = adjusted_ticks;
        elapsed_ticks_ = elapsed_ticks;
        elapsed_dummy_ticks_ = elapsed_dummy_ticks;
        warning_ = warning;
        bytes_per_pass_ = bytes_per_pass;
        completed_ = true;
        passes_ = passes;
        for (uint32_t i = 0; i < NUM_COUNTERS; i++) {
            event_stat_[i] = perf_count[i];
            // std::cout << cpu_affinity << ": " << perf_count[i] << std::endl; 
        }
        releaseLock();
    }
}
