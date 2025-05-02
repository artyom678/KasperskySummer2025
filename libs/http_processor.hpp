#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>
#include <string_view>

using json = nlohmann::json;


struct HttpProcessor {

	struct ParseResult {
		bool isComplete{false};
		bool success{false};
		json data{};
		std::string error{};
		std::size_t bodyLength{0};
		bool headersComplete{false};
		int statusCode{0}; // for http-responses only
	};

	inline static std::string buildPostRequest(const std::string& json_data, const std::string& host_ip, std::uint16_t host_port) {
        std::stringstream request;
        request << "POST / HTTP/1.1\r\n";
        request << "Host: " << host_ip << ":" << host_port << "\r\n";
        request << "Content-Type: application/json\r\n";
        request << "Content-Length: " << json_data.length() << "\r\n";
        request << "Connection: keep-alive\r\n";
        request << "\r\n";
        request << json_data;
        return request.str();
    }

    inline static ParseResult parsePostRequest(std::string_view request) {
        
        ParseResult result;

        if (!result.headersComplete) {
            size_t headerEnd = request.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                result.headersComplete = true;

                // check Content-Type
                if (request.find("Content-Type: application/json") == std::string::npos) {
                    result.error = "Invalid HTTP request: Content-Type must be application/json";
                    return result;
                }

                // extract Content-Length
                size_t contentLengthPos = request.find("Content-Length: ");
                if (contentLengthPos != std::string::npos) {
                    size_t lengthStart = contentLengthPos + 16;
                    size_t lengthEnd = request.find("\r\n", lengthStart);
                    if (lengthEnd != std::string::npos) {
                        std::string_view lengthStr = request.substr(lengthStart, lengthEnd - lengthStart);
                        try {
                            result.bodyLength = std::stoul(std::string(lengthStr));
                            if (result.bodyLength > maxRequestSize) {
                                result.error = "Content-Length exceeds limit";
                                return result;
                            }
                        } catch (...) {
                            result.error = "Invalid Content-Length";
                            return result;
                        }
                    }
                }

                if (result.bodyLength == 0 || request.length() >= headerEnd + 4 + result.bodyLength) {
                    result.isComplete = true;
                }
            }
        } else if (result.bodyLength > 0 && request.length() >= result.bodyLength + request.find("\r\n\r\n") + 4) {
            result.isComplete = true;
        }

        if (result.isComplete) {
            size_t bodyStart = request.find("\r\n\r\n");
            if (bodyStart == std::string::npos) {
                result.success = false;
                result.error = "Invalid HTTP request";
                return result;
            }

            std::string_view body = request.substr(bodyStart + 4);
            if (body.length() != result.bodyLength) {
                result.success = false;
                result.error = "Body length does not match Content-Length";
                return result;
            }

            try {
                result.data = json::parse(body);
                result.success = true;
            } catch (...) {
                result.success = false;
                result.error = "Invalid JSON";
            }
        }

        return result;
    }

    inline static std::string buildResponse(int statusCode, const std::string& json_data) {
        
        auto key = statusCode ^ std::hash<std::string>{}(json_data);
        auto it = cachedResponses.find(key);
        if (it != cachedResponses.end()) {
            return it->second;
        }

        std::stringstream response;
        response << "HTTP/1.1 " << statusCode << " " << (statusCode == 200 ? "OK" : "Bad Request") << "\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << json_data.length() << "\r\n";
        response << "Connection: keep-alive\r\n";
        response << "\r\n";
        response << json_data;

        auto result = response.str();
        cachedResponses.emplace(key, result);
        return result;
    }


    inline static ParseResult parseResponse(std::string_view response) {
        
        ParseResult result;

        // check the status line
        size_t firstLineEnd = response.find("\r\n");
        if (firstLineEnd == std::string::npos) {
            result.error = "Invalid HTTP response: missing status line";
            return result;
        }

        std::string_view statusLine = response.substr(0, firstLineEnd);
        if (!statusLine.starts_with("HTTP/1.1 ")) { // since C++20
            result.error = "Invalid HTTP response: incorrect protocol";
            return result;
        }

        size_t codeStart = statusLine.find(' ', 9);
        if (codeStart == std::string::npos) {
            result.error = "Invalid HTTP response: missing status code";
            return result;
        }

        std::string_view codeStr = statusLine.substr(9, codeStart - 9);
        try {
            result.statusCode = std::stoi(std::string(codeStr));
        } catch (...) {
            result.error = "Invalid HTTP response: invalid status code";
            return result;
        }

        // parse headers
        if (!result.headersComplete) {
            size_t headerEnd = response.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                result.headersComplete = true;

                // Проверяем Content-Type
                if (response.find("Content-Type: application/json") == std::string::npos) {
                    result.error = "Invalid HTTP response: Content-Type must be application/json";
                    return result;
                }

                // Извлекаем Content-Length
                size_t contentLengthPos = response.find("Content-Length: ");
                if (contentLengthPos != std::string::npos) {
                    size_t lengthStart = contentLengthPos + 16;
                    size_t lengthEnd = response.find("\r\n", lengthStart);
                    if (lengthEnd != std::string::npos) {
                        std::string_view lengthStr = response.substr(lengthStart, lengthEnd - lengthStart);
                        try {
                            result.bodyLength = std::stoul(std::string(lengthStr));
                            if (result.bodyLength > maxRequestSize) {
                                result.error = "Content-Length exceeds limit";
                                return result;
                            }
                        } catch (...) {
                            result.error = "Invalid Content-Length";
                            return result;
                        }
                    }
                }

                if (result.bodyLength == 0 || response.length() >= headerEnd + 4 + result.bodyLength) {
                    result.isComplete = true;
                }
            }
        } else if (result.bodyLength > 0 && response.length() >= result.bodyLength + response.find("\r\n\r\n") + 4) {
            result.isComplete = true;
        }

        if (result.isComplete) {
            size_t bodyStart = response.find("\r\n\r\n");
            if (bodyStart == std::string::npos) {
                result.success = false;
                result.error = "Invalid HTTP response";
                return result;
            }

            std::string_view body = response.substr(bodyStart + 4);
            if (body.length() != result.bodyLength) {
                result.success = false;
                result.error = "Body length does not match Content-Length";
                return result;
            }

            try {
                result.data = json::parse(body);
                result.success = true;
            } catch (...) {
                result.success = false;
                result.error = "Invalid JSON";
            }
        }

        return result;
    }

private:

	inline static std::unordered_map<size_t, std::string> cachedResponses;
    static constexpr size_t maxRequestSize = 1024 * 1024; 

};