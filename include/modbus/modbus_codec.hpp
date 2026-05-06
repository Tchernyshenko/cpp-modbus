#pragma once

#include <cstdint>
#include <vector>

#include "result.hpp"

namespace modbus {
class ModbusCodec {
public:
    static std::vector<uint8_t> encodeModbusHeader(uint16_t transaction_id,
                                                   uint16_t pdu_size,
                                                   uint8_t slave_id);

    // Read Holding Registers (03)
    static std::vector<uint8_t> encodeReadHoldingRegisters(uint16_t reg_addr,
                                                           uint16_t reg_count);

    // Read Input Registers (04)
    static std::vector<uint8_t> encodeReadInputRegisters(uint16_t reg_addr,
                                                         uint16_t reg_count);

    // Write Single Register (06)
    static std::vector<uint8_t> encodeWriteSingleRegister(uint16_t reg_addr,
                                                          uint16_t value);

    // Write Multiple Registers (16)
    static std::vector<uint8_t> encodeWriteMultipleRegisters(
        uint16_t reg_addr, const std::vector<uint16_t>& values);

    static Result parseMbapHeader(uint16_t expected_trans_id,
                                  const std::vector<uint8_t>& raw_mbap);

    static Result parsePdu(uint8_t expected_slave_id,
                           const std::vector<uint8_t>& pdu,
                           std::vector<uint16_t>& modbus_registers);

    // Утилита: упаковка uint16_t в big-endian
    static void appendUint16(std::vector<uint8_t>& buf, const uint16_t value) {
        buf.push_back(static_cast<uint8_t>(value >> 8));
        buf.push_back(static_cast<uint8_t>(value & 0xFF));
    }
};
} // namespace modbus
