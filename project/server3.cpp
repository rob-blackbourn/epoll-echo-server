#include <arpa/inet.h>

#include "io/logger.hpp"

#include "io/event_loop.hpp"
#include "io/tcp_listener_socket.hpp"

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
    listener_->reuseaddr(true);
    listener_->listen(10);
    event_loop_.add_fd_callback(
      listener_->fd(),
      EventLoop::EventType::READ,
      [this](int fd) { this->handle_accept(fd); });

    event_loop_.start(60 * 1000);
  }

private:
  void handle_accept(int fd)
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
  EchoServer echo_server;

  echo_server.start(8081);

  return 0;
}
