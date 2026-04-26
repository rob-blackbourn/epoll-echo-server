#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include "io/logger.hpp"

namespace jetblack::io
{
  class EventLoop
  {
  public:
    enum class EventType { READ, WRITE };
    typedef std::function<void(int fd)> fd_callback_t;
    typedef std::map<int, std::map<EventType, std::vector<fd_callback_t>>> fd_callback_map_t;

    typedef std::function<void(EventLoop& event_loop)> timeout_callback_t;
    typedef std::vector<timeout_callback_t> timeout_callbacks_t;

  private:
    fd_callback_map_t fd_callbacks_;
    timeout_callbacks_t timeout_callbacks_;
    std::map<int, uint32_t> event_state_;

  public:
    void add_fd_callback(int fd, EventType event_type, fd_callback_t callback)
    {
      if (fd_callbacks_.find(fd) == fd_callbacks_.end())
        fd_callbacks_[fd]= {};
      auto& fd_callbacks = fd_callbacks_[fd];
      if (fd_callbacks.find(event_type) == fd_callbacks.end())
        fd_callbacks[event_type] = { callback };
      else
        fd_callbacks[event_type].push_back(callback);
    }

    void add_timeout_callback(timeout_callback_t callback)
    {
      timeout_callbacks_.push_back(callback);
    }

    void start(int timeout)
    {
      // Create the queue.
      auto efd = epoll_create1(0);
      if (efd == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }

      while (!fd_callbacks_.empty())
      {
        prepare_events(efd);

        // Make a vector for the file descriptors.
        std::vector<epoll_event> events(fd_callbacks_.size());

        // Wait for events.
        auto nfds = epoll_wait(efd, events.data(), events.size(), timeout);

        if (nfds < 0)
        {
          // Exit on errors.
          throw std::system_error(std::make_error_code(std::errc(errno)));
        }

        if (nfds == 0)
        {
          auto callbacks = timeout_callbacks_t{ std::move(timeout_callbacks_)};
          for (auto& callback : callbacks)
          {
            callback(*this);
          }
          continue;
        }

        // Go through each of the events.
        for (auto i = 0; i < nfds; ++i)
        {
          auto& event = events[i];
          auto callbacks = take_fd_callbacks(events[i]);

          for (auto& callback : callbacks)
            callback(event.data.fd);
        }

        prune_events(efd);
      }
    }

  private:
    void prepare_events(int efd)
    {
      for (const auto& [fd, entry] : fd_callbacks_)
      {
        auto i_state = event_state_.find(fd);
        if (i_state == event_state_.end())
        {
          // This fd is not being monitored.
          epoll_event ev;
          ev.data.fd = fd;
          ev.events = EPOLLPRI | EPOLLERR | EPOLLHUP | EPOLLET;
          if (entry.find(EventType::READ) != entry.end())
          {
            ev.events |= EPOLLIN;
          }
          if (entry.find(EventType::WRITE) != entry.end())
          {
            ev.events |= EPOLLOUT;
          }
          event_state_[fd] = ev.events;
          if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) == -1)
          {
            throw std::system_error(std::make_error_code(std::errc(errno)));
          }
        }
        else
        {
          // This event is being handled.
          epoll_event ev;
          ev.data.fd = fd;
          ev.events = i_state->second;

          if (entry.find(EventType::READ) != entry.end())
          {
            ev.events |= EPOLLIN;
          }
          else
          {
            ev.events &= ~EPOLLIN;
          }

          if (entry.find(EventType::WRITE) != entry.end())
          {
            ev.events |= EPOLLOUT;
          }
          else
          {
            ev.events &= ~EPOLLOUT;
          }

          if (ev.events != i_state->second)
          {
            event_state_[fd] = ev.events;
            if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) == -1)
            {
              throw std::system_error(std::make_error_code(std::errc(errno)));
            }
          }
        }
      }
    }

    void prune_events(int efd)
    {
      std::vector<int> deletable;
      for (const auto& [fd, _] : event_state_)
      {
        if (fd_callbacks_.find(fd) == fd_callbacks_.end())
        {
          deletable.push_back(fd);
        }
      }
      for (auto fd : deletable)
      {
        if (epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr) == -1)
        {
          throw std::system_error(std::make_error_code(std::errc(errno)));
        }
      }
    }

    std::vector<fd_callback_t> take_fd_callbacks(const epoll_event& event)
    {
      std::vector<fd_callback_t> callables;

      if ((event.events & EPOLLIN) == EPOLLIN)
      {
        take_fd_event_callbacks(callables, event.data.fd, EventType::READ);
      }
      if ((event.events & EPOLLIN) == EPOLLIN)
      {
        take_fd_event_callbacks(callables, event.data.fd, EventType::WRITE);
      }
      if (fd_callbacks_[event.data.fd].empty())
      {
        fd_callbacks_.erase(event.data.fd);
      }

      return callables;
    }

    void take_fd_event_callbacks(std::vector<fd_callback_t> dest, int fd, EventType event_type)
    {
      auto&& callbacks = std::move(fd_callbacks_[fd].extract(event_type).mapped());
      dest.insert(
        dest.end(),
        std::make_move_iterator(callbacks.begin()),
        std::make_move_iterator(callbacks.end())
      );
    }
  };
  
  class File
  {
  protected:
    int fd_;
    int oflag_;
    bool is_open_ { true };

  public:
    explicit File(int fd, int oflag = O_RDWR) noexcept
      : fd_(fd),
        oflag_(oflag)
    {
    }

    File(const File&) = delete;
    File& operator = (const File&) = delete;

    File(File&& other)
    {
      *this = std::move(other);
    }

    File& operator = (File&& other)
    {
      fd_ = other.fd_;
      is_open_ = other.is_open_;

      other.fd_ = -1;
      other.is_open_ = false;

      return *this;
    }

    ~File()
    {
      if (!is_open_)
      {
        try
        {
          close();
        }
        catch(const std::exception& e)
        {
        }
      }
    }

    int fd() const noexcept { return fd_; }

    int oflag() const noexcept { return oflag_; }

    bool is_open() const noexcept { return is_open_; }
    void is_open(bool value) { is_open_ = value; }
    
    void close()
    {
      int result = ::close(fd_);
      if (result == -1) {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
      is_open_ = false;
    }

    int fcntl_flags() const
    {
      int flags = ::fcntl(fd_, F_GETFL, 0);
      if (flags == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
      return flags;
    }

    void fcntl_flags(int flags)
    {
      if (::fcntl(fd_, F_SETFL, flags) == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
    }

    void fcntl_flag(int flag, bool is_add)
    {
      int flags = fcntl_flags();
      flags = is_add ? (flags & ~flag) : (flags | flag);
      fcntl_flags(flags);
    }

    bool blocking() const { return (fcntl_flags() & O_NONBLOCK) == O_NONBLOCK; }
    void blocking(bool is_blocking) { fcntl_flag(O_NONBLOCK, is_blocking); }

    bool is_readonly() const noexcept { return oflag_ == O_RDONLY; }
    bool is_writeonly() const noexcept { return oflag_ == O_WRONLY; }
    bool is_readwrite() const noexcept { return oflag_ == O_RDWR; }

    bool can_read() const noexcept { return is_open_ && (is_readonly() || is_readwrite()); }
    bool can_write() const noexcept { return is_open_ && (is_writeonly() || is_readwrite()); }
  };

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
  class TcpListenerSocket : public TcpSocket
  {
  public:
    typedef std::shared_ptr<TcpServerSocket> client_pointer;

  public:
    TcpListenerSocket()
      : TcpSocket()
    {
    }

    TcpListenerSocket(int) = delete;

    void bind(uint16_t port)
    {
      bind(htonl(INADDR_ANY), port);
    }

    void bind(const std::string& address, uint16_t port)
    {
      in_addr addr;
      int result = inet_pton(AF_INET, address.c_str(), &addr);
      if (result == 0)
      {
        throw std::runtime_error("invalid network address");
      }
      else if (result == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }

      bind(addr.s_addr, port);
    }

    void bind(uint32_t addr, uint16_t port)
    {
      sockaddr_in servaddr;
      std::memset(&servaddr, 0, sizeof(servaddr));
      servaddr.sin_family = AF_INET;
      servaddr.sin_addr.s_addr = addr;
      servaddr.sin_port = htons(port);

      if (::bind(fd_, reinterpret_cast<sockaddr*>(&servaddr), sizeof(servaddr)) == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
    }

    void listen(int backlog = 10)
    {
      if (::listen(fd_, backlog) == -1)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
    }

    client_pointer accept()
    {
      sockaddr_in clientaddr;
      socklen_t clientlen = sizeof(clientaddr);

      int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&clientaddr), &clientlen);
      if (client_fd == -1)
      {
        if (errno == EWOULDBLOCK)
        {
          return client_pointer{};
        }

        throw std::system_error(std::make_error_code(std::errc(errno)));
      }

      char address[INET_ADDRSTRLEN];
      if (inet_ntop(AF_INET, &clientaddr.sin_addr, address, sizeof(address)) == nullptr)
      {
        throw std::system_error(std::make_error_code(std::errc(errno)));
      }
      uint16_t port = ntohs(clientaddr.sin_port);

      return std::make_shared<TcpServerSocket>(client_fd, address, port);
    }
  };

}

using namespace jetblack::io;

struct Buffer
{
    char data[100];
    std::size_t len { 0 };
    std::size_t offset { 0 };
};

class EchoServer
{
private:
  std::unique_ptr<TcpListenerSocket> listener_;
  EventLoop event_loop_;
  std::map<int, TcpListenerSocket::client_pointer> clients_;
  std::map<int, std::deque<std::unique_ptr<Buffer>>> messages_;

public:
  EchoServer()
    : listener_(std::make_unique<TcpListenerSocket>())
  {
  }

  void start(uint16_t port)
  {
    listener_->bind(INADDR_ANY, port);
    listener_->listen(10);

  }

private:
  void handle_accept(EventLoop& event_loop, int fd)
  {
    while (true)
    {
      auto client = listener_->accept();
      if (client.get() == nullptr)
      {
        break;
      }

      event_loop_.add_fd_callback(
        client->fd(),
        EventLoop::EventType::READ,
        [this](int fd)
        {
          this->handle_read(fd);
        });
      clients_[client->fd()] = std::move(client);
    }
  }

  void handle_read(int fd)
  {
    auto& client = clients_[fd];
    if (messages_.find(fd) == messages_.end())
    {
      messages_[fd] = std::deque<std::unique_ptr<Buffer>> {};
    }

    while (true)
    {
      auto buffer = std::make_unique<Buffer>();
      memset(buffer->data, 0, sizeof(buffer->data));
      auto nbytes_read = read(client->fd(), buffer->data, sizeof(buffer->data));
      std::cout << "Received " << buffer->len << " bytes from " << fd << " of \"" << buffer->data << "\"" << std::endl;

      if (nbytes_read == 0)
      {
        close(fd);

        if (messages_.find(fd) != messages_.end())
        {
          messages_.erase(fd);
        }
        return;
      }

      if (nbytes_read == -1)
      {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
          {
              break;
          }

          return;
      }

      buffer->len = nbytes_read;
      buffer->offset = 0;

      messages_[fd].push_back(std::move(buffer));
    }

    if (messages_.find(fd) == messages_.end())
    {
      event_loop_.add_fd_callback(
        fd,
        EventLoop::EventType::WRITE,
        [this](int fd)
        {
          this->handle_write(fd);
        });
    }
  }

  void handle_write(int fd)
  {
    auto& buffers = messages_[fd];

    while (!buffers.empty())
    {
      // Write the data back.
      auto& buffer = buffers.back();
      std::cout << "Echoing back - " << (buffer->data + buffer->offset) << std::endl;
      std::cout << "Writing " << buffer->len << std::endl;
      ssize_t nbytes_written = write(fd, buffer->data + buffer->offset, buffer->len - buffer->offset);
      std::cout << "Wrote " << nbytes_written << " bytes" << std::endl;

      if (nbytes_written <= 0)
      {
          if (errno == EWOULDBLOCK || errno == EAGAIN)
          {
              break;
          }

          return;
      }

      buffer->offset += nbytes_written;
      if (buffer->offset == buffer->len)
      {
          buffers.pop_back();
      }
    }

    if (!buffers.empty())
    {
      event_loop_.add_fd_callback(
        fd,
        EventLoop::EventType::READ,
        [this](int fd)
        {
          this->handle_write(fd);
        });
    }

  }
};

int main()
{
  EventLoop event_loop;

  event_loop.start(5000);

  return 0;
}
