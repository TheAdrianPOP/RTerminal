#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif
#include "threading.h"
#include <immintrin.h>
#if defined(__linux__)
#  include <sched.h>
#endif

ParallelExecutor::ParallelExecutor(size_t thread_count) {
    if (thread_count == 0) {
#ifdef _WIN32
        DWORD _nc = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (_nc == 0) {
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            _nc = sysInfo.dwNumberOfProcessors;
        }
        thread_count = static_cast<size_t>(_nc > 0 ? _nc : 4);
#else
        long _nc = sysconf(_SC_NPROCESSORS_ONLN);
        thread_count = static_cast<size_t>(_nc > 0 ? _nc : 4);
#endif
    }
    const size_t workers = (thread_count > 1) ? thread_count - 1 : 0;
    threads_.reserve(workers);

#ifdef _WIN32
    InitializeCriticalSection(&mutex_cs_);
    InitializeConditionVariable(&cv_start_);
    InitializeConditionVariable(&cv_done_);

    const DWORD _nc = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    const int num_cpus = static_cast<int>(_nc > 0 ? _nc : 1);

    for (int i = 0; i < static_cast<int>(workers); ++i) {
        // OPTIMIZATION: Passed "this" directly. No dynamic struct allocation.
        HANDLE h = CreateThread(NULL, 0, win32ThreadProc, this, 0, NULL);
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
        pthread_t tid;
        // OPTIMIZATION: Passed "this" directly. No dynamic struct allocation.
        pthread_create(&tid, nullptr, pthreadProc, this);
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

#ifdef _WIN32
DWORD WINAPI ParallelExecutor::win32ThreadProc(LPVOID param) {
    static_cast<ParallelExecutor*>(param)->workerLoop(false);
    return 0;
}
#else
void* ParallelExecutor::pthreadProc(void* param) {
    static_cast<ParallelExecutor*>(param)->workerLoop(false);
    return nullptr;
}
#endif

// OPTIMIZATION: Moved synchronization, locking, and waiting structures 
// out of the header template to implement them once in .cpp.
void ParallelExecutor::runInternal(int count, JobFunc wrapper, void* userData) {
#ifdef _WIN32
    EnterCriticalSection(&mutex_cs_);
    current_job_func_  = wrapper;
    current_user_data_ = userData;
    total_items_.store(count,   rt_mo::relaxed);
    next_item_idx_.store(0,     rt_mo::relaxed);
    active_workers_.store(0,    rt_mo::relaxed);
    job_active_.store(true,     rt_mo::release);
    LeaveCriticalSection(&mutex_cs_);
    WakeAllConditionVariable(&cv_start_);
#else
    pthread_mutex_lock(&mutex_);
    current_job_func_  = wrapper;
    current_user_data_ = userData;
    total_items_.store(count,   rt_mo::relaxed);
    next_item_idx_.store(0,     rt_mo::relaxed);
    active_workers_.store(0,    rt_mo::relaxed);
    job_active_.store(true,     rt_mo::release);
    pthread_mutex_unlock(&mutex_);
    pthread_cond_broadcast(&cv_start_);
#endif

    workerLoop(true);

    int spin = 0;
    while (job_active_.load(rt_mo::acquire) && spin++ < SPIN_LIMIT) {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
        _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
        __asm__ __volatile__("yield" ::: "memory");
#endif
    }

#ifdef _WIN32
    if (job_active_.load(rt_mo::acquire) || active_workers_.load(rt_mo::acquire) > 0) {
        EnterCriticalSection(&mutex_cs_);
        while (job_active_.load(rt_mo::relaxed) || active_workers_.load(rt_mo::relaxed) > 0)
            SleepConditionVariableCS(&cv_done_, &mutex_cs_, INFINITE);
        LeaveCriticalSection(&mutex_cs_);
    }
#else
    if (job_active_.load(rt_mo::acquire) || active_workers_.load(rt_mo::acquire) > 0) {
        pthread_mutex_lock(&mutex_);
        while (job_active_.load(rt_mo::relaxed) || active_workers_.load(rt_mo::relaxed) > 0)
            pthread_cond_wait(&cv_done_, &mutex_);
        pthread_mutex_unlock(&mutex_);
    }
#endif
}

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
            
            // OPTIMIZATION: Cached total_items_ locally to reduce atomic read operations.
            const int total = total_items_.load(rt_mo::relaxed);

            while (true) {
                const int start_idx = next_item_idx_.fetch_add(WORK_BATCH_SIZE,
                                                                rt_mo::relaxed);
                if (start_idx >= total) break;

                const int end_idx = fast_min_t(start_idx + WORK_BATCH_SIZE, total);
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

/* ── Generic Thread Helper Implementation ────────────────────────────────── */

struct ThreadParam {
    Thread::Func f;
    void* a;
};

#ifdef _WIN32
static DWORD WINAPI threadTrampoline(LPVOID lp) {
    ThreadParam* p = static_cast<ThreadParam*>(lp);
    p->f(p->a);
    delete p;
    return 0;
}
#else
static void* threadTrampoline(void* lp) {
    ThreadParam* p = static_cast<ThreadParam*>(lp);
    p->f(p->a);
    delete p;
    return nullptr;
}
#endif

void Thread::spawn(Thread::Func func, void* arg) {
    ThreadParam* p = new ThreadParam{func, arg};
#ifdef _WIN32
    HANDLE h = CreateThread(nullptr, 0, threadTrampoline, p, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        delete p;
    }
#else
    pthread_t t;
    if (pthread_create(&t, nullptr, threadTrampoline, p) == 0) {
        pthread_detach(t);
    } else {
        delete p;
    }
#endif
}