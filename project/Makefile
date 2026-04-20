CXX = clang++
# CXX = g++
CXXFLAGS = -g -std=c++23 -Wall -I. -I../external -I/opt/homebrew/include
LDLIBS = -L/opt/homebrew/lib -lspdlog -lfmt -lssl -lcrypto

COMMON_HPP = \
	io/file.hpp \
	io/tcp_socket.hpp \
	io/bio.hpp \
	io/tcp_stream.hpp \
	io/ssl_ctx.hpp \
	io/ssl.hpp \
	logging/log.hpp \
	utils/match.hpp \
	utils/utils.hpp

SERVER_HPP = \
	io/tcp_listener_socket.hpp \
	io/tcp_server_socket.hpp \
	io/poll_handler.hpp \
	io/poller.hpp \
	io/tcp_socket_poll_handler.hpp \
	io/tcp_listener_poll_handler.hpp
CLIENT_HPP = \
	io/tcp_client_socket.hpp \
	io/poller.hpp \
	io/poll_handler.hpp \
	io/tcp_socket_poll_handler.hpp \
	io/file_poll_handler.hpp

.PHONEY: default
default: all

.PHONEY: all
all: chat-server echo-server client

chat-server: chat-server.o
	$(LINK.cc) $^ $(LOADLIBES) $(LDLIBS) -o $@

chat-server.o: chat-server.cpp $(SERVER_HPP) $(COMMON_HPP)

echo-server: echo-server.o
	$(LINK.cc) $^ $(LOADLIBES) $(LDLIBS) -o $@

echo-server.o: echo-server.cpp $(SERVER_HPP)

client: client.o
	$(LINK.cc) $^ $(LOADLIBES) $(LDLIBS) -o $@

client.o: client.cpp $(CLIENT_HPP) $(COMMON_HPP)

.PHONY: clean
clean:
	rm -f chat-server.o chat-server
	rm -f echo-server.o echo-server
	rm -f client.o client
	
