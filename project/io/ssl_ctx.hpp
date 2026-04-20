#ifndef JETBLACK_IO_SSL_CTX_HPP
#define JETBLACK_IO_SSL_CTX_HPP

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <stdexcept> 
#include <string>
#include <utility>

#include "io/openssl_error.hpp"

namespace jetblack::io
{

  class SslContext
  {
  protected:
    SSL_CTX* ctx_;

  public:
    SslContext() = delete;

    SslContext(const SSL_METHOD* method) noexcept
      : ctx_(SSL_CTX_new(method))
    {
    }

    ~SslContext() noexcept
    {
      SSL_CTX_free(ctx_);
      ctx_ = nullptr;
    }

    SslContext(const SslContext&) = delete;

    SslContext(SslContext&& other) noexcept
    {
      ctx_ = other.ctx_;
      other.ctx_ = nullptr;
    }

    SslContext& operator=(const SslContext&) = delete;

    SslContext& operator=(SslContext&& other) noexcept
    {
      ctx_ = other.ctx_;
      other.ctx_ = nullptr;
      return *this;
    }

    SSL_CTX* ptr() noexcept { return ctx_; }

    void min_proto_version(int version)
    {
      if (SSL_CTX_set_min_proto_version(ctx_, version) == 0)
      {
        throw std::runtime_error("failed to set the minimum ssl protocol version]");
      }
    }
    int min_proto_version() const noexcept { return SSL_CTX_get_min_proto_version(ctx_); }

    void max_proto_version(int version)
    {
      if (SSL_CTX_set_max_proto_version(ctx_, version) == 0)
      {
        throw std::runtime_error("failed to set the minimum ssl protocol version]");
      }
    }
    int max_proto_version() const noexcept { return SSL_CTX_get_max_proto_version(ctx_); }
  };

  class SslClientContext : public SslContext
  {
  public:
    SslClientContext() noexcept
      : SslContext(TLS_client_method())
    {
    }

    SslClientContext(SslClientContext&& other) noexcept
      : SslContext(std::move(other))
    {
    }

    SslClientContext& operator = (SslClientContext&& other) noexcept
    {
      SslContext::operator=(std::move(other));
      return *this;
    }

    void verify(int mode = SSL_VERIFY_PEER) noexcept
    {
      SSL_CTX_set_verify(ctx_, mode, nullptr);
    }

    void load_verify_locations(const std::string& path)
    {
      if (SSL_CTX_load_verify_locations(ctx_, path.c_str(), nullptr) == 0)
      {
        throw std::runtime_error(openssl_strerror());
      }
    }

    void set_default_verify_paths()
    {
      if (SSL_CTX_set_default_verify_paths(ctx_) == 0)
      {
        throw std::runtime_error(openssl_strerror());
      }
    }
  };

  class SslServerContext : public SslContext
  {
  public:
    SslServerContext() noexcept
      : SslContext(TLS_server_method())
    {
    }

    SslServerContext(SslServerContext&& other) noexcept
      : SslContext(std::move(other))
    {
    }
    
    SslServerContext& operator = (SslServerContext&& other) noexcept
    {
      SslContext::operator=(std::move(other));
      return *this;
    }
    
    void use_certificate_file(const std::string& path, int type = SSL_FILETYPE_PEM)
    {
      if (SSL_CTX_use_certificate_file(ctx_, path.c_str(), type) <= 0)
      {
        throw std::runtime_error(openssl_strerror());
      }
    }
    
    void use_certificate_chain_file(const std::string& path)
    {
      if (SSL_CTX_use_certificate_chain_file(ctx_, path.c_str()) <= 0)
      {
        throw std::runtime_error(openssl_strerror());
      }
    }
    
    void use_private_key_file(const std::string& path, int type = SSL_FILETYPE_PEM)
    {
      if (SSL_CTX_use_PrivateKey_file(ctx_, path.c_str(), type) <= 0)
      {
        throw std::runtime_error(openssl_strerror());
      }
    }
  };
}

#endif // JETBLACK_IO_SSL_CTX_HPP
