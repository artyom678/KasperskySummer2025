#pragma once

#include <cstdint>
#include <stdexcept>

struct DisplayConfig {

    DisplayConfig(int argc, char* argv[]) {
        parse(argc, argv);
    }

    uint16_t getPort() const { return port; }

private:

    void parse(int argc, char* argv[]) {
        
        if (argc != 2) {
            throw std::invalid_argument("Usage: " + std::string(argv[0]) + " <port>");
        }

        // Port validation
        try {
            int portValue = std::stoi(argv[1]);
            if (portValue < 1 || portValue > 65535) {
                throw std::out_of_range("Port must be between 1 and 65535");
            }
            port = static_cast<uint16_t>(portValue);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Invalid port: " + std::string(argv[1]));
        }
    }

    uint16_t port;
};