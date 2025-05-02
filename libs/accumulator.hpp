#pragma once

#include <string>
#include "http_processor.hpp"
#include <stdexcept>


//We need these 2 classes for situations, when the request or response is too large as we can not
//read it in one shot, so we accumulate it using RequestAccumulator or ResponseAccumulator
//respectively. Inside use HttpProcessor::parsePostRequest and HtppProcessor::parseResponse to
//parse the request/response.


struct RequestAccumulator {

    void append(const char* data, std::size_t bytes) {
        if (buffer.size() + bytes > maxRequestSize) {
            throw std::runtime_error("Request size exceeds limit");
        }
        buffer.append(data, bytes);
        parseResult = HttpProcessor::parsePostRequest(std::string_view(buffer));
    }

    bool isComplete() const { return parseResult.isComplete; }
    const HttpProcessor::ParseResult& getParseResult() const { return parseResult; }
    std::string getRequest() const { return buffer; }
    void reset() { buffer.clear(); parseResult = {}; }

private:

    std::string buffer;
    HttpProcessor::ParseResult parseResult;
    static constexpr size_t maxRequestSize = 1024 * 1024;
};


struct ResponseAccumulator {

    void append(const char* data, std::size_t bytes) {
        if (buffer.size() + bytes > maxResponseSize) {
            throw std::runtime_error("Response size exceeds limit");
        }
        buffer.append(data, bytes);
        parseResult = HttpProcessor::parseResponse(std::string_view(buffer));
    }

    bool isComplete() const { return parseResult.isComplete; }
    const HttpProcessor::ParseResult& getParseResult() const { return parseResult; }
    std::string getResponse() const { return buffer; }
    void reset() { buffer.clear(); parseResult = {}; }

private:

    std::string buffer;
    HttpProcessor::ParseResult parseResult;
    static constexpr size_t maxResponseSize = 1024 * 1024; 
};