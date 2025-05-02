#pragma once

#include <string>
#include <cstdint>
#include <unistd.h>
#include <stdexcept>
#include <regex>

struct ClientConfig {
    
    ClientConfig(int argc, char* argv[]) {
        parse(argc, argv);
    }

    const std::string& getIp() const { return ip; }
    uint16_t getPort() const { return port; }

private:

    void parse(int argc, char* argv[]) {
        if (argc != 3) {
            throw std::invalid_argument("Usage: " + std::string(argv[0]) + " <ip> <port>");
        }

        ip = argv[1];

        // IP-address validation (IPv4)
        std::regex ipRegex(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
        std::smatch match;
        if (!std::regex_match(ip, match, ipRegex)) {
            throw std::invalid_argument("Invalid IP address: " + ip);
        }
        for (size_t i = 1; i <= 4; ++i) {
            int octet = std::stoi(match[i]);
            if (octet < 0 || octet > 255) {
                throw std::invalid_argument("Invalid IP address octet: " + match[i].str());
            }
        }

        // Port number validation
        try {
            int portValue = std::stoi(argv[2]);
            if (portValue < 1 || portValue > 65535) {
                throw std::out_of_range("Port must be between 1 and 65535");
            }
            port = static_cast<uint16_t>(portValue);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Invalid port: " + std::string(argv[2]));
        }
    }

    std::string ip;
    uint16_t port;
};
