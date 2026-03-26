#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace transport {

struct Config {
    std::string ip;
    int port = 502;
    int timeout_ms = 1000;
    int retries = 3;
};

enum class Result {
    OK = 0,

    // Конфигурационные ошибки
    INVALID_CONFIG,

    // Ошибки подключения
    CONNECT_TIMEOUT,
    CONNECTION_REFUSED,
    NETWORK_UNREACHABLE,

    // Общие сетевые ошибки
    NETWORK_ERROR,

    // Ошибки передачи данных
    SEND_FAILED,
    RECV_FAILED,

    // Закрытие соединения
    CONNECTION_CLOSED
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual Result connect() = 0;
    virtual void disconnect() = 0;

    virtual Result send(const std::vector<uint8_t>& data) = 0;
    virtual Result receive(std::vector<uint8_t>& out, size_t expected_size) = 0;

    [[nodiscard]] virtual const Config& config() const = 0;
};

inline std::string toString(Result r) {
    switch (r) {
        case Result::OK:
            return "OK";
        case Result::CONNECT_TIMEOUT:
            return "CONNECT_TIMEOUT";
        case Result::CONNECTION_REFUSED:
            return "CONNECTION_REFUSED";
        case Result::NETWORK_UNREACHABLE:
            return "NETWORK_UNREACHABLE";
        case Result::NETWORK_ERROR:
            return "NETWORK_ERROR";
        case Result::SEND_FAILED:
            return "SEND_FAILED";
        case Result::RECV_FAILED:
            return "RECV_FAILED";
        case Result::CONNECTION_CLOSED:
            return "CONNECTION_CLOSED";
        default:
            return "UNKNOWN_RESULT(" + std::to_string(static_cast<int>(r)) + ")";
    }
}

} // namespace transport