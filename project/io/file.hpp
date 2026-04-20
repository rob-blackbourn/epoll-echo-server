#ifndef JETBLACK_IO_FILE_HPP
#define JETBLACK_IO_FILE_HPP

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

namespace jetblack::io
{
  
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

}

#endif // JETBLACK_IO_FILE_HPP
