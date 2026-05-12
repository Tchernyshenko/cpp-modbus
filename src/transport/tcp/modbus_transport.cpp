#include "transport/tcp/modbus_transport.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "logger/logger.hpp"

namespace modbus::transport {

// Константы для работы с сокетом
constexpr int INVALID = -1; // Значение, обозначающее невалидный файловый дескриптор сокета

constexpr int ENABLE = 1; // Флаг включения опции (используется в setsockopt)
constexpr int KEEP_IDLE = 10; // Время простоя (в секундах) перед отправкой первого keep‑alive пакета
constexpr int KEEP_INTERVAL = 5; // Интервал (в секундах) между повторными keep‑alive пробами
constexpr int KEEP_COUNT = 3; // Количество неудачных проб до разрыва соединения

TcpTransport::TcpTransport(Config config) : config_(std::move(config)), socket_fd_(INVALID) {
}

TcpTransport::~TcpTransport() {
    disconnect();
}

Result TcpTransport::connect() {
    // Гарантируем, что предыдущее соединение закрыто
    disconnect();

    // Создаём TCP‑сокет (AF_INET — IPv4, SOCK_STREAM — потоковый сокет)
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ == INVALID) {
        SHOW_ERROR("Modbus:TCP", "Failed to create socket: " + std::string(strerror(errno)));
        return Result::NETWORK_ERROR;
    }

    // Настраиваем параметры keep‑alive для отслеживания состояния соединения
    setsockopt(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &ENABLE, sizeof(ENABLE));
    // Время простоя перед первой пробой
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_KEEPIDLE, &KEEP_IDLE, sizeof(KEEP_IDLE));
    // Интервал между пробами при отсутствии ответа
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_KEEPINTVL, &KEEP_INTERVAL, sizeof(KEEP_INTERVAL));
    // Количество проб до разрыва
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_KEEPCNT, &KEEP_COUNT, sizeof(KEEP_COUNT));

    // Заполняем структуру адреса сервера
    sockaddr_in addr{};
    addr.sin_family = AF_INET; // Используем IPv4
    addr.sin_port =
        htons(static_cast<uint16_t>(config_.port)); // Преобразуем порт в сетевой порядок байт
    // Преобразуем IP‑адрес из текстового формата в бинарный
    if (inet_pton(AF_INET, config_.ip.c_str(), &addr.sin_addr) <= 0) {
        SHOW_ERROR("Modbus:TCP", "Invalid IP address: " + config_.ip);
        disconnect();
        return Result::INVALID_CONFIG;
    }

    // Устанавливаем таймауты на приём и отправку данных
    const timeval timeout{
        config_.timeout_ms / 1000,         // секунды
        (config_.timeout_ms % 1000) * 1000 // микросекунды
    };

    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    SHOW_INFO("Modbus:TCP", "Attempt connect to " + config_.ip + ":" + std::to_string(config_.port));

    // Пытаемся установить соединение
    if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        const std::string err_msg = strerror(err);
        disconnect();
        switch (err) {
            case ETIMEDOUT:
            case EINPROGRESS:
                SHOW_WARN("Modbus:TCP", "Connection timeout (or in progress) to " + config_.ip);
                return Result::CONNECT_TIMEOUT;

            case ECONNREFUSED:
                SHOW_ERROR("Modbus:TCP", "Connection refused by " + config_.ip);
                return Result::CONNECTION_REFUSED;

            case ENETUNREACH:
            case EHOSTUNREACH:
                SHOW_ERROR("Modbus:TCP", "Network unreachable for " + config_.ip);
                return Result::NETWORK_UNREACHABLE;

            default:
                SHOW_ERROR("Modbus:TCP", "Unknown error (" + std::to_string(err) + "): " + err_msg);
                return Result::NETWORK_ERROR;
        }
    }

    SHOW_INFO("Modbus:TCP", "Connected to " + config_.ip + ":" + std::to_string(config_.port));
    return Result::OK;
}

void TcpTransport::disconnect() {
    if (socket_fd_ != INVALID) {
        close(socket_fd_); // Закрываем файловый дескриптор сокета
        socket_fd_ = INVALID; // Помечаем сокет как невалидный
        SHOW_WARN("Modbus:TCP", "Socket disconnected");
    }
}

Result TcpTransport::send(const std::vector<uint8_t>& request) {
    if (socket_fd_ != INVALID) {
        uint8_t dummy;

        // Проверяем состояние сокета без чтения данных (MSG_PEEK) и без блокировки (MSG_DONTWAIT)
        ssize_t res = recv(socket_fd_, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);

        if (res == 0) {
            // Сервер закрыл соединение (получен FIN)
            SHOW_WARN("Modbus:TCP", "Peer closed connection (FIN), reconnecting...");
            disconnect();
        } else if (res < 0 && (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            // Обнаружена ошибка сокета
            SHOW_WARN("Modbus:TCP", "Socket error detected via PEEK: " + std::string(strerror(errno)));
            disconnect();
        }

        // Если сокет всё ещё активен, очищаем буфер от старых данных
        if (socket_fd_ != INVALID) {
            flush();
        }
    }

    // Если сокет невалиден (был закрыт ранее), пытаемся подключиться
    if (socket_fd_ == INVALID) {
        if (const auto result = connect(); result != Result::OK) {
            return result;
        }
    }

    // Отправляем данные по частям, пока не передадим весь запрос
    size_t total_sent = 0;
    const uint8_t* ptr = request.data(); // Указатель на начало данных в векторе запроса
    size_t remaining = request.size(); // Количество байт, которые ещё нужно отправить

    while (remaining > 0) {
        // Пытаемся отправить оставшиеся данные
        // MSG_NOSIGNAL предотвращает генерацию SIGPIPE при разрыве соединения
        const ssize_t sent = ::send(socket_fd_, ptr + total_sent, remaining, MSG_NOSIGNAL);

        if (sent < 0) {
            if ((errno == EPIPE || errno == ECONNRESET) && total_sent == 0) {
                // Разрыв соединения при первой попытке отправки — пробуем переподключиться
                SHOW_WARN("Modbus:TCP", "Broken pipe on send, attempting instant reconnect...");
                disconnect();

                // После успешного переподключения сбрасываем счётчики и начинаем заново
                if (connect() == Result::OK) {
                    continue;
                }
            }

            if (errno == EINTR) {
                // Отправка прервана сигналом — продолжаем цикл, чтобы повторить попытку
                continue;
            }

            // Любая другая ошибка отправки — логируем и возвращаем ошибку
            const std::string err_msg = strerror(errno);
            SHOW_ERROR("Modbus:TCP", "Send failed: " + err_msg);
            disconnect();
            return Result::SEND_FAILED;
        }

        // Обновляем счётчики: сколько отправлено и сколько осталось
        total_sent += sent;
        remaining -= sent;
    }

    return Result::OK;
}

Result TcpTransport::receive(std::vector<uint8_t>& raw_response, const size_t expected_size) {
    // Выделяем память под ожидаемый объём данных
    raw_response.resize(expected_size);

    // Пытаемся получить данные, ожидая заполнения всего буфера (MSG_WAITALL)
    // Если сработает таймаут (SO_RCVTIMEO), recv вернёт -1 (EAGAIN) или меньше байт, чем просили
    const ssize_t received = recv(socket_fd_, raw_response.data(), expected_size, MSG_WAITALL);

    if (received < 0) {
        if (errno == EINTR) {
            // Приём прерван сигналом — рекурсивно повторяем попытку
            // В контексте EVerest это считается ошибкой, но здесь реализована повторная попытка
            return receive(raw_response, expected_size);
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Таймаут приёма — соединение разорвано или данные не пришли вовремя
            SHOW_WARN("Modbus:TCP", "Receive timeout occurred at " + config_.ip);
            disconnect();
            return Result::RECV_FAILED;
        }

        // Любая другая ошибка приёма — логируем и возвращаем код ошибки
        SHOW_ERROR("Modbus:TCP", "Receive error: " + std::string(strerror(errno)));
        disconnect();
        return Result::RECV_FAILED;
    }

    if (received == 0) {
        // Сервер закрыл соединение корректно (получен FIN)
        SHOW_WARN("Modbus:TCP", "Connection closed by peer");
        disconnect();
        return Result::CONNECTION_CLOSED;
    }

    if (static_cast<size_t>(received) < expected_size) {
        // Получено меньше данных, чем ожидалось — для протоколов типа Modbus это критично
        SHOW_ERROR("Modbus:TCP", "Incomplete data received: " + std::to_string(received) + "/" +
                              std::to_string(expected_size));
        // Частичные данные бесполезны — закрываем соединение для очистки буферов
        disconnect();
        return Result::RECV_FAILED;
    }

    return Result::OK;
}

const Config& TcpTransport::config() const {
    return config_;
}

void TcpTransport::flush() const {
    if (socket_fd_ == INVALID)
        return;

    uint8_t buffer[1024];
    ssize_t bytes_read;

    // Читаем данные, пока буфер не опустеет (MSG_DONTWAIT — неблокирующий вызов)
    while ((bytes_read = recv(socket_fd_, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0) {
        SHOW_WARN("Modbus:TCP", "Flushed " + std::to_string(bytes_read) + " bytes of stale data");
    }
}
} // namespace transport
