#pragma once

#include <atomic>
#include <thread>
#include <future>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <functional>

class WorkQueue
{
    public:
        typedef std::function<void()> WorkFuncType;
        typedef std::function<void(int, void*)> CallbackFuncType;

        struct WorkData
        {
            WorkFuncType func;
            int work_type;
            void* user_data;
        };

        struct ThreadData
        {
            std::queue<WorkData> jobs;
            std::thread thread;
            std::condition_variable condition_var;
            std::mutex mutex;
            WorkQueue* work_queue;
            bool stop = false;

            ThreadData() {}

            ThreadData(const ThreadData&) = delete;

            ThreadData(ThreadData&& rhs) {
                *this = std::move(rhs);
            }

            ~ThreadData() {
                stop = true;
                if (thread.joinable()) thread.join();
            }

            ThreadData& operator =(const ThreadData&) = delete;

            ThreadData& operator =(ThreadData&& rhs) {
                jobs = std::move(rhs.jobs);
                thread = std::move(rhs.thread);
                work_queue = rhs.work_queue;
                stop = rhs.stop;
                return *this;
            }
        };

        static void ThreadLoop(ThreadData*);

        WorkQueue(std::size_t max_threads, std::size_t max_queue_size);
        ~WorkQueue();

        bool AddWork(int work_type, WorkFuncType work, void* user_data = nullptr);

        void SetWorkFinishedCallback(CallbackFuncType callback);

        void Update();

        std::size_t NumThreads() const {
            return _threads.size();
        }

        void Wait() {
            while (_pending_jobs > 0) {}
        }

    private:
        std::vector<ThreadData> _threads;
        std::size_t _max_threads;
        std::size_t _max_queue_size;
        std::size_t _next_thread_idx;
        std::atomic_size_t _pending_jobs;
};

