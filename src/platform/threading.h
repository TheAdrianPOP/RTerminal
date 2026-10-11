#ifndef THREADING_H
#define THREADING_H

#include "vector.h"
#include "atomic.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
    #include <unistd.h>   // sysconf(_SC_NPROCESSORS_ONLN)
#endif

constexpr int CACHE_LINE_SIZE = 64;
constexpr int WORK_BATCH_SIZE = 8;
constexpr int SPIN_LIMIT      = 400;

class Thread {
public:
    using Func = void (*)(void*);
    static void spawn(Func func, void* arg);
};

class ParallelExecutor {
public:
    explicit ParallelExecutor(size_t thread_count = 0);
    ~ParallelExecutor();

    size_t getThreadCount() const noexcept { return threads_.size() + 1; }

    using JobFunc = void (*)(int, void*);

    // OPTIMIZATION: Template only sets up the trampoline wrapper, 
    // delegating the heavy platform-specific sync to runInternal.
    template<typename Func>
    void run(int count, Func&& func) {
        if (count <= 0) return;
        auto wrapper = +[](int index, void* userData) {
            (*static_cast<Func*>(userData))(index);
        };
        runInternal(count, wrapper, static_cast<void*>(&func));
    }

private:
    void runInternal(int count, JobFunc wrapper, void* userData);
    void workerLoop(bool isMainThread);

#ifdef _WIN32
    PodVector<HANDLE>   threads_;
    CRITICAL_SECTION    mutex_cs_;
    CONDITION_VARIABLE  cv_start_;
    CONDITION_VARIABLE  cv_done_;

    static DWORD WINAPI win32ThreadProc(LPVOID param);
#else
    PodVector<pthread_t>              threads_;
    alignas(CACHE_LINE_SIZE) pthread_mutex_t mutex_;
    pthread_cond_t                    cv_start_;
    pthread_cond_t                    cv_done_;

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