#ifndef JETBLACK_IO_EVENT_LOOP_HPP
#define JETBLACK_IO_EVENT_LOOP_HPP

#include <format>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "io/logger.hpp"

class EventLoop
{
public:
    typedef std::function<void(EventLoop&, int fd)> connect_callback_t;
    typedef std::function<void(EventLoop&, int fd)> read_callback_t;
    typedef std::function<void(EventLoop&, int fd)> write_callback_t;
    typedef std::function<void(EventLoop&, int fd, std::exception)> error_callback_t;

private:
    connect_callback_t connect_callback_;
    std::map<int, std::vector<read_callback_t>> read_callbacks_;
    std::map<int, std::vector<write_callback_t>> write_callbacks_;
    std::map<int, std::vector<error_callback_t>> error_callbacks_;

public:
    EventLoop(connect_callback_t connect_callback)
        : connect_callback_(connect_callback)
    {
    }
    
    void add_read_callback(int fd, read_callback_t cb)
    {
        if (read_callbacks_.find(fd) == read_callbacks_.end())
        {
            read_callbacks_[fd] = std::vector<read_callback_t> { cb };
        }
        else
        {
            read_callbacks_[fd].push_back(cb);
        }
    }
    
    void add_write_callback(int fd, write_callback_t cb)
    {
        write_callbacks_[fd] = cb;
    }
    
    void add_error_callback(int fd, error_callback_t cb)
    {
        read_callbacks_[fd] = cb;
    }
};

#endif // JETBLACK_IO_EVENT_LOOP_HPP
