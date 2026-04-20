#ifndef JETBLACK_IO_TCP_STREAM_HPP
#define JETBLACK_IO_TCP_STREAM_HPP

#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

#include <cerrno>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "utils/match.hpp"

#include "io/tcp_socket.hpp"
#include "io/file_types.hpp"
#include "io/ssl_ctx.hpp"
#include "io/ssl.hpp"
#include "io/bio.hpp"

namespace jetblack::io
{
  using jetblack::utils::match;

  class TcpStream
  {
  public:

    typedef std::shared_ptr<TcpSocket> socket_pointer;

    enum class State
    {
      START,
      HANDSHAKE,
      DATA,
      SHUTDOWN,
      STOP
    };

  private:

    Bio bio_;
    bool should_verify_;
    State state_ { State::START };

  public:

    socket_pointer socket;

  public:
  
    TcpStream(socket_pointer socket, bool should_verify)
      : bio_(*socket, BIO_NOCLOSE),
        should_verify_(should_verify),
        socket(std::move(socket))
    {
    }

    TcpStream(socket_pointer socket, std::shared_ptr<SslContext> ssl_ctx, bool is_client)
      : TcpStream(socket, is_client)
    {
        state_ = State::HANDSHAKE;
        bio_.push_ssl(*ssl_ctx, is_client);
    }

    TcpStream(socket_pointer socket, std::shared_ptr<SslContext> ssl_ctx, const std::string& server_name)
      : TcpStream(socket, ssl_ctx, true)
    {
      // Set hostname for SNI.
      bio_.ssl->tlsext_host_name(server_name);
      bio_.ssl->host(server_name);
    }

    static TcpStream make(
      socket_pointer socket,
      std::optional<std::shared_ptr<SslContext>> ssl_ctx,
      std::optional<std::string> server_name)
    {
      if (!ssl_ctx)
      {
        return TcpStream(socket, false);
      }
      else
      {
        if (!server_name)
          return TcpStream(socket, *ssl_ctx, false);
        else
          return TcpStream(socket, *ssl_ctx, *server_name);
      }
    }

    bool want_read() const noexcept { return bio_.should_read(); }
    bool want_write() const noexcept{ return bio_.should_write(); }

    bool do_handshake()
    {
      if (!bio_.ssl || state_ != State::HANDSHAKE)
      {
        return true; // continue processing reads.
      }

      bool is_done = std::visit(
        match {
          
          [](blocked&&)
          {
            return false;
          },
          
          [](bool is_complete)
          {
            return is_complete;
          }
        },
        bio_.ssl->do_handshake()
      );

      if (is_done)
      {
        state_ = State::DATA;
        if (should_verify_)
        {
          bio_.ssl->verify();
        }
      }

      return is_done;
    }

    void verify()
    {
      if (!bio_.ssl)
      {
        return;
      }

      bio_.ssl->verify();
    }

    bool do_shutdown()
    {
      if (state_ == State::DATA)
      {
        // Transition to shutdown state.
        state_ = State::SHUTDOWN;
      }

      if (state_ == State::STOP)
      {
        return true;
      }

      if (state_ != State::SHUTDOWN)
      {
        throw std::runtime_error("shutdown in invalid state");
      }

      if (!bio_.ssl)
      {
        state_ = State::STOP;
        return true;
      }

      if (!bio_.ssl || state_ != State::SHUTDOWN)
      {
        return true;
      }

      return handle_shutdown();
    }

    std::variant<std::vector<char>, eof, blocked> read(std::size_t len)
    {
      if (state_ == State::HANDSHAKE)
      {
        if (!do_handshake())
          return blocked {};
      }

      if (state_ == State::SHUTDOWN)
      {
        if (handle_shutdown())
          return eof {};
        else
          return blocked {};
      }

      std::vector<char> buf(len);

      std::optional<std::size_t> nbytes_read = bio_.read(buf);
      if (!nbytes_read) {
        if (bio_.should_retry())
        {
          // The socket is ok, but nothing has been read due to blocking.
          return blocked {};
        }

        if (bio_.ssl && bio_.ssl->error() == SSL_ERROR_ZERO_RETURN)
        {
          // The client has initiated an SSL shutdown.
          state_ = State::SHUTDOWN;
          if (handle_shutdown())
            return eof {};
          else
            return blocked {};
        }

        // The socket has faulted.
        handle_client_faulted();
        socket->is_open(false);
        return eof {};
      }

      if (*nbytes_read == 0) {
        // A read of zero bytes indicates socket has closed.
        socket->is_open(false);
        return eof {};
      }

      // Data has been read successfully. Resize the buffer and return.
      buf.resize(*nbytes_read);
      return buf;
    }

    std::variant<std::size_t, eof, blocked> write(const std::span<char>& buf)
    {
      if (state_ == State::HANDSHAKE)
      {
        if (!do_handshake())
          return blocked {};
      }

      if (state_ == State::SHUTDOWN)
      {
        if (handle_shutdown())
          return eof {};
        else
          return blocked {};
      }

      std::optional<std::size_t> nbytes_written = bio_.write(buf);
      if (!nbytes_written)
      {
        // Check if it's flow control.
        if (!bio_.should_retry()) {
          // Not flow control; the socket has faulted.
          socket->is_open(false);
          handle_client_faulted();
          throw std::runtime_error("failed to write");
        }

        // The socket is ok, but nothing has been written due to blocking.
        return blocked {};
      }

      if (*nbytes_written == 0)
      {
        // A write of zero bytes indicates socket has closed.
        socket->is_open(false);
        return eof {};
      }

      // return the number of bytes that were written.
      return *nbytes_written;
    }

  private:
    void handle_client_faulted()
    {
      if (bio_.ssl)
      {
        // This stops BIO_free_all (via SSL_SHUTDOWN) from raising SIGPIPE.
        bio_.ssl->quiet_shutdown(true);
      }
    }

    bool handle_shutdown()
    {
      if (!bio_.ssl)
      {
        state_ = State::STOP;
        return true;
      }

      bool is_done = std::visit(
        match {

          [](blocked&&)
          {
            return false;
          },

          [](bool is_completed)
          {
            return is_completed;
          }

        },
        bio_.ssl->shutdown()
      );

      if (is_done)
      {
        socket->is_open(false);
        state_ = State::STOP;
      }

      return is_done;
    }
  };
}

#endif // JETBLACK_IO_TCP_STREAM_HPP
