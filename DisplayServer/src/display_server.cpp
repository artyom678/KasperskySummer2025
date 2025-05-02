#include "accumulator.hpp"
#include "display_config.hpp"
#include "eventloop.hpp"
#include "socket.hpp"
#include "http_processor.hpp"
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>



struct Connection : std::enable_shared_from_this<Connection> {

	static constexpr int BUFFER_SIZE = 4096;

	inline static std::shared_ptr<Connection> create_connection(EventLoop& loop) {

		std::shared_ptr<Connection> new_connection(new Connection(loop));
		return new_connection;
	}

	void start() {
		do_read_request();
	}

	Socket& socket() {
		return sock;
	}

private:

	Connection(EventLoop& loop) 
		: sock(loop)
	{}

	Connection(const Connection&) = delete;
	Connection& operator=(const Connection&) = delete;

	void do_read_request() {
        
        auto self = shared_from_this();
        sock.async_read(buffer, BUFFER_SIZE, [this, self](int error, std::size_t bytes) {
            
            if (error || bytes == 0) {
                sock.close();
                return;
            }

            try {
                accumulator.append(buffer, bytes);
            } catch (const std::exception& e) {
                
                accumulator.reset();
                do_read_request();
                return;
            }

            if (accumulator.isComplete()) {

                auto& result = accumulator.getParseResult();
                if (result.success && result.data.contains("data") && result.data["data"].is_string()) {
                    
                    auto processed_data = result.data["data"].get<std::string>();
                    std::cout << "Data received: " << processed_data << std::endl;

                } else {
                	std::cout << "error: " << (result.error.empty() ? "Missing or invalid data field" : result.error) << std::endl;
                }
                accumulator.reset();
            }
            
            do_read_request(); 
        });
    }

private:
	Socket sock;
	char buffer[BUFFER_SIZE];
	RequestAccumulator accumulator;
};


struct Server {

	Server(EventLoop& loop, const Endpoint &ep) 
		: loop(loop)
		, acceptor(loop, ep)
	{}

	void start() {

		std::cout << "Display server started!" << std::endl;
		do_accept();
	}

private:

	void do_accept() {

		auto new_connection = Connection::create_connection(loop);
		acceptor.async_accept(new_connection->socket(), [this, new_connection](int error){

			if (!error) {
				new_connection->start();
			}
			else {
				std::cerr << "Failed to accept a new connection" << std::endl;
			}

			do_accept();

		});

	}

private:

	EventLoop& loop;
	Acceptor acceptor;
};


int main(int argc, char* argv[]) {

	try {

		DisplayConfig config(argc, argv);
		Endpoint ep(config.getPort(), 0); 

		EventLoop loop;
		
		Server server(loop, ep);
		server.start();

		loop.run();
		return 0;
	}
	catch(std::exception& e) {
		std::cout << "Error: " << e.what() << std::endl;
		return 1;
	}

}