#ifndef JETBLACK_IO_TCP_ADDRESS_HPP
#define JETBLACK_IO_TCP_ADDRESS_HPP

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <charconv>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace jetblack::io
{

  inline std::string to_string(std::uint16_t value)
  {
      char buf[6];
      std::memset(buf, 0, sizeof(buf));
      auto res = std::to_chars(buf, buf + sizeof(buf) - 1, value);
      if (res.ec != std::errc{})
      {
          throw std::system_error(std::make_error_code(res.ec));
      }
      return std::string(static_cast<const char*>(buf));
  }

  inline std::string to_string(const in_addr& addr)
  {
      char ip_address[INET_ADDRSTRLEN];
      if (inet_ntop(AF_INET, &addr, ip_address, sizeof(ip_address)) == nullptr)
      {
          throw std::system_error(std::make_error_code(std::errc(errno)));
      }
      return (std::string(static_cast<const char*>(ip_address)));
  }

  inline std::string to_string(const sockaddr_in& addr)
  {
      return to_string(addr.sin_addr) + ":" + to_string(ntohs(addr.sin_port));
  }

  inline sockaddr_in to_sockaddr_in(const in_addr& host, uint16_t port)
  {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    std::memcpy(&addr.sin_addr, &host, sizeof(host));
    addr.sin_port = htons(port);

    return addr;
  }

  inline std::ostream& operator << (std::ostream& os, const sockaddr_in& addr)
  {
      return os << to_string(addr);
  }

  inline std::vector<sockaddr_in> getaddrinfo_inet4(const std::string& host, std::uint16_t port)
  {
      std::vector<sockaddr_in> results;

      auto port_str = to_string(port);

      addrinfo hints {
          .ai_flags = 0,
          .ai_family = AF_INET,
          .ai_socktype = SOCK_STREAM,
          .ai_protocol = IPPROTO_TCP,
          .ai_addrlen = 0,
          .ai_addr = nullptr,
          .ai_canonname = nullptr,
          .ai_next = nullptr
      };
      addrinfo* addresses;
      int result = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &addresses);
      if (result != 0)
      {
          throw std::runtime_error(gai_strerror(result));
      }

      for (addrinfo* i = addresses; i != nullptr; i = i->ai_next)
      {
          results.push_back(*reinterpret_cast<sockaddr_in*>(i->ai_addr));
      }

      freeaddrinfo(addresses);

      return results;
  }

}

#endif // JETBLACK_IO_TCP_ADDRESS_HPP
