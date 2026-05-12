#pragma once

#include "transport/modbus_transport.hpp"

namespace modbus::transport {

class TcpTransport final : public IModbusTcpTransport {
public:
    explicit TcpTransport(Config config);
    ~TcpTransport() override;

    [[nodiscard]] Result connect() override;
    void disconnect() override;

    [[nodiscard]] Result send(const std::vector<uint8_t>& request) override;
    [[nodiscard]] Result receive(std::vector<uint8_t>& raw_response, size_t expected_size) override;

    [[nodiscard]] const Config& config() const override;

private:
    void flush() const;

    Config config_;
    int socket_fd_;
};
} // namespace transport
