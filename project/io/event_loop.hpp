#ifndef JETBLACK_IO_EVENT_LOOP_HPP
#define JETBLACK_IO_EVENT_LOOP_HPP

#include <fcntl.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include "io/logger.hpp"

namespace jetblack::io
{
    class EventLoop
    {
    public:
        enum class EventType { READ, WRITE };
        typedef std::function<void(EventLoop& event_loop, int fd)> fd_callback_t;
        typedef std::map<int, std::map<EventType, std::vector<fd_callback_t>>> fd_callback_map_t;

        typedef std::function<void(EventLoop& event_loop)> timeout_callback_t;
        typedef std::vector<timeout_callback_t> timeout_callbacks_t;

    private:
        fd_callback_map_t fd_callbacks_;
        timeout_callbacks_t timeout_callbacks_;

    public:
        void add_fd_callback(int fd, EventType event_type, fd_callback_t callback)
        {
            if (fd_callbacks_.find(fd) == fd_callbacks_.end())
                fd_callbacks_[fd]= {};
            auto& fd_callbacks = fd_callbacks_[fd];
            if (fd_callbacks.find(event_type) == fd_callbacks.end())
                fd_callbacks[event_type] = { callback };
            else
                fd_callbacks[event_type].push_back(callback);
        }

        void add_timeout_callback(timeout_callback_t callback)
        {
            timeout_callbacks_.push_back(callback);
        }

        void start(int timeout)
        {
            // Create the queue.
            auto efd = epoll_create1(0);
            if (efd == -1)
            {
                throw std::system_error(std::make_error_code(std::errc(errno)));
            }

            while (!fd_callbacks_.empty())
            {
                // Make a vector for the file descriptors.
                std::vector<epoll_event> events(fd_callbacks_.size());

                // Wait for events.
                auto nfds = epoll_wait(efd, events.data(), events.size(), timeout);

                if (nfds < 0)
                {
                    // Exit on errors.
                    throw std::system_error(std::make_error_code(std::errc(errno)));
                }

                if (nfds == 0)
                {
                    auto callbacks = timeout_callbacks_t{ std::move(timeout_callbacks_)};
                    for (auto& callback : callbacks)
                        callback(*this);
                    continue;
                }

                // Go through each of the sockets. Decrement the active fds for early termination.
                for (auto i = 0; i < nfds; ++i)
                {
                    auto& event = events[i];
                    auto callbacks = take_fd_callbacks(events[i]);

                    for (auto& callback : callbacks)
                        callback(*this, event.data.fd);
                }
            }
        }

    private:
        std::vector<fd_callback_t> take_fd_callbacks(const epoll_event& event)
        {
            std::vector<fd_callback_t> callables;

            if ((event.events & EPOLLIN) == EPOLLIN)
            {
                take_fd_event_callbacks(callables, event.data.fd, EventType::READ);
            }
            if ((event.events & EPOLLIN) == EPOLLIN)
            {
                take_fd_event_callbacks(callables, event.data.fd, EventType::WRITE);
            }
            if (fd_callbacks_[event.data.fd].empty())
            {
                fd_callbacks_.erase(event.data.fd);
            }

            return callables;
        }

        void take_fd_event_callbacks(std::vector<fd_callback_t> dest, int fd, EventType event_type)
        {
            auto&& callbacks = std::move(fd_callbacks_[fd].extract(event_type).mapped());
            dest.insert(
                dest.end(),
                std::make_move_iterator(callbacks.begin()),
                std::make_move_iterator(callbacks.end())
            );

        }
    };
}

#endif // JETBLACK_IO_EVENT_LOOP_HPP
