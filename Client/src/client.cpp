#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "http_processor.hpp"
#include "eventloop.hpp"
#include "socket.hpp"
#include <nlohmann/json.hpp>
#include "accumulator.hpp"
#include "client_config.hpp"


struct ServerConnection : public std::enable_shared_from_this<ServerConnection> {

	static constexpr int BUFFER_SIZE = 1024;

	ServerConnection(EventLoop& loop, const Endpoint& ep) 
		: sock(loop)
		, input(loop, STDIN_FILENO)
		, ep(ep)
	{}

	void start()  {
		
		connect();
		std::cout << "Now you can input the data:" << std::endl;
		do_read_input();

	}

	void stop() {

	}

private:

	void connect() {
		sock.connect(ep);
		std::cout << "Successfully connected to the server!" << std::endl;
	}

	void do_read_input() {
		
		input.async_read(buffer, BUFFER_SIZE, [this](int error, std::size_t bytes){

			if (error) {
				throw std::runtime_error("Failed to read the input");
			}

			if (buffer[0] == '\n') {

				if (!data.empty()) {
					send_data();
					data.clear();
				}
				else {
					do_read_input();
				}

				return;
			}

			
			data.emplace_back(buffer, bytes - 1); // extracting '\n'
			do_read_input(); // continue recieving data from the client

		});
	}


	void send_data() {

		json json_data = data;

		std::string request = HttpProcessor::buildPostRequest(json_data.dump(), ep.ip(), ep.port());
		auto request_buffer = std::make_shared<std::string>(std::move(request));
		
		sock.async_write(request_buffer->data(), request_buffer->length(), [this, request_buffer](int error, std::size_t){

			if (error) {
				sock.close();
				throw std::runtime_error("Failed to send data to the server");
			}

			std::cout << "The data was sent successfully, now waiting for the response from the server..." << std::endl;
			do_read_response();

		});

	}

	void do_read_response() {

		sock.async_read(buffer, BUFFER_SIZE, [this](int error, std::size_t bytes){


			if (error || bytes == 0) {
				sock.close();
                throw std::runtime_error("Failed to read a response from the server");
            }

            try {
                response_accumulator.append(buffer, bytes); 
            } catch (const std::exception& e) {
                std::cerr << "Response error: " << e.what() << std::endl;
                response_accumulator.reset();
                
                std::cout << std::endl << "Now you can input data again:" << std::endl;
                do_read_input();
                return;
            }

            if (response_accumulator.isComplete()) {
                
                auto& result = response_accumulator.getParseResult();
                if (result.success && result.statusCode == 200) {
                	std::cout << "Response: " << result.data["message"].get<std::string>() << std::endl;
                } else {
                    std::cerr << "Server error: " << result.error << " (status: " << result.statusCode << ")" << std::endl
                    	<< result.data["error"].get<std::string>() << std::endl; 
                }
                response_accumulator.reset();

                std::cout << std::endl << "Now you can input data again:" << std::endl;
                do_read_input(); // start recieving data from the console again
            }
            else {
            	do_read_response(); // response is not complete yet
            }


		});


	}

private:

	Socket sock;
	StreamDescriptor input;
	Endpoint ep;
	char buffer[BUFFER_SIZE];
	std::vector<std::string> data;
	ResponseAccumulator response_accumulator;
};






int main(int argc, char* argv[]) {

	try {

		ClientConfig config(argc, argv);
		Endpoint ep(config.getPort(), config.getIp());

		EventLoop loop;
		ServerConnection conn(loop, ep);
		conn.start();

		loop.run();
		return 0;
	}
	catch(std::exception& e) {

		std::cout << e.what() << std::endl;
		return 1;
	}

}
