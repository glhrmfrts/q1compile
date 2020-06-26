#include "mutex_char_buffer.h"


void MutexCharBuffer::append(const std::string& str)
{
    for (char c : str) {
        push(c);
    }
}

void MutexCharBuffer::push(char item)
{
    std::lock_guard<std::mutex> lock{ _mutex };
    _data.push(item);
}

char MutexCharBuffer::pop()
{
    std::lock_guard<std::mutex> lock{ _mutex };
    char item = _data.front();
    _data.pop();
    return item;
}

bool MutexCharBuffer::empty() const
{
    return _data.empty();
}
