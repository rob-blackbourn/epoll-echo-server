#ifndef JETBLACK_IO_FILE_POLL_HANDLER_HPP
#define JETBLACK_IO_FILE_POLL_HANDLER_HPP

#include <poll.h>

#include <cstddef>

#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "utils/match.hpp"

#include "io/poll_handler.hpp"
#include "io/poller.hpp"
#include "io/file.hpp"
#include "io/file_stream.hpp"

namespace jetblack::io
{
  using jetblack::utils::match;

  class FilePollHandler : public PollHandler
  {
  private:
    FileStream stream_;
    std::deque<std::vector<char>> read_queue_;
    std::deque<std::pair<std::vector<char>, std::size_t>> write_queue_;

  public:
    const std::size_t read_bufsiz;
    const std::size_t write_bufsiz;

  public:
    FilePollHandler(
      std::shared_ptr<File> file,
      std::size_t read_bufsiz,
      std::size_t write_bufsiz)
      : stream_(file),
        read_bufsiz(read_bufsiz),
        write_bufsiz(write_bufsiz)
    {
    }
    ~FilePollHandler() override
    {
    }

    bool is_listener() const noexcept override { return false; }
    int fd() const noexcept override { return stream_.file->fd(); }
    bool is_open() const noexcept override { return stream_.file->is_open(); }
    bool want_read() const noexcept override { return stream_.file->can_read(); }
    bool want_write() const noexcept override { return stream_.file->can_write() && !write_queue_.empty(); }

    bool read([[maybe_unused]] Poller& poller) override
    {
      try
      {
        bool can_read = true;
        while (can_read && stream_.file->is_open()) {
          can_read = std::visit(match {
            
            [](blocked&&)
            {
              return false;
            },

            [](eof&&)
            {
              return false;
            },

            [&](std::vector<char>&& buf) mutable
            {
              read_queue_.push_back(std::move(buf));
              return true;
            }

          },
          stream_.read(read_bufsiz));
        }
      }
      catch (...)
      {
        stream_.file->is_open(false);
        throw;
      }
      

      return stream_.file->is_open();
    }

    bool write() override
    {
      try
      {
        bool can_write = true;
        while (can_write && stream_.file->is_open() && !write_queue_.empty()) {

          auto& [orig_buf, offset] = write_queue_.front();
          std::size_t count = std::min(orig_buf.size() - offset, write_bufsiz);
          const auto& buf = std::span<char>(orig_buf).subspan(offset, count);

          can_write = std::visit(match {
            
            [](eof&&)
            {
              return false;
            },

            [](blocked&&)
            {
              return false;
            },

            [&](ssize_t&& bytes_written) mutable
            {
              // Update the offset reference by the number of bytes written.
              offset += bytes_written;
              // Are we there yet?
              if (offset == orig_buf.size()) {
                // The buffer has been completely used. Remove it from the
                // queue.
                write_queue_.pop_front();
              }
              return true;
            }
            
          },
          stream_.write(buf));
        }
      }
      catch (...)
      {
        stream_.file->is_open(false);
        throw;
      }
      

      return stream_.file->is_open();
    }

    void close() override
    {
      if (stream_.file->is_open())
      {
        stream_.file->close();
      }
    }

    bool has_reads() const noexcept { return !read_queue_.empty(); }

    std::optional<std::vector<char>> dequeue() noexcept override
    {
      if (read_queue_.empty())
        return std::nullopt;

      auto buf { std::move(read_queue_.front()) };
      read_queue_.pop_front();
      return buf;
    }

    void enqueue(const std::vector<char>& buf) noexcept override
    {
      write_queue_.push_back(std::make_pair(std::move(buf), 0));
    }
  };
}

#endif // JETBLACK_IO_FILE_POLL_HANDLER_HPP
