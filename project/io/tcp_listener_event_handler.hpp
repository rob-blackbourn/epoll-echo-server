#ifndef JETBLACK_IO_LISTENER_POLL_HANDLER_HPP
#define JETBLACK_IO_LISTENER_POLL_HANDLER_HPP

#include <poll.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>

#include "utils/match.hpp"

#include "io/tcp_socket.hpp"
#include "io/tcp_listener_socket.hpp"
#include "io/tcp_stream.hpp"
#include "io/tcp_socket_event_handler.hpp"

namespace jetblack::io
{
  class TcpListenerEventHandler
  {
  private:
    std::shared_ptr<EventLoop> event_loop_;
    std::optional<std::shared_ptr<SslContext>> ssl_ctx_;
    TcpListenerSocket listener_;

  public:
    TcpListenerEventHandler(
      uint16_t port,
      std::optional<std::shared_ptr<SslContext>> ssl_ctx = std::nullopt,
      int backlog = 10)
      : event_loop_(std::make_shared<EventLoop>()),
        ssl_ctx_ { ssl_ctx }
    {
      listener_.bind(port);
      listener_.blocking(false);
      listener_.reuseaddr(true);
      listener_.listen(backlog);
    }
    ~TcpListenerEventHandler()
    {
    }

    int fd() const noexcept { return listener_.fd(); }
    bool is_open() const noexcept { return listener_.is_open(); }

    bool want_read() const noexcept { return true; }
    bool want_write() const noexcept { return false; }

    bool read()
    {
      auto client = listener_.accept();
      client->blocking(false);

      if (!ssl_ctx_)
      {
        event_loop_->add_fd_handler(
          std::make_unique<TcpSocketEventHandler>(std::move(client), 8096, 8096));
      }
      else
      {
        poller.add_handler(
          std::make_unique<TcpSocketEventHandler>(std::move(client), *ssl_ctx_, 8096, 8096));
      }

      return true;
    }

    bool write() override { return false; }

    void close() override
    {
      if (listener_.is_open())
      {
        listener_.close();
      }
    }

    std::optional<std::vector<char>> dequeue() noexcept override { return std::nullopt; }
    void enqueue([[maybe_unused]] const std::vector<char>& buf) noexcept override {}
  };

}

#endif // JETBLACK_IO_LISTENER_POLL_HANDLER_HPP
