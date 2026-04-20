#ifndef JETBLACK_IO_POLLER_HPP
#define JETBLACK_IO_POLLER_HPP

#include <poll.h>

#include <deque>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include "io/logger.hpp"
#include "io/poll_handler.hpp"

namespace jetblack::io
{
  
  inline int poll(std::vector<pollfd> &fds)
  {
    log.trace("polling");

    int active_fd_count = ::poll(fds.data(), fds.size(), -1);
    if (active_fd_count < 0)
    {
      throw std::system_error(errno, std::generic_category(), "poll failed");
    }
    return active_fd_count;
  }

  class Poller
  {
  public:
    typedef std::unique_ptr<PollHandler> handler_pointer;
    typedef std::map<int, handler_pointer> handler_map;
    typedef std::function<void(Poller&, int fd)> connection_callback;
    typedef std::function<void(Poller&, int fd, std::vector<std::vector<char>>&& bufs)> read_callback;
    typedef std::function<void(Poller&, int fd, std::exception)> error_callback;

  private:
    handler_map handlers_;
    connection_callback on_open_;
    connection_callback on_close_;
    read_callback on_read_;
    error_callback on_error_;

  public:
    Poller(
      connection_callback on_open,
      connection_callback on_close,
      read_callback on_read,
      error_callback on_error)
      : on_open_(on_open),
        on_close_(on_close),
        on_read_(on_read),
        on_error_(on_error)
    {
    }

    void add_handler(handler_pointer handler) noexcept
    {
      int fd = handler->fd();
      bool is_listener = handler->is_listener();
      handlers_[fd] = std::move(handler);
      if (!is_listener)
        on_open_(*this, fd);
    }

    void write(int fd, const std::vector<char>& buf) noexcept
    {
      if (auto i = handlers_.find(fd); i != handlers_.end())
      {
        i->second->enqueue(buf);
      }
    }

    void close(int fd) noexcept
    {
      if (auto i = handlers_.find(fd); i != handlers_.end())
      {
        i->second->close();
      }
    }

    void event_loop()
    {
      bool is_ok = true;

      while (is_ok) {

        std::vector<pollfd> fds = make_poll_fds();

        int active_fd_count = poll(fds);

        for (const auto& poll_state : fds)
        {
          if (poll_state.revents == 0)
          {
            continue; // no events for file descriptor.
          }

          handle_event(poll_state);

          if (--active_fd_count == 0)
            break;
        }

        remove_closed_handlers();
      }
    }

  private:

    void handle_event(const pollfd& poll_state)
    {
      auto handler = handlers_[poll_state.fd].get();

      if ((poll_state.revents & POLLIN) == POLLIN)
      {
        if (handler->is_listener())
        {
          handler->read(*this);
          return;
        }
        
        if (!handle_read(handler))
          return;
      }

      if ((poll_state.revents & POLLOUT) == POLLOUT)
      {
        if (!handle_write(handler))
          return;
      }
    }

    bool handle_read(PollHandler* handler) noexcept
    {
      log.trace(std::format("handling read for {}", handler->fd()));

      try
      {
        auto can_continue = handler->read(*this);

        std::vector<std::vector<char>> bufs;
        auto buf = handler->dequeue();
        while (buf)
        {
          bufs.push_back(*buf);
          buf = handler->dequeue();
        }

        if (!bufs.empty())
        {
          on_read_(*this, handler->fd(), std::move(bufs));
        }

        return can_continue;
      }
      catch(const std::exception& error)
      {
        on_error_(*this, handler->fd(), error);
        return false;
      }
    }

    bool handle_write(PollHandler* handler) noexcept
    {
      log.trace(std::format("handling write for {}", handler->fd()));

      try
      {
        return handler->write();
      }
      catch(const std::exception& error)
      {
        on_error_(*this, handler->fd(), error);
        return false;
      }
    }

    std::vector<pollfd> make_poll_fds()
    {
      std::vector<pollfd> fds;

      for (auto& [fd, handler] : handlers_)
      {
        int16_t flags = POLLPRI | POLLERR | POLLHUP | POLLNVAL;

        if (handler->want_read())
        {
            flags |= POLLIN;
        }

        if (handler->want_write())
        {
            flags |= POLLOUT;
        }

        fds.push_back(pollfd{fd, flags, 0});
      }

      return fds;
    }

    void remove_closed_handlers()
    {
      auto closed_fds = find_closed_handler_fds();
      remove_closed_handlers(closed_fds);
    }

    std::vector<int> find_closed_handler_fds()
    {
      std::vector<int> closed_fds;
      for (auto& [fd, handler] : handlers_)
      {
        if (!handler->is_open())
        {
          closed_fds.push_back(fd);
        }
      }
      return closed_fds;
    }

    void remove_closed_handlers(const std::vector<int>& closed_fds)
    {
      for (auto fd : closed_fds)
      {
        auto handler = std::move(handlers_[fd]);
        handlers_.erase(fd);
        if (!handler->is_listener())
          on_close_(*this, handler->fd());
      }
    }
  };
}

#endif // JETBLACK_IO_POLLER_HPP
