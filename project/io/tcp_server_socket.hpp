#ifndef JETBLACK_IO_TCP_SERVER_SOCKET_HPP
#define JETBLACK_IO_TCP_SERVER_SOCKET_HPP

#include <cstdint>
#include <string>

#include "io/tcp_socket.hpp"

namespace jetblack::io
{

  class TcpServerSocket : public TcpSocket
  {
  private:
    std::string address_;
    std::uint16_t port_;

  public:
    TcpServerSocket(int fd, const std::string& address, std::uint16_t port) noexcept
      : TcpSocket(fd)
      , address_(address)
      , port_(port)
    {
    }

    const std::string& address() const noexcept { return address_; }
    uint16_t port() const noexcept { return port_; }
  };

}

#endif // JETBLACK_IO_TCP_SERVER_SOCKET_HPP
