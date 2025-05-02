#pragma  once

#include <cstdint>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include "eventloop.hpp"
#include <memory>
#include <fcntl.h>


struct Socket;



struct Endpoint {

	explicit Endpoint(std::uint16_t port, std::uint32_t ip) {
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = htonl(ip);
	}

	explicit Endpoint(std::uint16_t port, const std::string& ip) {
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_aton(ip.c_str(), &addr.sin_addr);
	}

	std::uint16_t port() const noexcept {
		return ntohs(addr.sin_port);
	}

	std::string ip() const {
		return inet_ntoa(addr.sin_addr);
	}


	friend class Socket;
	friend class Acceptor;

private:
	struct sockaddr_in addr;
};



//this class represents a tcp-socket
struct Socket {

	Socket(EventLoop& loop);

	Socket(const Socket&) = delete;
	Socket& operator=(const Socket&) = delete;

	void connect(const Endpoint& ep);

	const Endpoint* endpoint() const noexcept;

	friend class Acceptor;

	void async_read(char*, std::size_t bytes, EventLoop::ReadHandler);

	int read(char* buffer, std::size_t bytes);

	void async_write(char*, std::size_t bytes, EventLoop::WriteHandler);

	int write(char* buffer, std::size_t bytes);

	void close();

	int getFd() const noexcept;

	~Socket();

private:

	int fd;
	EventLoop& loop;
	std::unique_ptr<Endpoint> ep;
};


//this class represents a stream descriptor (in this program I use it to asynchronously read from the standard input)
struct StreamDescriptor {

	StreamDescriptor(EventLoop& loop, int fd) 
		: fd(fd) 
		,loop(loop)
	{
		if (set_non_blocking() == -1) {
			throw std::runtime_error("Failed to set the non-blocking mode for stream descriptor");
		}
	}

	StreamDescriptor(const StreamDescriptor&) = delete;
	StreamDescriptor& operator=(const StreamDescriptor&) = delete;

	void async_read(char* buffer, std::size_t bytes, EventLoop::ReadHandler handler) {
		
		loop.add_read(fd, [this, buffer, bytes, handler = std::move(handler)](int error) {

			if (error == EventLoop::EVENTLOOP_STOPPED) {
				handler(EventLoop::EVENTLOOP_STOPPED, 0);
				return;
			}
			if (error == EPOLLERR) {
				handler(1,0);
				return;
			}

			if (loop.write_tasks.find(fd) == loop.write_tasks.end()) {
				
				loop.remove(fd);	
			}
			else {
				loop.modify(fd, EPOLLOUT);
			}

			int n = read(fd, buffer, bytes);
			if (n <= 0) {

				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					async_read(buffer, bytes, std::move(handler));
					return;
				}

				handler(1, 0); // an error occured
			}
			else {

				handler(0, n); // it's alright
			}



		});
	}

	void async_write(char* buffer, std::size_t bytes, EventLoop::WriteHandler handler) {
		
		loop.add_write(fd, [this, buffer, bytes, handler = std::move(handler)](int error) {

			if (error == EventLoop::EVENTLOOP_STOPPED) {
				handler(EventLoop::EVENTLOOP_STOPPED, 0);
				return;
			}
			if (error == EPOLLERR) {
				handler(1,0);
				return;
			}

			if (loop.read_tasks.find(fd) == loop.read_tasks.end()) { // this logic should be put into the EventLoop
				loop.remove(fd);			
			}
			else {
				loop.modify(fd, EPOLLIN);
			}

			int n = read(fd, buffer, bytes);
			if (n == -1) {
				
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					async_write(buffer, bytes, std::move(handler));
					return;
				}

				handler(1,0); 			
			}
			else if (n != bytes) {
				async_write(buffer + n, bytes - n, std::move(handler)); // we call handler only when all the data is already written
			}
			else {
				handler(0,n);
			}

		});
	}

	~StreamDescriptor() {
		loop.cleanup(fd);
	}

	int set_non_blocking() {

		int flags = fcntl(fd, F_GETFL, 0);
		if (flags == -1) {
			return -1;
		}

		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
			return -1;
		}

		return 0;
	}

public:
	int fd;
	EventLoop& loop;
};


//tcp acceptor
class Acceptor {

public:

	Acceptor(EventLoop& loop, const Endpoint& ep);

	Acceptor(const Acceptor&) = delete;
	Acceptor& operator=(const Acceptor&) = delete;

	void async_accept(Socket& new_socket, EventLoop::AcceptHandler handler);

	void accept(Socket& new_socket);

	void close();

	~Acceptor();

	inline int getFd() const noexcept { return fd; }

private:

	int fd;
	EventLoop& loop;
	std::unique_ptr<Endpoint> ep;

	static constexpr int MAX_WAITING_CONNECTIONS = 256; // 
};





/////////////////////////////////////////////////////IMPLEMENTATION/////////////////////////////////////////////////////



//socket


inline Socket::Socket(EventLoop& loop) 
	: fd(-1)
	, loop(loop)
	, ep(nullptr)
{
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		throw std::runtime_error("Error on socket() : " + std::string(strerror(errno)));
	}

}

inline void Socket::connect(const Endpoint& ep) {
	
	if (::connect(fd, (struct sockaddr*)&ep.addr, sizeof(ep.addr)) == -1) {
		throw std::runtime_error("Failed to connect to the endpoint!"); 
	}

	this->ep = std::make_unique<Endpoint>(ep); 

}

inline const Endpoint* Socket::endpoint() const noexcept {
	return ep.get();
}


inline void Socket::async_read(char* buffer, std::size_t bytes, EventLoop::ReadHandler handler) {
	
	loop.add_read(fd, [this, buffer, bytes, handler = std::move(handler)](int error) {

		if (error == EventLoop::EVENTLOOP_STOPPED) {
			handler(EventLoop::EVENTLOOP_STOPPED, 0);
			return;
		}
		if (error == EPOLLERR) {
			handler(1,0);
			return;
		}

		if (loop.write_tasks.find(fd) == loop.write_tasks.end()) {
			loop.remove(fd);	
		}
		else {
			loop.modify(fd, EPOLLOUT);
		}


		int n = recv(fd, buffer, bytes, O_NONBLOCK);
		if (n == -1) {

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				async_read(buffer, bytes, std::move(handler));
				return;
			}

			handler(1, 0); // an error occured
		}
		else if (n == 0) {

			handler(2, 0); // means that client disconnected
		}
		else {

			handler(0, n); // it's alright
		}

	});

}

inline int Socket::read(char* buffer, std::size_t bytes) {
	int bytes_read = ::read(fd, buffer, bytes);
	return bytes_read;
}

inline void Socket::async_write(char* buffer, std::size_t bytes, EventLoop::WriteHandler handler) {

	loop.add_write(fd, [this, buffer, bytes, handler = std::move(handler)](int error) {

		if (error == EventLoop::EVENTLOOP_STOPPED) {
			handler(EventLoop::EVENTLOOP_STOPPED, 0);
			return;
		}
		else if (error == EPOLLERR) {
			handler(1,0);
			return;
		}
		else if (error == EPOLLHUP) {
			handler(2,0);
			return;
		}

		if (loop.read_tasks.find(fd) == loop.read_tasks.end()) { // to do : this logic should be put into the EventLoop
			loop.remove(fd);		
		}
		else {
			loop.modify(fd, EPOLLIN);
		}

		int n = send(fd, buffer, bytes, O_NONBLOCK | MSG_NOSIGNAL);

		if (n == -1) {
			
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				async_write(buffer, bytes, std::move(handler));
				return;
			}

			handler(1,0); 			
		}
		else if (n != bytes) {
			async_write(buffer + n, bytes - n, std::move(handler)); // we call handler only when all the data is already written
		}
		else {
			handler(0,n);
		}

	});
}

inline int Socket::write(char* buffer, std::size_t bytes) {
	int bytes_written = ::write(fd, buffer, bytes);
	return bytes_written;
}

inline int Socket::getFd() const noexcept { return fd; }

inline void Socket::close() {

	if (fd == -1) {
		return;
	}

	loop.cleanup(fd); ///
	::close(fd);
	fd = -1;
}

inline Socket::~Socket() {
	close();
}




//acceptor

inline Acceptor::Acceptor(EventLoop& loop, const Endpoint& ep) 
	: loop(loop)
	, ep(nullptr)
	, fd(-1)
{

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		throw std::runtime_error("Failed to create a socket!");	
	}

	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
		throw std::runtime_error("Failed to set SO_REUSEPORT");
	}

	if (bind(fd, (struct sockaddr*)&ep.addr, sizeof(ep.addr)) == -1) {
		throw std::runtime_error("Failed to bind a socket with a port!"); /////
	}

	if (listen(fd, MAX_WAITING_CONNECTIONS) == -1) {
		throw std::runtime_error("Failed to listen");
	}

}

inline void Acceptor::async_accept(Socket& new_socket, EventLoop::AcceptHandler handler) {

	loop.add_accept(fd, [&new_socket, this, handler = std::move(handler)](int error) {

		if (error == EventLoop::EVENTLOOP_STOPPED) {
			handler(EventLoop::EVENTLOOP_STOPPED);
			return;
		}
		if (error == EPOLLERR) {
			handler(1);
			return;
		}

		loop.remove(fd);	

		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		new_socket.fd = accept4(fd, (struct sockaddr*)&addr, &addrlen, O_NONBLOCK);

		if (new_socket.fd == -1) {

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				async_accept(new_socket, std::move(handler));
				return;
			}
			handler(1);
		}
		else {
			handler(0);
		}

	});
}

inline void Acceptor::accept(Socket& new_socket) {
	socklen_t addr_len;
	new_socket.fd = ::accept(fd, (struct sockaddr*)new_socket.ep.get(), &addr_len);
	if (new_socket.fd == -1) {
		throw std::runtime_error("Failed to accept a client " + std::string(strerror(errno)));
	}
}

inline void Acceptor::close() {
	if (fd == -1) {
		return;
	}
	loop.cleanup(fd);
	::close(fd);
	fd = -1;
}


inline Acceptor::~Acceptor() {
	close();
}
