#include <fcntl.h>
#include <netdb.h>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <map>
#include <vector>

typedef struct kevent kevent_t;

struct Client
{
    char buf[100];
    std::size_t len { 0 };
    std::size_t offset { 0 };
};

int main()
{
    const uint16_t port = 22000;

    // Create the queue.
    auto kfd = kqueue();

    // Create the listener socket.
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        std::cerr
            << "failed to create listener socket: "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

    // Make the listener socket non-blocking.
    if (fcntl(listen_fd, F_SETFL, O_NONBLOCK) == -1)
    {
        std::cerr
            << "failed to make listener socket non-blocking: "
            << std::strerror(errno)
            << std::endl;
        return 1;

    }

    // Make the address to listen on.
    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(port);

    // Bind the address of to socket.
    if (bind(listen_fd, (sockaddr *)&servaddr, sizeof(servaddr)) == -1)
    {
        std::cerr
            << "failed to bind listener socket to port " << port << ": "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

    // Start listening for connections.
    if (listen(listen_fd, 10) == -1)
    {
        std::cerr
            << "failed to listen to bound socket: "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

    kevent_t listen_ev;
    EV_SET(&listen_ev, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kfd, &listen_ev, 1, nullptr, 0, nullptr) == -1)
    {
        std::cerr
            << "failed to make listen events: "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &signals, nullptr) == -1)
    {
        std::cerr
            << "failed to block signal events: "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

    kevent_t signal_ev[2];
    EV_SET(&signal_ev[0], SIGINT, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    EV_SET(&signal_ev[1], SIGQUIT, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kfd, signal_ev, 2, nullptr, 0, nullptr) == -1)
    {
        std::cerr
            << "failed to make signal events: "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

    std::map<int, Client> clients;
    bool is_listening = true;
    while (is_listening)
    {
        // Make a vector for the sockets and add the listen socket, and all the client sockets.
        std::vector<kevent_t> events(1 + clients.size());

        // Wait for an event.
        struct timespec timeout { 5, 0 };
        int nevents = kevent(kfd, nullptr, 0, events.data(), events.size(), &timeout);

        if (nevents < 0)
        {
            // Exit on errors.
            std::cerr << "failed to poll: " << std::strerror(errno) << std::endl;
            is_listening = false;
            continue;
        }

        if (nevents == 0)
        {
            std::cerr << "Timeout" << std::endl;
            continue;
        }

        // Go through each of the sockets. Decrement the active fds for early termination.
        for (auto i = 0; i < nevents; ++i)
        {
            auto& event = events[i];
            int fd = event.ident;

            if (fd == listen_fd)
            {
                // Accept the new connection.
                int client_fd = accept(listen_fd, (sockaddr *)nullptr, nullptr);
                if (client_fd == -1)
                {
                    std::cerr
                        << "failed to accept client socket: "
                        << std::strerror(errno)
                        << std::endl;
                    is_listening = false;
                    break;
                }

                // Make the connection non-blocking.
                if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1)
                {
                    std::cerr
                        << "failed to make client socket non-blocking: "
                        << std::strerror(errno)
                        << std::endl;
                    is_listening = false;
                    break;
                }

                // Add events for reads or writes, but only enable reads.
                kevent_t client_ev[2];
                EV_SET(&client_ev[0], client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
                EV_SET(&client_ev[1], client_fd, EVFILT_WRITE, EV_ADD | EV_DISABLE, 0, 0, nullptr);
                if (kevent(kfd, client_ev, 2, nullptr, 0, nullptr) == -1)
                {
                    std::cerr
                        << "failed to make client events: "
                        << std::strerror(errno)
                        << std::endl;
                    is_listening = false;
                    break;
                }

                clients[client_fd] = Client {};
            }
            else if (event.filter == EVFILT_SIGNAL)
            {
                if (event.ident == SIGINT)
                {
                    std::cout << "SIGINT\n";
                }
                else if (event.ident == SIGQUIT)
                {
                    std::cout << "SIGQUIT\n";
                    is_listening = false;
                }
                continue;
            }
            else if (event.filter == EVFILT_READ || event.filter == EVFILT_WRITE)
            {
                auto& client = clients[fd];

                if (event.filter == EVFILT_READ)
                {
                    // Read from the client socket.
                    memset(client.buf, 0, sizeof(client.buf));
                    auto nbytes_read = read(fd, client.buf, sizeof(client.buf));
                    std::cout << "Received " << client.len << " bytes from " << fd << " of \"" << client.buf << "\"" << std::endl;

                    if (nbytes_read <= 0)
                    {
                        std::cout << "Removing client " << fd << std::endl;
                        clients.erase(fd);
                        close(fd);
                    }
                    else
                    {
                        client.len = nbytes_read;
                        client.offset = 0;

                        // Disable reading, enable writing.
                        kevent_t client_ev[2];
                        EV_SET(&client_ev[0], fd, EVFILT_READ, EV_DISABLE, 0, 0, nullptr);
                        EV_SET(&client_ev[1], fd, EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
                        if (kevent(kfd, client_ev, 2, nullptr, 0, nullptr) == -1)
                        {
                            std::cerr
                                << "failed to switch client events to write: "
                                << std::strerror(errno)
                                << std::endl;
                            is_listening = false;
                            break;
                        }
                    }
                }
                else if (event.filter == EVFILT_WRITE)
                {
                    if (client.len == 0)
                    {
                        // Nothing to write.
                        std::cerr << "Nothing to write\n";
                        continue;
                    }

                    // Write the data back.
                    std::cout << "Echoing back - " << (client.buf + client.offset) << std::endl;
                    std::cout << "Writing " << client.len << std::endl;
                    ssize_t nbytes_written = write(fd, client.buf + client.offset, client.len - client.offset);
                    std::cout << "Wrote " << nbytes_written << " bytes" << std::endl;

                    if (nbytes_written <= 0)
                    {
                        // Either error or close.
                        std::cout << "Removing client " << fd << std::endl;
                        clients.erase(fd);
                    }
                    else
                    {
                        client.offset += nbytes_written;
                        if (client.offset == client.len)
                        {
                            client.len = 0;
                            client.offset = 0;

                            // Enable reading, disable writing.
                            kevent_t client_ev[2];
                            EV_SET(&client_ev[0], fd, EVFILT_READ, EV_ENABLE, 0, 0, nullptr);
                            EV_SET(&client_ev[1], fd, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
                            if (kevent(kfd, client_ev, 2, nullptr, 0, nullptr) == -1)
                            {
                                std::cerr
                                    << "failed to switch client events to write: "
                                    << std::strerror(errno)
                                    << std::endl;
                                is_listening = false;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}
