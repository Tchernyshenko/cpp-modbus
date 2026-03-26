#pragma once

#include "transport/transport.hpp"

namespace transport {
class TcpTransport final : public ITransport {
public:
    explicit TcpTransport(Config config);
    ~TcpTransport() override;

    [[nodiscard]] Result connect() override;
    void disconnect() override;

    [[nodiscard]] Result send(const std::vector<uint8_t>& request) override;
    [[nodiscard]] Result receive(std::vector<uint8_t>& raw_response, size_t expected_size) override;

    [[nodiscard]] const Config& config() const override;

private:
    Config config_;
    int socket_fd_;
};
} // namespace transport
