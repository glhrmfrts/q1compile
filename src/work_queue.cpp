#include "work_queue.h"

namespace work_queue {

void WorkQueue::ThreadLoop(typename WorkQueue::ThreadData* data) {
    auto wq = data->work_queue;

    for (;;) {
        typename WorkQueue::WorkData work;
        {
            std::unique_lock<std::mutex> lock(data->mutex);
            data->condition_var.wait(lock, [data] { return !data->jobs.empty() || data->stop; });
            if (data->stop) return;

            work = data->jobs.front();
            data->jobs.pop();
        }
        work.func();
        if (data->stop) return;

        wq->_pending_jobs--;
        //wq->_workFinished.emit(work);
    }
}

WorkQueue::WorkQueue(std::size_t max_threads, std::size_t max_queue_size)
    : _max_threads(max_threads), _max_queue_size(max_queue_size), _next_thread_idx(0), _pending_jobs(0) {
}

WorkQueue::~WorkQueue() {
    for (auto& data : _threads) {
        std::unique_lock<std::mutex> lock(data.mutex);
        data.stop = true;
        data.condition_var.notify_one();
    }
}

bool WorkQueue::AddWork(int work_type, typename WorkQueue::WorkFuncType func, void* user_data) {
    typename WorkQueue::WorkData work = { func, work_type, user_data };

    std::size_t i = 0;
    for (auto& data : _threads) {
        if (i++ < _next_thread_idx) continue;

        std::unique_lock<std::mutex> lock(data.mutex, std::defer_lock);
        lock.lock();
        if (data.jobs.size() < _max_queue_size) {
            data.jobs.push(work);
            _pending_jobs++;

            lock.unlock();
            data.condition_var.notify_one();
            _next_thread_idx = i % _threads.size();
            return true;
        } else {
            lock.unlock();
        }
    }

    if (_threads.size() < _max_threads) {
        _threads.push_back(ThreadData{});

        ThreadData& data = _threads.back();
        data.jobs.push(work);
        data.work_queue = this;
        data.thread = std::thread(ThreadLoop, &data);
        data.condition_var.notify_one();

        _pending_jobs++;
        return true;
    }

    return false;
}

void WorkQueue::SetWorkFinishedCallback(CallbackFuncType callback) {
    
}

void WorkQueue::Update() {
}

}