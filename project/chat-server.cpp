#include <format>
#include <set>

#include "io/poller.hpp"
#include "io/tcp_listener_poll_handler.hpp"
#include "io/ssl_ctx.hpp"
#include "logging/log.hpp"
#include "utils/utils.hpp"

#include <popl.hpp>

using namespace jetblack::io;
namespace logging = jetblack::logging;

std::shared_ptr<SslContext> make_ssl_context(const std::string& certfile, const std::string& keyfile)
{
  auto ctx = std::make_shared<SslServerContext>();
  ctx->min_proto_version(TLS1_2_VERSION);
  ctx->use_certificate_file(certfile);
  ctx->use_private_key_file(keyfile);
  return ctx;
}

int main(int argc, char** argv)
{
  bool use_tls = false;
  uint16_t port = 22000;
  popl::OptionParser op("options");
  op.add<popl::Switch>("s", "ssl", "Connect with TLS", &use_tls);
  auto help_option = op.add<popl::Switch>("", "help", "produce help message");
  op.add<popl::Value<decltype(port)>>("p", "port", "port number", port, &port);
  auto certfile_option = op.add<popl::Value<std::string>>("c", "certfile", "path to certificate file");
  auto keyfile_option = op.add<popl::Value<std::string>>("k", "keyfile", "path to key file");

  try
  {
    op.parse(argc, argv);

    if (help_option->is_set())
    {
      if (help_option->count() == 1)
    		std::cout << op << "\n";
	    else if (help_option->count() == 2)
		    std::cout << op.help(popl::Attribute::advanced) << "\n";
	    else
		    std::cout << op.help(popl::Attribute::expert) << "\n";
      exit(1);
    }

    logging::info(
      std::format(
        "starting chat server on port {}{}.",
        static_cast<int>(port),
        (use_tls ? " with TLS" : "")));

    std::optional<std::shared_ptr<SslContext>> ssl_ctx;

    if (use_tls)
    {
      if (!certfile_option->is_set())
      {
        std::cout << "For ssl must use certfile" << std::endl;
    		std::cout << op << "\n";
        exit(1);
      }
      if (!keyfile_option->is_set())
      {
        std::cout << "For ssl must use keyfile" << std::endl;
    		std::cout << op << "\n";
        exit(1);
      }
      ssl_ctx = make_ssl_context(certfile_option->value(), keyfile_option->value());
    }

    std::set<int> clients;

    auto poller = Poller(

      [&clients](Poller&, int fd)
      {
        logging::info(std::format("on_open: {}", fd));
        clients.insert(fd);
      },

      [&clients](Poller&, int fd)
      {
        logging::info(std::format("on_close: {}", fd));
        clients.erase(fd);
      },

      [&clients](Poller& poller, int fd, std::vector<std::vector<char>>&& bufs)
      {
        logging::info(std::format("on_read: {}", fd));

        for (auto& buf : bufs)
        {
          logging::info(std::format("on_read: received {}", to_string(buf)));
          for (auto client_fd : clients)
          {
            if (client_fd != fd)
            {
              logging::info(std::format("on_read: sending to {}", client_fd));
              poller.write(client_fd, buf);
            }
          }
        }
      },
      
      [](Poller&, int fd, std::exception error)
      {
        logging::info(std::format("on_error: {}, {}", fd, error.what()));
      }
    );
    poller.add_handler(std::make_unique<TcpListenerPollHandler>(port, ssl_ctx));
    poller.event_loop();
  }
  catch(const std::exception& error)
  {
    logging::error(std::format("Server failed: {}", error.what()));
  }

  return 0;
}
