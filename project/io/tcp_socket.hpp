#ifndef JETBLACK_IO_TCP_SOCKET_HPP
#define JETBLACK_IO_TCP_SOCKET_HPP

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <system_error>

#include "io/file.hpp"

namespace jetblack::io
{

  class TcpSocket : public File
  {
  public:
    explicit TcpSocket()
      : File(socket(AF_INET, SOCK_STREAM, 0))
    {
      if (fd_ == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
    }

    explicit TcpSocket(int fd) noexcept
      : File(fd)
    {
    }

    void set_option(int level, int name, bool is_set) const
    {
      int value = is_set ? 1 : 0;
      if (::setsockopt(fd_, level, name, (void*)&value, sizeof(value)) == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
    }

    void reuseaddr(bool is_reusable)
    {
      set_option(SOL_SOCKET, SO_REUSEADDR, is_reusable);
    }
  };

}

#endif // JETBLACK_IO_TCP_SOCKET_HPP
