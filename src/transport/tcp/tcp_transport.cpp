#include "transport/tcp/tcp_transport.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

#include <logger/logger.hpp>

namespace transport {

constexpr int INVALID = -1;

TcpTransport::TcpTransport(Config config) : config_(std::move(config)), socket_fd_(INVALID) {
}

TcpTransport::~TcpTransport() {
    disconnect();
}

Result TcpTransport::connect() {
    disconnect();

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ == INVALID) {
        SHOW_ERROR("TCP", "Failed to create socket: " + std::string(strerror(errno)));
        return Result::NETWORK_ERROR;
    }

    const timeval timeout{
        config_.timeout_ms / 1000,         // секунды
        (config_.timeout_ms % 1000) * 1000 // микросекунды
    };
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_.port));
    if (inet_pton(AF_INET, config_.ip.c_str(), &addr.sin_addr) <= 0) {
        SHOW_ERROR("TCP", "Invalid IP address: " + config_.ip);
        disconnect();
        return Result::INVALID_CONFIG;
    }

    if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        const std::string err_msg = strerror(err);
        disconnect();
        switch (err) {
            case ETIMEDOUT:
                SHOW_ERROR("TCP", "Connection timed out to " + config_.ip + ":" +
                                      std::to_string(config_.port));
                return Result::CONNECT_TIMEOUT;
            case ECONNREFUSED:
                SHOW_ERROR("TCP", "Connection refused by " + config_.ip + ":" +
                                      std::to_string(config_.port));
                return Result::CONNECTION_REFUSED;
            case ENETUNREACH:
            case EHOSTUNREACH:
                SHOW_ERROR("TCP", "Network unreachable for " + config_.ip + ":" +
                                      std::to_string(config_.port));
                return Result::NETWORK_UNREACHABLE;
            default:
                SHOW_ERROR("TCP",
                           "Unknown connection error (" + std::to_string(err) + "): " + err_msg);
                return Result::NETWORK_ERROR;
        }
    }
    SHOW_INFO("TCP", "Connected to " + config_.ip + ":" + std::to_string(config_.port));
    return Result::OK;
}

void TcpTransport::disconnect() {
    if (socket_fd_ != INVALID) {
        close(socket_fd_);
        socket_fd_ = INVALID;
        SHOW_DEBUG("TCP", "Socket disconnected");
    }
}

Result TcpTransport::send(const std::vector<uint8_t>& request) {
    if (socket_fd_ == INVALID) {
        if (const auto result = connect(); result != Result::OK) {
            return result;
        }
    }

    size_t total_sent = 0;
    const uint8_t* ptr = request.data();
    size_t remaining = request.size();

    while (remaining > 0) {
        const ssize_t sent = ::send(socket_fd_, ptr + total_sent, remaining, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                // Прервано сигналом или временная блокировка — повторяем
                continue;
            }
            const std::string err_msg = strerror(errno);
            disconnect();
            SHOW_ERROR("TCP", "Send failed: " + err_msg);
            return Result::SEND_FAILED;
        }

        total_sent += sent;
        remaining -= sent;
    }

    return Result::OK;
}

Result TcpTransport::receive(std::vector<uint8_t>& raw_response, const size_t expected_size) {
    raw_response.resize(expected_size);
    size_t total = 0;

    while (total < expected_size) {
        const ssize_t received =
            recv(socket_fd_, raw_response.data() + total, expected_size - total, 0);
        // Внутри receive()
        if (received < 0) {
            if (errno == EINTR) {
                continue; // Сигнал, можно пробовать снова
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Это тайм-аут, установленный через setsockopt
                SHOW_WARN("TCP", "Receive timeout occurred");
                return Result::RECV_FAILED; // Или Result::TIMEOUT, если добавите в enum
            }
            // Другие ошибки
            disconnect();
            return Result::RECV_FAILED;
        }
        if (received == 0) {
            SHOW_WARN("TCP", "Connection closed by peer during receive");
            disconnect();
            return Result::CONNECTION_CLOSED;
        }
        total += received;
    }
    return Result::OK;
}

const Config& TcpTransport::config() const {
    return config_;
}
} // namespace transport
