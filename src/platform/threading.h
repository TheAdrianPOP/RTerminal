#ifndef THREADING_H
#define THREADING_H

#include "vector.h"
#include "atomic.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
    #include <unistd.h>   // sysconf(_SC_NPROCESSORS_ONLN)
#endif

#include <immintrin.h>

constexpr int CACHE_LINE_SIZE = 64;
constexpr int WORK_BATCH_SIZE = 8;
constexpr int SPIN_LIMIT      = 400;

class ParallelExecutor {
public:
    explicit ParallelExecutor(size_t thread_count);
    ~ParallelExecutor();

    size_t getThreadCount() const noexcept { return threads_.size() + 1; }

    using JobFunc = void (*)(int, void*);

    template<typename Func>
    void run(int count, Func&& func) {
        if (count <= 0) return;
        auto wrapper = +[](int index, void* userData) {
            (*static_cast<Func*>(userData))(index);
        };
#ifdef _WIN32
        EnterCriticalSection(&mutex_cs_);
        current_job_func_  = wrapper;
        current_user_data_ = static_cast<void*>(&func);
        total_items_.store(count,   rt_mo::relaxed);
        next_item_idx_.store(0,     rt_mo::relaxed);
        active_workers_.store(0,    rt_mo::relaxed);
        job_active_.store(true,     rt_mo::release);
        LeaveCriticalSection(&mutex_cs_);
        WakeAllConditionVariable(&cv_start_);
#else
        pthread_mutex_lock(&mutex_);
        current_job_func_  = wrapper;
        current_user_data_ = static_cast<void*>(&func);
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

private:
    void workerMain(int core_id);
    void workerLoop(bool isMainThread);

#ifdef _WIN32
    PodVector<HANDLE>   threads_;
    CRITICAL_SECTION    mutex_cs_;
    CONDITION_VARIABLE  cv_start_;
    CONDITION_VARIABLE  cv_done_;

    struct Win32ThreadArg { ParallelExecutor* self; int core_id; };
    static DWORD WINAPI win32ThreadProc(LPVOID param);
#else
    PodVector<pthread_t>              threads_;
    alignas(CACHE_LINE_SIZE) pthread_mutex_t mutex_;
    pthread_cond_t                    cv_start_;
    pthread_cond_t                    cv_done_;
    struct PthreadArg { ParallelExecutor* self; int core_id; };
    static void* pthreadProc(void* param);
#endif

    JobFunc  current_job_func_  = nullptr;
    void*    current_user_data_ = nullptr;

    AtomicInt total_items_;

    alignas(CACHE_LINE_SIZE) AtomicInt  next_item_idx_;
    alignas(CACHE_LINE_SIZE) AtomicInt  active_workers_;
    alignas(CACHE_LINE_SIZE) AtomicBool job_active_;
    alignas(CACHE_LINE_SIZE) AtomicBool shutdown_;
};

#endif