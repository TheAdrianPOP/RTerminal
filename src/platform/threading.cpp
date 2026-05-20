#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif
#include "threading.h"
#include <immintrin.h>
#if defined(__linux__)
#  include <sched.h>
#endif

ParallelExecutor::ParallelExecutor(size_t thread_count) {
    const size_t workers = (thread_count > 1) ? thread_count - 1 : 0;
    threads_.reserve(workers);

#ifdef _WIN32
    InitializeCriticalSection(&mutex_cs_);
    InitializeConditionVariable(&cv_start_);
    InitializeConditionVariable(&cv_done_);

    const DWORD _nc = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    const int num_cpus = static_cast<int>(_nc > 0 ? _nc : 1);

    for (int i = 0; i < static_cast<int>(workers); ++i) {
        Win32ThreadArg* arg = new Win32ThreadArg{this, i + 1};
        HANDLE h = CreateThread(
            NULL,
            0,
            win32ThreadProc,
            arg,
            0,
            NULL);
        threads_.push_back(h);

        const int target_cpu = (i + 1) % num_cpus;
        SetThreadAffinityMask(h, static_cast<DWORD_PTR>(1) << target_cpu);
    }
#else
    pthread_mutex_init(&mutex_, nullptr);
    pthread_cond_init(&cv_start_, nullptr);
    pthread_cond_init(&cv_done_,  nullptr);

    const long _nc = sysconf(_SC_NPROCESSORS_ONLN);
    const int num_cpus = (_nc > 0) ? (int)_nc : 1;

    for (int i = 0; i < static_cast<int>(workers); ++i) {
        PthreadArg* arg = new PthreadArg{this, i + 1};
        pthread_t tid;
        pthread_create(&tid, nullptr, pthreadProc, arg);
        threads_.push_back(tid);

        const int target_cpu = (i + 1) % num_cpus;
#if defined(__linux__)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(target_cpu, &cpuset);
        pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset);
#else
        (void)target_cpu;
#endif
    }
#endif
}

ParallelExecutor::~ParallelExecutor() {
    shutdown_.store(true, rt_mo::release);

#ifdef _WIN32
    WakeAllConditionVariable(&cv_start_);
    for (HANDLE h : threads_) {
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    }
    DeleteCriticalSection(&mutex_cs_);
#else
    pthread_cond_broadcast(&cv_start_);
    for (pthread_t t : threads_) pthread_join(t, nullptr);
    pthread_cond_destroy(&cv_done_);
    pthread_cond_destroy(&cv_start_);
    pthread_mutex_destroy(&mutex_);
#endif
}

void ParallelExecutor::workerMain(int core_id) {
    workerLoop(false);
}

#ifdef _WIN32
DWORD WINAPI ParallelExecutor::win32ThreadProc(LPVOID param) {
    Win32ThreadArg* arg = static_cast<Win32ThreadArg*>(param);
    ParallelExecutor* self    = arg->self;
    int               core_id = arg->core_id;
    delete arg;
    self->workerMain(core_id);
    return 0;
}
#else
void* ParallelExecutor::pthreadProc(void* param) {
    PthreadArg* arg = static_cast<PthreadArg*>(param);
    ParallelExecutor* self    = arg->self;
    int               core_id = arg->core_id;
    delete arg;
    self->workerMain(core_id);
    return nullptr;
}
#endif

void ParallelExecutor::workerLoop(bool isMainThread) {
    while (true) {

        int spin = 0;
        while (!job_active_.load(rt_mo::acquire) &&
               !shutdown_.load(rt_mo::relaxed))
        {
            if (isMainThread) return;

            if (spin++ < SPIN_LIMIT) {
                _mm_pause();
            } else {
#ifdef _WIN32
                EnterCriticalSection(&mutex_cs_);
                while (!job_active_.load(rt_mo::relaxed) &&
                       !shutdown_.load(rt_mo::relaxed))
                    SleepConditionVariableCS(&cv_start_, &mutex_cs_, INFINITE);
                LeaveCriticalSection(&mutex_cs_);
#else
                pthread_mutex_lock(&mutex_);
                while (!job_active_.load(rt_mo::relaxed) &&
                       !shutdown_.load(rt_mo::relaxed))
                    pthread_cond_wait(&cv_start_, &mutex_);
                pthread_mutex_unlock(&mutex_);
#endif
                spin = 0;
            }
        }

        if (shutdown_.load(rt_mo::relaxed)) return;

        active_workers_.fetch_add(1, rt_mo::relaxed);

        if (job_active_.load(rt_mo::acquire)) {
            const JobFunc task     = current_job_func_;
            void* const   userData = current_user_data_;

            while (true) {
                const int start_idx = next_item_idx_.fetch_add(WORK_BATCH_SIZE,
                                                                rt_mo::relaxed);
                if (start_idx >= total_items_.load(rt_mo::relaxed)) break;

                const int end_idx = fast_min_t(start_idx + WORK_BATCH_SIZE,
                                               total_items_.load(rt_mo::relaxed));
                for (int i = start_idx; i < end_idx; ++i)
                    task(i, userData);
            }
        }

        if (active_workers_.fetch_sub(1, rt_mo::acq_rel) == 1) {
            job_active_.store(false, rt_mo::release);
#ifdef _WIN32
            EnterCriticalSection(&mutex_cs_);
            LeaveCriticalSection(&mutex_cs_);
            WakeAllConditionVariable(&cv_done_);
#else
            pthread_mutex_lock(&mutex_);
            pthread_mutex_unlock(&mutex_);
            pthread_cond_broadcast(&cv_done_);
#endif
        }

        if (isMainThread) return;
    }
}