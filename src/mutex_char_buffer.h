#pragma once

#include <string>
#include <queue>
#include <mutex>

namespace mutex_char_buffer {

class MutexCharBuffer
{
    public:
        void append(const std::string& str);

        void push(char item);

        char pop();

        bool empty() const;

    private:
        std::queue<char> _data;
        std::mutex _mutex;
};

}