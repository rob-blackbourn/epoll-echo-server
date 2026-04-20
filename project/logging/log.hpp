#ifndef JETBLACK_LOGGING_LOG_HPP
#define JETBLACK_LOGGING_LOG_HPP

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <utility>

namespace jetblack::logging
{

  enum class Level
  {
    NONE     = 0,
    CRITICAL = 1,
    ERROR    = 2,
    WARNING  = 3,
    INFO     = 4,
    DEBUG    = 5,
    TRACE    = 6
  };

  namespace
  {
    static const std::string root_logger_name = "root";
    static const std::map<std::string, std::size_t> position_map = {
        {"time", 0},
        {"level", 1},
        {"name", 2},
        {"message", 3},
        {"function", 4},
        {"file", 5},
        {"line", 6},
    };
    // static const std::string default_format_string = "{time:%Y-%m-%d %X} {level:7} {message} {function} ({file}, {line})";
    static const std::string default_format_string = "{time:%Y-%m-%d %X} {level:8} {message}";

    inline std::string to_string(Level level)
    {
      switch (level)
      {
      case Level::NONE:
        return "NONE";
      case Level::CRITICAL:
        return "CRITICAL";
      case Level::ERROR:
        return "ERROR";
      case Level::WARNING:
        return "WARNING";
      case Level::INFO:
        return "INFO";
      case Level::DEBUG:
        return "DEBUG";
      case Level::TRACE:
        return "TRACE";
      }

      std::unreachable();
    }

    inline Level parse_level_or(std::string& name, Level default_level)
    {
      if (name == "NAME")
        return Level::NONE;
      if (name == "CRITICAL")
        return Level::CRITICAL;
      if (name == "ERROR")
        return Level::ERROR;
      if (name == "WARNING")
        return Level::WARNING;
      if (name == "INFO")
        return Level::INFO;
      if (name == "DEBUG")
        return Level::DEBUG;
      if (name == "TRACE")
        return Level::TRACE;
      return default_level;
    }

    inline Level env_level_or(const std::string& logger_name, Level default_level)
    {
      // Check the environment variable "LOGGER_LEVEL" and "LOGGER_LEVEL_<name>".
      auto base_env_name = std::string("LOGGER_LEVEL");
      auto logger_env_name = std::format("{}_{}", base_env_name, logger_name);

      auto env_level = std::getenv(logger_env_name.c_str());
      if (env_level == nullptr)
        env_level = std::getenv(base_env_name.c_str());

      if (env_level == nullptr)
        return default_level;
      auto level_string = std::string(env_level);
      return parse_level_or(level_string, default_level);
    }

    inline std::string env_format_string_or(
      const std::string& logger_name,
      const std::string& default_format_string)
    {
      // Check the environment variable "LOGGER_FORMAT" and "LOGGER_FORMAT_<name>".
      auto base_env_name = std::string("LOGGER_FORMAT");
      auto logger_env_name = std::format("{}_{}", base_env_name, logger_name);

      auto env_format_string = std::getenv(logger_env_name.c_str());
      if (env_format_string == nullptr)
        env_format_string = std::getenv(base_env_name.c_str());

      if (env_format_string == nullptr)
        return default_format_string;
      return std::string(env_format_string);
    }

    std::string make_format(
      const std::string& format_string,
      const std::map<std::string, std::size_t>& position_map)
    {
        auto position_format = std::string();

        auto i = std::size_t {0};
        while (i != std::string::npos && i < format_string.size())
        {
            auto start = format_string.find('{', i);
            if (start == std::string::npos)
            {
                position_format += format_string.substr(i);
                i = format_string.size();
                continue;
            }

            // Skip the '{'.
            if (++start >= format_string.size())
                throw std::logic_error("invalid format: no closing bracket");

            // Check for escaped "{{".
            if (format_string.at(start) == '{')
            {
                position_format += format_string.substr(i, 1 + start - i);
                i = start + 1;
                continue;
            }
            // check for "{}"
            if (format_string.at(start) == '}')
                throw std::logic_error("invalid format: unnamed parameter");

            auto end = format_string.find('}', start);
            if (end == std::string::npos)
                throw std::logic_error("invalid format: no closing bracket");

            auto colon = format_string.substr(start, 1 + end - start).find(':');
            auto name = (
                colon == std::string::npos
                ? format_string.substr(start, end - start)
                : format_string.substr(start, colon));
            
            auto i_position = position_map.find(name);
            if (i_position == position_map.end())
                throw new std::logic_error(std::format("invalid format: bad name \"{}\"", name));
            auto position = std::to_string(i_position->second);

            position_format += format_string.substr(i, start - i);
            position_format += position;
            position_format += (
                colon == std::string::npos
                ? format_string.substr(end, 1)
                : format_string.substr(start + colon, 1 + end - (start + colon)));

            i = end + 1;
        }

        position_format += "\n";

        return position_format;
    }

  }

  struct LogRecord
  {
    std::chrono::system_clock::time_point time;
    std::string name;
    Level level;
    std::source_location loc;
    std::string msg;
  };

  class LogHandler
  {
  public:
    virtual ~LogHandler() {}

    virtual void emit(const LogRecord& log_record, const std::string& format_string) = 0;
  };

  class StreamLogHandler : public LogHandler
  {
  private:
    FILE* stream_;
  public:
    StreamLogHandler(FILE* stream = stderr)
      : stream_(stream)
    {
    }

    void emit(const LogRecord& log_record, const std::string& format_string) override
    {
      auto log_level = to_string(log_record.level);
      auto function = std::string(log_record.loc.function_name());
      auto file = std::string(log_record.loc.file_name());
      auto line = log_record.loc.line();

      auto formatted = std::vformat(
        format_string,
        std::make_format_args(
          log_record.time,
          log_level,
          log_record.name,
          log_record.msg,
          function,
          file,
          line));


        fputs(formatted.c_str(), stream_);
    }
  };

  class Logger
  {
  private:
    std::string name_;
    Level level_;
    std::string format_string_;
    std::shared_ptr<LogHandler> log_handler_;
    std::mutex key_;

  public:
    Logger() {}
    Logger(
      const std::string& name,
      Level level,
      const std::string& format_string,
      std::shared_ptr<LogHandler> log_handler)
      : name_(name),
      level_(level),
      format_string_(make_format(format_string, position_map)),
      log_handler_(log_handler)
    {
    }
    Logger(const Logger& other)
      : name_(other.name_),
        level_(other.level_),
        format_string_(other.format_string_),
        log_handler_(other.log_handler_)
    {
    }
    Logger& operator = (const Logger& other)
    {
      this->name_ = other.name_;
      this->level_ = other.level_;
      this->format_string_ = other.format_string_;
      this->log_handler_ = other.log_handler_;
      return *this;
    }

    const std::string& name() const noexcept { return name_; }

    Level level() const noexcept { return level_; }
    void level(Level level) noexcept { level_ = level; }

    const std::string& format_string() const noexcept { return format_string_; }
    void format_string(const std::string& format_string) noexcept { format_string_ = make_format(format_string, position_map); }

    void log(Level level, const std::string& message, std::source_location loc)
    {
      if (static_cast<int>(level) <= static_cast<int>(level_))
      {
        std::scoped_lock lock(key_);

        auto time = std::chrono::system_clock::now();
        // auto time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());

        auto log_record = LogRecord
        {
          .time = time,
          .name = name_,
          .level = level,
          .loc = loc,
          .msg = message
        };
        log_handler_->emit(log_record, format_string_);
      }
    }

    void trace(std::string message, std::source_location loc = std::source_location::current())
    {
      log(Level::TRACE, message, loc);
    }
    
    void debug(std::string message, std::source_location loc = std::source_location::current())
    {
      log(Level::DEBUG, message, loc);
    }
    
    void info(std::string message, std::source_location loc = std::source_location::current())
    {
      log(Level::INFO, message, loc);
    }
    
    void warning(std::string message, std::source_location loc = std::source_location::current())
    {
      log(Level::WARNING, message, loc);
    }
    
    void error(std::string message, std::source_location loc = std::source_location::current())
    {
      log(Level::ERROR, message, loc);
    }
    
    void critical(std::string message, std::source_location loc = std::source_location::current())
    {
      log(Level::CRITICAL, message, loc);
    }
  };

  namespace
  {
    class LogManager
    {
    private:
      LogManager()
      {
      }

    public:
      static Logger& get(const std::string& name = root_logger_name)
      {
        static LogManager instance;
        return instance.logger(name);
      }

    private:
      std::map<std::string, Logger> loggers_;

      Logger& logger(const std::string& name)
      {
        auto i = loggers_.find(name);
        if (i == loggers_.end())
        {
          auto level = env_level_or(name, Level::INFO);
          auto format_string = env_format_string_or(name, default_format_string);
          auto logger = Logger(name, level, format_string, std::make_shared<StreamLogHandler>(stderr));
          loggers_[name] = logger;
        }
        return loggers_[name];
      }
    };
  }

  inline Logger& logger(const std::string& name = root_logger_name)
  {
    return LogManager::get(name);
  }

  inline Level level() noexcept
  {
    return LogManager::get().level();
  }

  inline void level(Level l) noexcept
  {
    LogManager::get().level(l);
  }
  
  inline void log(Level level, std::string message, std::source_location loc = std::source_location::current())
  {
    LogManager::get().log(level, message, loc);
  }
  
  inline void trace(std::string message, std::source_location loc = std::source_location::current())
  {
    LogManager::get().trace(message, loc);
  }
  
  inline void debug(std::string message, std::source_location loc = std::source_location::current())
  {
    LogManager::get().debug(message, loc);
  }
  
  inline void info(std::string message, std::source_location loc = std::source_location::current())
  {
    LogManager::get().info(message, loc);
  }
  
  inline void warning(std::string message, std::source_location loc = std::source_location::current())
  {
    LogManager::get().warning(message, loc);
  }
  
  inline void error(std::string message, std::source_location loc = std::source_location::current())
  {
    LogManager::get().error(message, loc);
  }
  
  inline void critical(std::string message, std::source_location loc = std::source_location::current())
  {
    LogManager::get().critical(message, loc);
  }
}

#endif // JETBLACK_LOGGING_LOG_HPP
