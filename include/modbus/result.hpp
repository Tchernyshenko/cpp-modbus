#pragma once

#include <string>

namespace modbus {

enum class Result {
    OK = 0,
    // Протокольные ошибки Modbus
    TRANSACTION_MISMATCH,
    INVALID_PROTOCOL,
    MBAP_TRUNCATED,       // MBAP < 6 байт
    INVALID_PDU_LENGTH,   // длина PDU вне [2..253]
    PDU_TRUNCATED,        // PDU < 3 байт
    PDU_LENGTH_MISMATCH,  // byte_count ≠ actual data size
    ODD_BYTE_COUNT,       // нечётное количество байт в данных
    UNSUPPORTED_FUNCTION, // неизвестный function code
    SLAVE_MISMATCH,

    // Modbus Exception Codes (0x81–0x8B)
    ILLEGAL_FUNCTION = 0x81,         // 0x01 + 0x80
    ILLEGAL_DATA_ADDRESS = 0x82,     // 0x02 + 0x80
    ILLEGAL_DATA_VALUE = 0x83,       // 0x03 + 0x80
    SLAVE_DEVICE_FAILURE = 0x84,     // 0x04 + 0x80
    ACKNOWLEDGE = 0x85,              // 0x05 + 0x80
    SLAVE_DEVICE_BUSY = 0x86,        // 0x06 + 0x80
    MEMORY_PARITY_ERROR = 0x88,      // 0x08 + 0x80
    GATEWAY_PATH_UNAVAILABLE = 0x8A, // 0x0A + 0x80
    GATEWAY_TARGET_FAILED = 0x8B,    // 0x0B + 0x80

    FATAL_ERROR
};

inline std::string toString(Result r) {
    switch (r) {
        // Успех
        case Result::OK:
            return "OK";

        // Протокольные ошибки Modbus
        case Result::TRANSACTION_MISMATCH:
            return "TRANSACTION_MISMATCH";
        case Result::INVALID_PROTOCOL:
            return "INVALID_PROTOCOL";
        case Result::MBAP_TRUNCATED:
            return "MBAP_TRUNCATED";
        case Result::INVALID_PDU_LENGTH:
            return "INVALID_PDU_LENGTH";
        case Result::PDU_TRUNCATED:
            return "PDU_TRUNCATED";
        case Result::PDU_LENGTH_MISMATCH:
            return "PDU_LENGTH_MISMATCH";
        case Result::ODD_BYTE_COUNT:
            return "ODD_BYTE_COUNT";
        case Result::UNSUPPORTED_FUNCTION:
            return "UNSUPPORTED_FUNCTION";
        case Result::SLAVE_MISMATCH:
            return "SLAVE_MISMATCH";

        // Modbus Exception Codes
        case Result::ILLEGAL_FUNCTION:
            return "ILLEGAL_FUNCTION (0x81)";
        case Result::ILLEGAL_DATA_ADDRESS:
            return "ILLEGAL_DATA_ADDRESS (0x82)";
        case Result::ILLEGAL_DATA_VALUE:
            return "ILLEGAL_DATA_VALUE (0x83)";
        case Result::SLAVE_DEVICE_FAILURE:
            return "SLAVE_DEVICE_FAILURE (0x84)";
        case Result::ACKNOWLEDGE:
            return "ACKNOWLEDGE (0x85)";
        case Result::SLAVE_DEVICE_BUSY:
            return "SLAVE_DEVICE_BUSY (0x86)";
        case Result::MEMORY_PARITY_ERROR:
            return "MEMORY_PARITY_ERROR (0x88)";
        case Result::GATEWAY_PATH_UNAVAILABLE:
            return "GATEWAY_PATH_UNAVAILABLE (0x8A)";
        case Result::GATEWAY_TARGET_FAILED:
            return "GATEWAY_TARGET_FAILED (0x8B)";
        case Result::FATAL_ERROR:
            return "FATAL_ERROR (0x8C)";
        default:
            return "UNKNOWN_RESULT(" + std::to_string(static_cast<int>(r)) + ")";
    }
}

} // namespace modbus