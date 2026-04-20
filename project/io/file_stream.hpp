#ifndef JETBLACK_IO_FILE_STREAM_HPP
#define JETBLACK_IO_FILE_STREAM_HPP

#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "io/file.hpp"
#include "io/file_types.hpp"

namespace jetblack::io
{
  class FileStream
  {
  public:
    typedef std::shared_ptr<File> file_pointer;

  public:
    FileStream(file_pointer file) noexcept
      : file(std::move(file))
    {
    }

    file_pointer file;

    std::variant<std::vector<char>, eof, blocked> read(std::size_t len)
    {
      std::vector<char> buf(len);
      int result = ::read(file->fd(), buf.data(), len);
      if (result == -1) {
        // Check if it's flow control.
        if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
          // Not a flow control error; the file has faulted.
          file->is_open(false);
          throw std::system_error(
            errno, std::generic_category(), "client file failed to read");
        }

        // The file is ok, but nothing has been read due to blocking.
        return blocked {};
      }

      if (result == 0) {
        // A read of zero bytes indicates file has closed.
        file->is_open(false);
        return eof {};
      }

      // Data has been read successfully. Resize the buffer and return.
      buf.resize(result);
      return buf;
    }

    std::variant<ssize_t, eof, blocked> write(const std::span<char>& buf)
    {
      int result = ::write(file->fd(), buf.data(), buf.size());
      if (result == -1)
      {
        // Check if it's flow control.
        if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
          // Not flow control; the file has faulted.
          file->is_open(false);
          throw std::system_error(
            errno, std::generic_category(), "client file failed to write");
        }

        // The file is ok, but nothing has been written due to blocking.
        return blocked {};
      }

      if (result == 0)
      {
        // A write of zero bytes indicates file has closed.
        file->is_open(false);
        return eof {};
      }

      // return the number of bytes that were written.
      return result;
    }
  };

}

#endif // JETBLACK_IO_FILE_STREAM_HPP
