#pragma once

#include <queue>
#include <mutex>

template<class T> class MutexQueue
{
    public:
        void push(const T& item);

        T pop();

        bool empty() const;

    private:
        std::queue<T> _data;
        std::mutex _mutex;
};

template<class T> inline void MutexQueue<T>::push(const T& item)
{
    std::lock_guard<std::mutex> lock{ _mutex };
    _data.push(item);
}

template<class T> inline T MutexQueue<T>::pop()
{
    std::lock_guard<std::mutex> lock{ _mutex };
    T item = _data.front();
    _data.pop();
    return item;
}

template<class T> inline bool MutexQueue<T>::empty() const
{
    return _data.empty();
}
