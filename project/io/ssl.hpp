#ifndef JETBLACK_IO_SSL_HPP
#define JETBLACK_IO_SSL_HPP

#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

#include "io/file_types.hpp"

namespace jetblack::io
{

  class Ssl
  {
  private:

    SSL* ssl_;
    bool is_owner_;

  public:

    Ssl(SSL* ssl, bool is_owner) noexcept
      : ssl_(ssl),
        is_owner_(is_owner)
    {
    }

    ~Ssl() noexcept
    {
      if (is_owner_)
      {
        SSL_free(ssl_);
      }
      ssl_ = nullptr;
    }

    Ssl(const Ssl&) = delete;
    
    Ssl& operator=(const Ssl&) = delete;
    
    Ssl(Ssl&& other) noexcept
    {
      *this = std::move(other);
    }
    
    Ssl& operator=(Ssl&& other) noexcept
    {
      ssl_ = other.ssl_;
      is_owner_ = other.is_owner_;
      other.ssl_ = nullptr;
      return *this;
    }

    int error(int ret = 0) const noexcept
    {
      return SSL_get_error(ssl_, ret);
    }

    static const char* error_code(int error)
    {
      switch (error)
      {
      case SSL_ERROR_NONE:
        return "NONE";
      case SSL_ERROR_ZERO_RETURN:
        return "ZERO_RETURN";
      case SSL_ERROR_WANT_READ:
        return "WANT_READ";
      case SSL_ERROR_WANT_WRITE:
        return "WANT_WRITE";
      case SSL_ERROR_WANT_ACCEPT:
        return "WANT_ACCEPT";
      case SSL_ERROR_WANT_CONNECT:
        return "WANT_CONNECT";
      case SSL_ERROR_WANT_X509_LOOKUP:
        return "WANT_X509_LOOKUP";
      case SSL_ERROR_WANT_ASYNC:
        return "WANT_ASYNC";
      case SSL_ERROR_WANT_ASYNC_JOB:
        return "WANT_ASYNC_JOB";
      case SSL_ERROR_WANT_CLIENT_HELLO_CB:
        return "WANT_CLIENT_HELLO_CB";
      case SSL_ERROR_SYSCALL:
        return "SYSCALL";
      case SSL_ERROR_SSL:
        return "SSL";
      default:
        return "UNKNOWN";
      }
    }

    static const char* error_description(int error)
    {
      switch (error)
      {
      case SSL_ERROR_NONE:
        return "operation completed";
      case SSL_ERROR_ZERO_RETURN:
        return "peer closed connection";
      case SSL_ERROR_WANT_READ:
        return "a read is required";    
      case SSL_ERROR_WANT_WRITE:
        return "a write is required";    
      case SSL_ERROR_WANT_ACCEPT:
        return "an accept would block and should be retried";    
      case SSL_ERROR_WANT_CONNECT:
        return "a connect would block and should be retried";    
      case SSL_ERROR_WANT_X509_LOOKUP:
        return "the callback asked to be called again";    
      case SSL_ERROR_WANT_ASYNC:
        return "the async engine is still processing data";    
      case SSL_ERROR_WANT_ASYNC_JOB:
        return "an async job could not be created";    
      case SSL_ERROR_WANT_CLIENT_HELLO_CB:
        return "the callback asked to be called again";    
      case SSL_ERROR_SYSCALL:
        return "unrecoverable";    
      case SSL_ERROR_SSL:
        return "unrecoverable";    
      default:
        return "unknown";
      }
    }

    void tlsext_host_name(const std::string& host_name)
    {
      // Set hostname for SNI.
      if (SSL_set_tlsext_host_name(ssl_, host_name.c_str()) != 1)
      {
        throw std::runtime_error("failed to set host name for SNI");
      }
    }

    void host(const std::string& host_name)
    {
      if (!SSL_set1_host(ssl_, host_name.c_str()))
      {
        throw std::runtime_error("failed to configure hostname check");
      }
    }

    std::variant<bool, blocked> do_handshake()
    {
      int ret = SSL_do_handshake(ssl_);
      if (ret == 1)
      {
        return true; // handshake complete
      }

      if (ret == 0)
      {
        return false; // handshake in progress.
      }

      auto err = error(ret);
      if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
      {
        return blocked {};
      }

      std::string message = std::format(
        "handshake failed: {} - {}",
        error_code(err),
        error_description(err));
      throw std::runtime_error(message);
    }

    const X509* peer_certificate() const noexcept
    {
      return SSL_get_peer_certificate(ssl_);
    }

    void verify_result_or_throw() const
    {
      int result = SSL_get_verify_result(ssl_);
      if (result != X509_V_OK)
      {
        std::string message = X509_verify_cert_error_string(result);
        throw std::runtime_error(message);
      }
    }

    void verify()
    {
      verify_result_or_throw();

      if (peer_certificate() == nullptr) {
          throw std::runtime_error("no certificate was presented");
      }
    }

    void quiet_shutdown(bool is_quiet) noexcept
    {
      // This stops BIO_free_all (via SSL_SHUTDOWN) from raising SIGPIPE.
      SSL_set_quiet_shutdown(ssl_, is_quiet ? 1 : 0);
    }

    bool quiet_shutdown() const noexcept
    {
      return SSL_get_quiet_shutdown(ssl_) == 1;
    }

    std::variant<bool, blocked> shutdown()
    {
      int retcode = SSL_shutdown(ssl_);

      if (retcode == 1)
      {
        return true; // Shutdown complete.
      }

      if (retcode == 0)
      {
        return false; // A response is required.
      }

      auto err = error(retcode);
      if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
      {
        return blocked {};
      }

      throw std::runtime_error("shutdown failed");
    }
  };

}

#endif // JETBLACK_IO_SSL_HPP
