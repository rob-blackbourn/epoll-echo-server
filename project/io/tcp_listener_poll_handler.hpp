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

#include "io/poll_handler.hpp"
#include "io/poller.hpp"

#include "io/tcp_socket.hpp"
#include "io/tcp_listener_socket.hpp"
#include "io/tcp_stream.hpp"
#include "io/tcp_socket_poll_handler.hpp"

namespace jetblack::io
{
  class TcpListenerPollHandler : public PollHandler
  {
  private:
    std::optional<std::shared_ptr<SslContext>> ssl_ctx_;
    TcpListenerSocket listener_;

  public:
    TcpListenerPollHandler(
      uint16_t port,
      std::optional<std::shared_ptr<SslContext>> ssl_ctx = std::nullopt,
      int backlog = 10)
      : ssl_ctx_ { ssl_ctx }
    {
      listener_.bind(port);
      listener_.blocking(false);
      listener_.reuseaddr(true);
      listener_.listen(backlog);
    }
    ~TcpListenerPollHandler() override
    {
    }

    bool is_listener() const noexcept override { return true; }

    int fd() const noexcept override { return listener_.fd(); }
    bool is_open() const noexcept override { return listener_.is_open(); }

    bool want_read() const noexcept override { return true; }
    bool want_write() const noexcept override { return false; }

    bool read(Poller& poller) override
    {
      auto client = listener_.accept();
      client->blocking(false);

      if (!ssl_ctx_)
      {
        poller.add_handler(
          std::make_unique<TcpSocketPollHandler>(std::move(client), 8096, 8096));
      }
      else
      {
        poller.add_handler(
          std::make_unique<TcpSocketPollHandler>(std::move(client), *ssl_ctx_, 8096, 8096));
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
