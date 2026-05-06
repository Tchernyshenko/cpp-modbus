#include "modbus/modbus_codec.hpp"

#include <iomanip>
#include <iostream>

#include "logger/logger.hpp"

namespace modbus {

constexpr auto PROTOCOL_ID = static_cast<uint8_t>(0x00);
constexpr auto FUNC_READ_HOLDING = static_cast<uint8_t>(0x03);
constexpr auto FUNC_READ_INPUT = static_cast<uint8_t>(0x04);
constexpr auto FUNC_WRITE_SINGLE = static_cast<uint8_t>(0x06);
constexpr auto FUNC_WRITE_MULTI = static_cast<uint8_t>(0x10);

// Вспомогательная функция для форматирования hex
std::string toHex(uint8_t value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
        << static_cast<int>(value);
    return ss.str();
}

std::vector<uint8_t> ModbusCodec::encodeModbusHeader(const uint16_t transaction_id,
                                                     const uint16_t pdu_size,
                                                     const uint8_t slave_id) {

    const uint16_t length = 1 + pdu_size; // 1 byte slave_id + pdu_size

    std::vector header = {static_cast<uint8_t>(transaction_id >> 8 & 0xFF),
                          static_cast<uint8_t>(transaction_id & 0xFF),
                          PROTOCOL_ID,
                          PROTOCOL_ID,
                          static_cast<uint8_t>(length >> 8 & 0xFF),
                          static_cast<uint8_t>(length & 0xFF),
                          slave_id};

    return header;
}

std::vector<uint8_t> ModbusCodec::encodeReadHoldingRegisters(const uint16_t reg_addr,
                                                             const uint16_t reg_count) {

    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_READ_HOLDING);
    appendUint16(pdu, reg_addr);
    appendUint16(pdu, reg_count);
    return pdu;
}

std::vector<uint8_t> ModbusCodec::encodeReadInputRegisters(const uint16_t reg_addr,
                                                           const uint16_t reg_count) {

    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_READ_INPUT);
    appendUint16(pdu, reg_addr);
    appendUint16(pdu, reg_count);
    return pdu;
}

std::vector<uint8_t> ModbusCodec::encodeWriteSingleRegister(const uint16_t reg_addr,
                                                            const uint16_t value) {

    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_WRITE_SINGLE);
    appendUint16(pdu, reg_addr);
    appendUint16(pdu, value);
    return pdu;
}

std::vector<uint8_t>
ModbusCodec::encodeWriteMultipleRegisters(const uint16_t reg_addr,
                                          const std::vector<uint16_t>& values) {

    if (values.empty() || values.size() > 123) {
        SHOW_WARN("Modbus",
                  "Invalid register count for write multiple: " + std::to_string(values.size()));
        return {};
    }

    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_WRITE_MULTI);
    appendUint16(pdu, reg_addr);
    appendUint16(pdu, static_cast<uint16_t>(values.size()));
    pdu.push_back(static_cast<uint8_t>(values.size() * 2)); // byte count

    for (const uint16_t val : values) {
        appendUint16(pdu, val);
    }
    return pdu;
}

Result ModbusCodec::parseMbapHeader(const uint16_t expected_trans_id,
                                    const std::vector<uint8_t>& raw_mbap) {
    if (raw_mbap.size() < 6) {
        // 6 (MBAP)
        return Result::MBAP_TRUNCATED;
    }

    // 2. Парсим поля
    const uint16_t trans_id = (static_cast<uint16_t>(raw_mbap[0]) << 8) | raw_mbap[1];
    const uint16_t proto_id = (static_cast<uint16_t>(raw_mbap[2]) << 8) | raw_mbap[3];
    const uint16_t length = (static_cast<uint16_t>(raw_mbap[4]) << 8) | raw_mbap[5];

    // 3. Валидация
    if (trans_id != expected_trans_id) {
        SHOW_ERROR("Modbus",
                   "Transaction ID mismatch. Expected: " + std::to_string(expected_trans_id) +
                   ", Got: " + std::to_string(trans_id));
        return Result::TRANSACTION_MISMATCH;
    }
    if (proto_id != 0) {
        SHOW_ERROR("Modbus", "Protocol invalid. Got: " + std::to_string(proto_id));
        return Result::INVALID_PROTOCOL;
    }
    if (length < 2 || length > 253) {
        SHOW_ERROR("Modbus", "Length invalid. Got: " + std::to_string(length));
        return Result::INVALID_PDU_LENGTH;
    }

    return Result::OK;
}

Result ModbusCodec::parsePdu(uint8_t expected_slave_id,
                             const std::vector<uint8_t>& pdu,
                             std::vector<uint16_t>& modbus_registers) {
    if (pdu.size() < 3) {
        SHOW_ERROR("Modbus",
                   "PDU too short (" + std::to_string(pdu.size()) + " bytes). "
                   "Expected at least 3 (slave_id + func_code + byte_count).");
        return Result::PDU_TRUNCATED;
    }

    const uint8_t actual_slave_id = pdu[0];
    if (actual_slave_id != expected_slave_id) {
        SHOW_ERROR("Modbus",
                   "Slave ID mismatch. Expected: " + toHex(expected_slave_id) +
                   ", Got: " + toHex(actual_slave_id));
        return Result::SLAVE_MISMATCH;
    }

    const uint8_t func_code = pdu[1];
    if ((func_code & 0x80) != 0) {
        const uint8_t exception_code = pdu[2];
        SHOW_ERROR("Modbus",
                   "Exception response. Function: " + toHex(func_code & 0x7F) +
                   ", Exception Code: " + toHex(exception_code));
        return static_cast<Result>(exception_code);
    }

    // 2. Логика для ЧТЕНИЯ (0x03, 0x04)
    if (func_code == FUNC_READ_HOLDING || func_code == FUNC_READ_INPUT) {
        const uint8_t byte_count = pdu[2];
        const size_t expected_pdu_size = 3 + byte_count;

        if (pdu.size() != expected_pdu_size) {
            SHOW_ERROR("Modbus",
                       "PDU size mismatch. Byte count: " + std::to_string(byte_count) +
                       ", Actual size: " + std::to_string(pdu.size()) +
                       ", Expected: " + std::to_string(expected_pdu_size));
            return Result::PDU_LENGTH_MISMATCH;
        }

        if (byte_count % 2 != 0) {
            SHOW_ERROR("Modbus",
                       "Odd byte count (" + std::to_string(byte_count) +
                       "). Register count must be whole number (each "
                       "register = 2 bytes).");
            return Result::ODD_BYTE_COUNT;
        }

        const size_t register_count = byte_count / 2;
        modbus_registers.resize(register_count);

        for (size_t i = 0; i < register_count; ++i) {
            modbus_registers[i] = (static_cast<uint16_t>(pdu[3 + i * 2]) << 8) |
                                  static_cast<uint16_t>(pdu[3 + i * 2 + 1]);
        }
    } // 3. Логика для ЗАПИСИ (0x06, 0x10)
    else if (func_code == FUNC_WRITE_SINGLE || func_code == FUNC_WRITE_MULTI) {
        // При записи Modbus TCP обычно возвращает подтверждение (копия части запроса)
        // Нам достаточно знать, что ошибки нет (мы здесь)
        return Result::OK;
    } else {
        return Result::UNSUPPORTED_FUNCTION;
    }

    return Result::OK;
}

} // namespace modbus
