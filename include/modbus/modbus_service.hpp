#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "result.hpp"
#include "transport/tcp/modbus_transport.hpp"

namespace modbus::service {

constexpr uint16_t INIT_TRANS_ID = 0x0001;

class Service final {
public:
    explicit Service(std::unique_ptr<transport::IModbusTcpTransport> transport);

    Result readHoldingRegisters(uint8_t slave_id, uint16_t reg_addr, uint16_t reg_count,
                                std::vector<uint16_t>& response);

    Result readInputRegisters(uint8_t slave_id, uint16_t reg_addr, uint16_t reg_count,
                              std::vector<uint16_t>& response);

    Result writeSingleRegister(uint8_t slave_id, uint16_t reg_addr, uint16_t value);

    Result writeMultipleRegisters(uint8_t slave_id, uint16_t reg_addr,
                                  const std::vector<uint16_t>& values);

private:
    std::mutex mutex_;
    std::atomic<uint16_t> transaction_id_{INIT_TRANS_ID};

    std::mutex transport_mutex_;
    std::unique_ptr<transport::IModbusTcpTransport> transport_;

    template<class EncodeFunc>
    Result executeRequest(uint8_t current_slave_id, EncodeFunc encode_pdu,
                          std::vector<uint16_t>* modbus_registers = nullptr);
};
} // namespace modbus
