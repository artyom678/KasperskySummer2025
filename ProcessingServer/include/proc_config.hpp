#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>
#include <regex>


struct ProcConfig {

    ProcConfig(int argc, char* argv[]) {
        parse(argc, argv);
    }

    uint16_t getServerPort() const { return serverPort; }
    const std::string& getDisplayIp() const { return displayIp; }
    uint16_t getDisplayPort() const { return displayPort; }

private:

    void parse(int argc, char* argv[]) {
        if (argc != 4) {
            throw std::invalid_argument("Usage: " + std::string(argv[0]) + " <server_port> <display_ip> <display_port>");
        }

        // Processing-server port validation
        try {
            int portValue = std::stoi(argv[1]);
            if (portValue < 1 || portValue > 65535) {
                throw std::out_of_range("Server port must be between 1 and 65535");
            }
            serverPort = static_cast<uint16_t>(portValue);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Invalid server port: " + std::string(argv[1]));
        }

        // Display-server IP-address validation (ipV4)
        displayIp = argv[2];
        std::regex ipRegex(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
        std::smatch match;
        if (!std::regex_match(displayIp, match, ipRegex)) {
            throw std::invalid_argument("Invalid display server IP address: " + displayIp);
        }
        for (size_t i = 1; i <= 4; ++i) {
            int octet = std::stoi(match[i]);
            if (octet < 0 || octet > 255) {
                throw std::invalid_argument("Invalid display server IP address octet: " + match[i].str());
            }
        }

        // Валидация порта лог-сервера
        try {
            int portValue = std::stoi(argv[3]);
            if (portValue < 1 || portValue > 65535) {
                throw std::out_of_range("Display server port must be between 1 and 65535");
            }
            displayPort = static_cast<uint16_t>(portValue);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Invalid display server port: " + std::string(argv[3]));
        }
    }

    uint16_t serverPort;
    std::string displayIp;
    uint16_t displayPort;
};