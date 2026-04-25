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
#include <iostream>
#include <map>
#include <system_error>
#include <vector>

struct Buffer
{
    char data[100];
    std::size_t len { 0 };
    std::size_t offset { 0 };
};

struct Client
{
    std::deque<Buffer> buffers;
    bool can_write { false };
};

enum class ListenState
{
    OK,
    WOULD_BLOCK,
    ERROR
};

std::expected<int, std::error_code> _accept(int listen_fd, sockaddr* addr, socklen_t* addr_len)
{
    int client_fd = accept(listen_fd, addr, addr_len);
    if (client_fd == -1)
    {
        return std::unexpected(std::make_error_code(std::errc(errno)));
    }
    return client_fd;
}

ListenState accept_client(int efd, int listen_fd, std::map<int, Client>& clients)
{
    // Accept the new connection.
    auto client_fd = _accept(listen_fd, (sockaddr *)nullptr, nullptr);
    if (!client_fd)
    {
        if (client_fd.error() == std::errc::operation_would_block)
        {
            std::cerr << "Accept would block\n";
            return ListenState::WOULD_BLOCK;
        }

        std::cerr
            << "failed to accept client socket: "
            << std::strerror(errno)
            << std::endl;
        
            return ListenState::ERROR;
    }

    // Make the connection non-blocking.
    if (fcntl(*client_fd, F_SETFL, O_NONBLOCK) == -1)
    {
        std::cerr
            << "failed to make client socket non-blocking: "
            << std::strerror(errno)
            << std::endl;
        return ListenState::ERROR;
    }

    // Add events for reads or writes with edge trigger.
    epoll_event client_ev;
    client_ev.events = EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLERR | EPOLLHUP | EPOLLET;
    client_ev.data.fd = *client_fd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, *client_fd, &client_ev) == -1)
    {
        std::cerr
            << "failed to make client events: "
            << std::strerror(errno)
            << std::endl;
        return ListenState::ERROR;
    }

    // Create the client.
    clients[*client_fd] = Client {};

    // Keep listening.
    return ListenState::OK;
}

bool accept_clients(int efd, int listen_fd, std::map<int, Client>& clients)
{
    bool is_ok = true;
    bool keep_trying = true;
    
    while (keep_trying)
    {
        switch (accept_client(efd, listen_fd, clients))
        {
        case ListenState::OK:
            break;

        case ListenState::WOULD_BLOCK:
            keep_trying = false;
            break;

        case ListenState::ERROR:
            is_ok = false;
            keep_trying = false;
            break;
        }
    }

    return is_ok;
}

enum class SignalState
{
    OK,
    WOULD_BLOCK,
    QUIT,
    ERROR
};

SignalState read_signal(int signal_fd)
{
    signalfd_siginfo fdsi;
    auto nbytes_read = read(signal_fd, &fdsi, sizeof(fdsi));
    if (nbytes_read == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return SignalState::WOULD_BLOCK;
        }
        else
        {
            std::cerr << "Error reading signal\n";
            return SignalState::ERROR;
        }
    }

    if (nbytes_read != sizeof(fdsi))
    {
        std::cerr << "Failed to read entire signal\n";
        return SignalState::ERROR;
    }

    if (fdsi.ssi_signo == SIGINT)
    {
        std::cout << "SIGINT\n";
    } else if (fdsi.ssi_signo == SIGQUIT) {
        std::cout << "SIGQUIT\n";
        return SignalState::QUIT;
    } else
    {
        std::cout << "Unexpected signal\n";
        return SignalState::ERROR;
    }

    return SignalState::OK;
}

bool read_signals(int efd, int signal_fd)
{
    bool is_ok = true;
    bool keep_trying = true;

    while (keep_trying)
    {
        switch (read_signal(signal_fd))
        {
        case SignalState::OK:
            break;

        case SignalState::WOULD_BLOCK:
            keep_trying = false;
            break;

        case SignalState::QUIT:
            close(signal_fd);
            epoll_ctl(efd, EPOLL_CTL_DEL, signal_fd, nullptr);
            is_ok = false;
            keep_trying = false;
            break;

        case SignalState::ERROR:
            is_ok = false;
            keep_trying = false;
            break;
        }
    }

    return is_ok;
}

enum class ReadState
{
    OK,
    WOULD_BLOCK,
    END_OF_FILE,
    ERROR
};

ReadState client_read(int client_fd, Client& client)
{
    // Read from the client socket.
    Buffer buffer;
    memset(buffer.data, 0, sizeof(buffer.data));
    auto nbytes_read = read(client_fd, buffer.data, sizeof(buffer.data));
    std::cout << "Received " << buffer.len << " bytes from " << client_fd << " of \"" << buffer.data << "\"" << std::endl;

    if (nbytes_read == 0)
    {
        return ReadState::END_OF_FILE;
    }

    if (nbytes_read == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return ReadState::WOULD_BLOCK;
        }
        else
        {
            return ReadState::ERROR;
        }
    }

    buffer.len = nbytes_read;
    buffer.offset = 0;
    client.buffers.push_front(buffer);

    return ReadState::OK;
}

bool client_reads(int efd, int client_fd, Client& client)
{
    bool is_ok = true;
    bool keep_trying = true;

    while (keep_trying)
    {
        switch (client_read(client_fd, client))
        {
        case ReadState::OK:
            break;

        case ReadState::WOULD_BLOCK:
            keep_trying = false;
            break;

        case ReadState::END_OF_FILE:
        case ReadState::ERROR:
            epoll_ctl(efd, EPOLL_CTL_DEL, client_fd, nullptr);
            close(client_fd);
            is_ok = false;
            keep_trying = false;
            break;
        }
    }

    return is_ok;
}

enum class WriteState
{
    OK,
    NOTHING_TO_WRITE,
    WOULD_BLOCK,
    ERROR
};

WriteState client_write(int client_fd, Client& client)
{
    if (client.buffers.empty())
    {
        // Nothing to write.
        std::cerr << "Nothing to write\n";
        return WriteState::NOTHING_TO_WRITE;
    }

    // Write the data back.
    auto& buffer = client.buffers.back();
    std::cout << "Echoing back - " << (buffer.data + buffer.offset) << std::endl;
    std::cout << "Writing " << buffer.len << std::endl;
    ssize_t nbytes_written = write(client_fd, buffer.data + buffer.offset, buffer.len - buffer.offset);
    std::cout << "Wrote " << nbytes_written << " bytes" << std::endl;

    if (nbytes_written <= 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            return WriteState::WOULD_BLOCK;
        }
        else
        {
            return WriteState::ERROR;
        }
    }

    buffer.offset += nbytes_written;
    if (buffer.offset == buffer.len)
    {
        client.buffers.pop_back();
    }

    return WriteState::OK;
}

bool client_writes(int efd, int client_fd, Client& client)
{
    bool is_ok = true;
    bool keep_trying = true;

    while (keep_trying)
    {
        switch (client_write(client_fd, client))
        {
        case WriteState::OK:
            break;

        case WriteState::NOTHING_TO_WRITE:
            keep_trying = false;
            break;

        case WriteState::WOULD_BLOCK:
            keep_trying = false;
            break;

        case WriteState::ERROR:
            epoll_ctl(efd, EPOLL_CTL_DEL, client_fd, nullptr);
            close(client_fd);
            is_ok = false;
            keep_trying = false;
            break;
        }
    }

    return is_ok;
}

bool read_write_client(int efd, epoll_event& event, std::map<int, Client>& clients)
{
    auto& client = clients[event.data.fd];

    if ((event.events & EPOLLIN) == EPOLLIN)
    {
        auto is_open = client_reads(efd, event.data.fd, client);
        if (!is_open)
        {
            std::cout << "Removing client " << event.data.fd << std::endl;
            clients.erase(event.data.fd);
            // We can't write, so return.
            return false;
        }
    }

    if (((event.events & EPOLLOUT) == EPOLLOUT) | client.can_write)
    {
        auto is_open = client_writes(efd, event.data.fd, client);
        if (!is_open)
        {
            std::cout << "Removing client " << event.data.fd << std::endl;
            clients.erase(event.data.fd);
        }
    }

    return true;
}

bool handle_event(int efd, int listen_fd, int signal_fd, epoll_event& event, std::map<int, Client>& clients)
{
    if (event.data.fd == listen_fd)
    {
        // Return, as the listen fd can only accept.
        return accept_clients(efd, listen_fd, clients);
    }

    if (event.data.fd == signal_fd)
    {
        // Return, as the signal fd can only be read.
        return read_signals(efd, signal_fd);
    }

    if ((event.events & (EPOLLIN | EPOLLOUT)) != 0)
    {
        read_write_client(efd, event, clients);
    }

    return true;
}

int main()
{
    const uint16_t port = 22000;

    // Create the queue.
    auto efd = epoll_create1(0);
    if (efd == -1)
    {
        std::cerr
            << "failed to create epoll: "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

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

    epoll_event listen_ev;
    listen_ev.events = EPOLLIN;
    listen_ev.data.fd = listen_fd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, listen_fd, &listen_ev) == -1)
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

    auto signal_fd = signalfd(-1, &signals, SFD_NONBLOCK);
    if (signal_fd == -1)
    {
        std::cerr
            << "failed to create listener socket: "
            << std::strerror(errno)
            << std::endl;
        return 1;
    }

    epoll_event signal_ev;
    signal_ev.events = EPOLLIN;
    signal_ev.data.fd = signal_fd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, signal_fd, &signal_ev) == -1)
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
        std::vector<epoll_event> events(2 + clients.size());

        // Wait for events.
        auto nfds = epoll_wait(efd, events.data(), events.size(), 5 * 1000);

        if (nfds < 0)
        {
            // Exit on errors.
            std::cerr << "failed to poll: " << std::strerror(errno) << std::endl;
            is_listening = false;
            continue;
        }

        if (nfds == 0)
        {
            std::cerr << "Timeout" << std::endl;
            continue;
        }

        // Go through each of the sockets. Decrement the active fds for early termination.
        for (auto i = 0; i < nfds; ++i)
        {
            is_listening = handle_event(efd, listen_fd, signal_fd, events[i], clients);
        }
    }

    return 0;
}
