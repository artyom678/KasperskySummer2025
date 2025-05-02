#pragma  once

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <functional>
#include <unistd.h>
#include <unordered_map>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>



class EventLoop {

	static constexpr int MAX_EVENTS = 1024;

	using ReadHandler = std::function<void(int, std::size_t)>;
	using WriteHandler = std::function<void(int, std::size_t)>;
	using AcceptHandler = std::function<void(int)>;
	using Task = std::function<void(int)>;


public:
	
	EventLoop();
	~EventLoop();

	void run();
	void stop();

	static constexpr int EVENTLOOP_STOPPED = 123;

private:

	void add_read(int fd, Task task);
	void add_write(int fd, Task task);
	void add_accept(int fd, Task task);

	void add(int fd, std::uint32_t events);

	void remove(int fd);

	void modify(int fd, std::uint32_t events);

	void cleanup(int fd);

	friend struct Socket;
	friend struct Acceptor;
	friend struct StreamDescriptor;

private:

	int epoll_fd;
	std::unordered_map<int, Task> read_tasks;
	std::unordered_map<int, Task> write_tasks;
	std::unordered_map<int, Task> accept_tasks;
};



inline EventLoop::EventLoop() 
{

	epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		throw std::runtime_error("Failed to create epoll instance : " + std::string(strerror(errno)));
	}
}

inline EventLoop::~EventLoop() {
	stop();
}

//this method runs the event loop
inline void EventLoop::run() {

	struct epoll_event ev, events[MAX_EVENTS];

	while(!read_tasks.empty() || !write_tasks.empty() || !accept_tasks.empty()) {

		int count_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (count_events == -1) {

			if (errno == EINTR) {
				continue;
			}

			throw std::runtime_error("Error on epoll_wait : " + std::string(strerror(errno)));
		}

		for(int i = 0; i < count_events && epoll_fd != -1; ++i) { // epoll_fd == -1 means that EventLoop has been stopped
			
			int fd = events[i].data.fd;
			
			if ((events[i].events & EPOLLERR) && epoll_fd != -1) {

				remove(fd); 

				if (read_tasks.find(fd) != read_tasks.end()) {
					auto read_task = std::move(read_tasks[fd]);
					read_tasks.erase(fd);
					read_task(EPOLLERR);
				}
				if (write_tasks.find(fd) != write_tasks.end()) {
					auto write_task = std::move(write_tasks[fd]);
					write_tasks.erase(fd);
					write_task(EPOLLERR);
				}
				if (accept_tasks.find(fd) != accept_tasks.end()) {
					auto accept_task = std::move(accept_tasks[fd]);
					accept_tasks.erase(fd);
					accept_task(EPOLLERR);
				}

				continue;
			}
			if ((events[i].events & EPOLLIN) && epoll_fd != -1) {
				
				if (accept_tasks.find(fd) != accept_tasks.end()) {
					
					auto accept_task = std::move(accept_tasks[fd]);
					accept_tasks.erase(fd);
					accept_task(0); // 0 means OK

				}
				else {

					auto read_task = std::move(read_tasks[fd]);
					read_tasks.erase(fd);
					read_task(0);
				}
			}
			if ((events[i].events & EPOLLOUT) && epoll_fd != -1) {

				auto write_task = std::move(write_tasks[fd]);
				write_tasks.erase(fd);
				if (events[i].events & EPOLLHUP) {
					write_task(EPOLLHUP);	
				}
				else {
					write_task(0);					
				}
				
			}
		}

	}

}

//using this method we can stop the EventLoop.
//All the waiting handlers are gonna
//be called with EventLoop::EVENTLOOP_STOPPED
//error code as soon as you call this method (not waiting
// for their events to happen).
//All new async_read, async_write
//and async_accept operations will be called
//immidiately with error code EventLoop::EVENTLOOP_STOPPED
inline void EventLoop::stop() {

	if (epoll_fd == -1) {
		return;
	}

	close(epoll_fd);
	epoll_fd = -1;

	while(!write_tasks.empty()) {
		Task t = std::move(write_tasks.begin()->second);
		write_tasks.erase(write_tasks.begin());
		t(EVENTLOOP_STOPPED);
	}
	while(!read_tasks.empty()) {
		Task t = std::move(read_tasks.begin()->second);
		read_tasks.erase(read_tasks.begin());
		t(EVENTLOOP_STOPPED);
	}
	while(!accept_tasks.empty()) {
		Task t = std::move(accept_tasks.begin()->second);
		accept_tasks.erase(accept_tasks.begin());
		t(EVENTLOOP_STOPPED);
	}

}


inline void EventLoop::add_read(int fd, Task task) {

	if (epoll_fd == -1) {
		task(EVENTLOOP_STOPPED);
	}

	if (write_tasks.find(fd) != write_tasks.end()) {
		modify(fd, EPOLLIN | EPOLLOUT);
	}
	else if (read_tasks.find(fd) == read_tasks.end()) {
		add(fd, EPOLLIN);
	}

	read_tasks[fd] = std::move(task);
}


inline void EventLoop::add_accept(int fd, Task task) {

	if (epoll_fd == -1) {
		task(EVENTLOOP_STOPPED);
		return;
	}

	if (accept_tasks.find(fd) == accept_tasks.end()) {
		add(fd, EPOLLIN);
	}

	accept_tasks[fd] = std::move(task);

}


inline void EventLoop::add_write(int fd, Task task) {

	if (epoll_fd == -1) {
		task(EVENTLOOP_STOPPED);
		return;
	}

	if (read_tasks.find(fd) != read_tasks.end()) {
		modify(fd, EPOLLIN | EPOLLOUT);
	}
	else if (write_tasks.find(fd) == write_tasks.end()) {
		add(fd, EPOLLOUT);
	}

	write_tasks[fd] = std::move(task);

}


inline void EventLoop::cleanup(int fd) {

	if (epoll_fd == -1) {
		return;
	}

	try {
		remove(fd);
	}
	catch(...) {

		if (errno != ENOENT) {
			throw;
		}
	}

    read_tasks.erase(fd);
    write_tasks.erase(fd);
    accept_tasks.erase(fd);
        
}


inline void EventLoop::add(int fd, std::uint32_t events) {

	struct epoll_event ev;
	ev.data.fd = fd;
	ev.events = events;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		throw std::runtime_error("Failed on EPOLL_CTL_ADD " + std::string(strerror(errno)));
	}
}

inline void EventLoop::remove(int fd) {

	if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {

		throw std::runtime_error("Failed on EPOLL_CTL_DEL " + std::string(strerror(errno)));
	}
}

inline void EventLoop::modify(int fd, std::uint32_t events) {

	struct epoll_event ev;
	ev.data.fd = fd;
	ev.events = events;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
		throw std::runtime_error("Failed on EPOLL_CTL_MOD " + std::string(strerror(errno)));
	}
}

