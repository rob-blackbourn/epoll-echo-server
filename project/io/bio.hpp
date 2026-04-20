#ifndef JETBLACK_IO_BIO_HPP
#define JETBLACK_IO_BIO_HPP

#include <optional>
#include <span>
#include <utility>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

#include "io/tcp_socket.hpp"
#include "io/ssl_ctx.hpp"
#include "io/ssl.hpp"

namespace jetblack::io
{
  
  class Bio
  {
  private:

    BIO* bio_;

  public:

    std::optional<Ssl> ssl;

  public:

    Bio(BIO* bio)
      : bio_(bio)
    {
    }

    ~Bio()
    {
      BIO_free_all(bio_);
      bio_ = nullptr;
    }

    Bio(const Bio&) = delete;
    
    Bio& operator=(const Bio&) = delete;
    
    Bio(Bio&& other)
    {
      *this = std::move(other);
    }
    
    Bio& operator=(Bio&& other)
    {
      bio_ = other.bio_;
      other.bio_ = nullptr;
      return *this;
    }

    Bio(const TcpSocket& socket, int close_flag = BIO_NOCLOSE)
      : Bio(BIO_new_socket(socket.fd(), close_flag))
    {
    }

    void push_ssl(SslContext& ssl_ctx, bool is_client)
    {
        BIO* ssl_bio = BIO_new_ssl(ssl_ctx.ptr(), is_client);
        BIO_push(ssl_bio, bio_);
        bio_ = ssl_bio;
        SSL* ssl_ptr = nullptr;
        BIO_get_ssl(bio_, &ssl_ptr);
        ssl = Ssl(ssl_ptr, false);
    }

    bool should_retry() const noexcept { return BIO_should_retry(bio_); }
    bool should_read() const noexcept { return BIO_should_read(bio_); }
    bool should_write() const noexcept{ return BIO_should_write(bio_); }

    std::optional<std::size_t> read(const std::span<char>& buf)
    {
      std::size_t readbytes;
      if (BIO_read_ex(bio_, buf.data(), buf.size(), &readbytes) == 0)
      {
        return std::nullopt;
      }
      return readbytes;
    }

    std::optional<std::size_t> write(const std::span<char>& buf)
    {
      std::size_t written;
      if (BIO_write_ex(bio_, buf.data(), buf.size(), &written) == 0)
      {
        return std::nullopt;
      }
      return written;
    }
  };

}

#endif // JETBLACK_IO_BIO_HPP
