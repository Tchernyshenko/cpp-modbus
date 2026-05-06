#include "transport/tcp/tcp_transport.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

#include <logger/logger.hpp>
#include <netinet/tcp.h>

namespace transport {

constexpr int INVALID = -1;

constexpr int ENABLE = 1;
constexpr int KEEP_IDLE = 10;
constexpr int KEEP_INTERVAL = 5;
constexpr int KEEP_COUNT = 3;

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

    // Включаем Keep-Alive
    setsockopt(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &ENABLE, sizeof(ENABLE));
    // Через какое время простоя начать слать пробы
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_KEEPIDLE, &KEEP_IDLE, sizeof(KEEP_IDLE));
    // Интервал между пробами, если не ответили
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_KEEPINTVL, &KEEP_INTERVAL, sizeof(KEEP_INTERVAL));
    // Количество неудачных проб до разрыва
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_KEEPCNT, &KEEP_COUNT, sizeof(KEEP_COUNT));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_.port));
    if (inet_pton(AF_INET, config_.ip.c_str(), &addr.sin_addr) <= 0) {
        SHOW_ERROR("TCP", "Invalid IP address: " + config_.ip);
        disconnect();
        return Result::INVALID_CONFIG;
    }

    const timeval timeout{
        config_.timeout_ms / 1000,         // секунды
        (config_.timeout_ms % 1000) * 1000 // микросекунды
    };

    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    SHOW_INFO("TCP", "Connected to " + config_.ip + ":" + std::to_string(config_.port));

    if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        const std::string err_msg = strerror(err);
        disconnect();
        switch (err) {
            case ETIMEDOUT:
            case EINPROGRESS:
                SHOW_WARN("TCP", "Connection timeout (or in progress) to " + config_.ip);
                return Result::CONNECT_TIMEOUT; // disconnect() уже вызван выше, дублировать не нужно

            case ECONNREFUSED:
                SHOW_ERROR("TCP", "Connection refused by " + config_.ip);
                return Result::CONNECTION_REFUSED;

            case ENETUNREACH:
            case EHOSTUNREACH:
                SHOW_ERROR("TCP", "Network unreachable for " + config_.ip);
                return Result::NETWORK_UNREACHABLE;

            default:
                SHOW_ERROR("TCP", "Unknown error (" + std::to_string(err) + "): " + err_msg);
                return Result::NETWORK_ERROR;
        }

    }

    return Result::OK;
}

void TcpTransport::disconnect() {
    if (socket_fd_ != INVALID) {
        close(socket_fd_);
        socket_fd_ = INVALID;
        SHOW_WARN("TCP", "Socket disconnected");
    }
}

void TcpTransport::flush() {
    if (socket_fd_ == INVALID)
        return;

    uint8_t buffer[1024];
    ssize_t bytes_read;

    // Вычитываем всё до тех пор, пока буфер не опустеет
    // MSG_DONTWAIT делает вызов неблокирующим
    while ((bytes_read = ::recv(socket_fd_, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0) {
        SHOW_WARN("TCP", "Flushed " + std::to_string(bytes_read) + " bytes of stale data");
    }
}

Result TcpTransport::send(const std::vector<uint8_t>& request) {
    // В будущем здесь будет lock_guard для мьютекса

    if (socket_fd_ != INVALID) {
        uint8_t dummy;
        ssize_t res = ::recv(socket_fd_, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);

        if (res == 0) {
            SHOW_WARN("TCP", "Peer closed connection (FIN), reconnecting...");
            disconnect();
        } else if (res < 0 && (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            SHOW_WARN("TCP", "Socket error detected via PEEK: " + std::string(strerror(errno)));
            disconnect();
        }

        // Если сокет все еще жив, чистим мусор
        if (socket_fd_ != INVALID) {
            flush();
        }
    }

    // 2. Если сокет невалиден (был закрыт ранее или только что через PEEK) — коннектимся
    if (socket_fd_ == INVALID) {
        if (const auto result = connect(); result != Result::OK) {
            return result;
        }
    }

    // 3. Пытаемся отправить данные
    size_t total_sent = 0;
    const uint8_t* ptr = request.data();
    size_t remaining = request.size();

    while (remaining > 0) {
        const ssize_t sent = ::send(socket_fd_, ptr + total_sent, remaining, MSG_NOSIGNAL);

        if (sent < 0) {
            // Если получили ошибку записи (EPIPE или ECONNRESET) на первом проходе
            // есть смысл один раз попробовать переподключиться
            if ((errno == EPIPE || errno == ECONNRESET) && total_sent == 0) {
                SHOW_WARN("TCP", "Broken pipe on send, attempting instant reconnect...");
                disconnect();
                if (connect() == Result::OK) {
                    // После переподключения обнуляем счетчики и пробуем снова (через continue)
                    continue;
                }
            }

            if (errno == EINTR)
                continue;

            const std::string err_msg = strerror(errno);
            SHOW_ERROR("TCP", "Send failed: " + err_msg);
            disconnect();
            return Result::SEND_FAILED;
        }

        total_sent += sent;
        remaining -= sent;
    }

    return Result::OK;
}

Result TcpTransport::receive(std::vector<uint8_t>& raw_response, const size_t expected_size) {
    raw_response.resize(expected_size);

    // MSG_WAITALL заставляет ядро ждать заполнения всего буфера.
    // Если сработает SO_RCVTIMEO, recv вернет -1 (EAGAIN) или меньше байт, чем просили.
    const ssize_t received = ::recv(socket_fd_, raw_response.data(), expected_size, MSG_WAITALL);

    if (received < 0) {
        if (errno == EINTR) {
            // Если прервано сигналом, можно попробовать перевызвать,
            // но в контексте EVerest проще считать это ошибкой и переподключиться.
            return receive(raw_response, expected_size);
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            SHOW_WARN("TCP", "Receive timeout occurred at " + config_.ip);
            disconnect();
            return Result::RECV_FAILED;
        }
        SHOW_ERROR("TCP", "Receive error: " + std::string(strerror(errno)));
        disconnect();
        return Result::RECV_FAILED;
    }

    if (received == 0) {
        SHOW_WARN("TCP", "Connection closed by peer");
        disconnect();
        return Result::CONNECTION_CLOSED;
    }

    if (static_cast<size_t>(received) < expected_size) {
        SHOW_ERROR("TCP", "Incomplete data received: " + std::to_string(received) + "/" +
                              std::to_string(expected_size));
        // Частичные данные для Modbus бесполезны, закрываемся для очистки буферов
        disconnect();
        return Result::RECV_FAILED;
    }

    return Result::OK;
}

const Config& TcpTransport::config() const {
    return config_;
}
} // namespace transport
