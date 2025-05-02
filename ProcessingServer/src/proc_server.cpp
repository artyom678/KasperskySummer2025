#include "eventloop.hpp"
#include "socket.hpp"
#include "http_processor.hpp"
#include "proc_config.hpp"
#include <exception>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <thread>
#include <iostream>
#include <queue>
#include <algorithm>
#include <unordered_set>
#include "accumulator.hpp"


//this class is for request validation (validates the strings from client)
struct StringValidator {

    inline static bool validate(const std::vector<std::string>& data, std::string& errorMessage) {
        
        bool allValid = std::all_of(data.begin(), data.end(), [](const std::string& str) {
            
            return !str.empty() && std::all_of(str.begin(), str.end(), [](char c) {
                
                return std::isalnum(c) && (std::isalpha(c) || std::isdigit(c));
            });
        });

        errorMessage = allValid ? "" : "Strings must contain only English letters and digits";
        
        return allValid;
    }
};


//concatenates the strings, deleting duplicates
struct StringProcessor {

    inline static std::string process(const std::vector<std::string>& data) {

    	std::unordered_set<std::string> already_met;
    	std::string result;

    	for(const std::string& s : data) {
    		
    		if (already_met.find(s) == already_met.end()) {
    			result += s;
    			result.push_back(' ');
    			already_met.insert(s);
    		}
    	}

    	if (!result.empty()) {
    		result.pop_back(); // getting rid of ' ' on the end
    	}
    	return result;
    }
};



//this class is responsible for sending processed requests to the display server
struct StringSender {

	StringSender(Socket& display_socket) 
		: sock(display_socket)
		, is_writing(false)
	{}

	void send(const std::string& data) {
        
        auto json_data = json{{"data", data}}.dump(); // putting the processed data into json and getting back a json-string
        auto request = HttpProcessor::buildPostRequest(json_data, sock.endpoint()->ip(), sock.endpoint()->port()); //building post-request
        
        write_queue.push(std::move(request)); // pushing request to the waiting queue (requests are sent one by one)

        try_write();
    }

private:

    void try_write() {

        if (is_writing || write_queue.empty()) {
            return;
        }

        is_writing = true;

        auto buffer = std::make_shared<std::string>(std::move(write_queue.front()));
        write_queue.pop();

        sock.async_write(buffer->data(), buffer->length(), [this, buffer](int error, std::size_t bytesWritten) {
            
            if(error) {
            	
            	sock.close();
            	throw std::runtime_error("Display server is unavailible"); //Fatal error, no sense to continue
            }

            is_writing = false;
            
            try_write();
        });
    }


private:

	Socket& sock;
	std::queue<std::string> write_queue;
	bool is_writing;

};





struct ClientConnection : public std::enable_shared_from_this<ClientConnection> {

	inline static std::shared_ptr<ClientConnection> create_connection(EventLoop& loop, StringSender& string_sender) {
		
		std::shared_ptr<ClientConnection> new_connection(new ClientConnection(loop, string_sender));
		return new_connection;
	}

	void start() {
		do_read_request();
	}

	Socket& socket() {
		return sock;
	}


private:

	ClientConnection(EventLoop& loop, StringSender& str_sender) 
		: loop(loop)
		, sock(loop)
		, string_sender(str_sender)
	{}

	void do_read_request() {


		sock.async_read(buffer, BUFFER_SIZE, [this, self = shared_from_this()](int error, std::size_t bytes){

			if (error) {
				sock.close();
				return;
			}

			try {
				accumulator.append(buffer, bytes); // request may be too big, so we read it by chunks (and check for the ending)
			}
			catch(std::exception& e) { // throws if the request is too large (over 1 MB)
				sendErrorResponse(400, {{"error", e.what()}});
				accumulator.reset();
				return;
			}

			if (accumulator.isComplete()) { //we've finally read a complete request

				auto& result = accumulator.getParseResult(); 
				std::string response; // response for the client
				std::string processedData; // the result of request processing

				//check the result
				if (!result.success) {
                    response = HttpProcessor::buildResponse(400, json{{"error", result.error}}.dump());
                } else if (!result.data.is_array()) {
                    response = HttpProcessor::buildResponse(400, json{{"error", "Request body must be an array"}}.dump());
                } else {
                    std::vector<std::string> data = result.data.get<std::vector<std::string>>();
                    std::string errorMessage;
                    if (!StringValidator::validate(data, errorMessage)) {
                        response = HttpProcessor::buildResponse(400, json{{"error", errorMessage}}.dump());
                    } else {
                        processedData = StringProcessor::process(data);
                        response = HttpProcessor::buildResponse(200, json{{"message", "Data was received successfully!"}}.dump());
                    }
                }


                do_write_response(std::move(response));

                if (!processedData.empty()) {
                    string_sender.send(processedData);
                }

                accumulator.reset();

            } else {
            	do_read_request(); // the request from the client is not complete yet, waiting for the rest of it
            }

		});

	}


	void sendErrorResponse(int statusCode, const json& data) {


		auto response = HttpProcessor::buildResponse(statusCode, data.dump());
		do_write_response(std::move(response));

	}


	void do_write_response(std::string response) {

		auto respBuffer = std::make_shared<std::string>(std::move(response));
		auto self = shared_from_this();

		sock.async_write(respBuffer->data(), respBuffer->length(), [this, self, respBuffer](int error, std::size_t){

			if (error) {
				sock.close(); 
				return;
			}

			do_read_request(); // waiting for a new request

		});
	}


private:

	static constexpr int BUFFER_SIZE = 1024 * 4;

	EventLoop& loop;
	Socket sock;
	StringSender& string_sender;
	char buffer[BUFFER_SIZE];
	RequestAccumulator accumulator;
};




struct Worker {

	Worker(const Endpoint& proc_ep, const Endpoint& disp_ep) 
		: loop() 
		, acceptor(loop, proc_ep)
		, display_socket(loop)
		, string_sender(display_socket)
	{
		//connecting to the display server
		display_socket.connect(disp_ep); // this should be a separate method...
		std::cout << "Successully connected to the display server!" << std::endl;
	}

	void run() {

		do_accept();

		loop.run();
	}

private:

	void do_accept() {

		auto new_connection = ClientConnection::create_connection(loop, string_sender);
		acceptor.async_accept(new_connection->socket(), [this, new_connection](int error){

			if (!error) {
				new_connection->start();
			}
			else if (error == EventLoop::EVENTLOOP_STOPPED) {
				acceptor.close();
				return;
			}
			else {
				std::cerr << "Failed to accept a new client!" << std::endl;
			}

			do_accept();

		});

	}

private:
	
	EventLoop loop;
	Acceptor acceptor;
	Socket display_socket;
	StringSender string_sender;
};


//this is the processing server
struct Server {


	Server(const Endpoint& proc_ep, const Endpoint& disp_ep, std::size_t count_workers) 
		: proc_ep(proc_ep)
		, disp_ep(disp_ep)
		, count_workers(count_workers)
	{}

	void start() { ////

		std::cout << "Server is running...\n";
		std::vector<std::thread> workers;

		for(std::size_t i = 0; i != count_workers; ++i) {
			try {
				workers.emplace_back([this]{

					try {

						Worker worker(proc_ep, disp_ep);
						worker.run();
					}
					catch(std::exception& e) {
						std::cerr << "Thread finished with an error : " << e.what() << std::endl; 
					}
				});
			}
			catch(std::exception& e) {
				std::cerr << "Failed to create a new thread: " << e.what() << std::endl;
				for(std::size_t j = 0; j != count_workers; ++j) {
					workers[j].join();
				}

				throw;
			}
		}

		for(std::thread& w : workers) {
			w.join();
		}
	}

private:

	Endpoint proc_ep;
	Endpoint disp_ep;
	std::size_t count_workers;

};


int main(int argc, char* argv[]) {


	try {

		ProcConfig config(argc, argv);

		Endpoint proc_ep(config.getServerPort(), 0); // processing server's (this one) endpoint
		Endpoint disp_ep(config.getDisplayPort(), config.getDisplayIp()); // display server's endpoint

		Server server(proc_ep, disp_ep, std::thread::hardware_concurrency());
		server.start();

		return 0;
	}
	catch(std::exception& e) {
		std::cout << "error : " << e.what() << std::endl;
		return 1;
	}

}