#include "modbus/modbus_codec.hpp"

#include <iomanip>
#include <iostream>

#include "logger/logger.hpp"

namespace modbus::codec {

// Константы Modbus TCP — фиксированные значения протокола
constexpr auto PROTOCOL_ID = static_cast<uint8_t>(0x00); // ID протокола (всегда 0 для Modbus)
constexpr auto FUNC_READ_HOLDING =
    static_cast<uint8_t>(0x03); // Код функции: чтение регистров хранения
constexpr auto FUNC_READ_INPUT =
    static_cast<uint8_t>(0x04); // Код функции: чтение входных регистров
constexpr auto FUNC_WRITE_SINGLE =
    static_cast<uint8_t>(0x06); // Код функции: запись одного регистра
constexpr auto FUNC_WRITE_MULTI =
    static_cast<uint8_t>(0x10); // Код функции: запись нескольких регистров

// Вспомогательная функция для форматирования байта в шестнадцатеричный вид
std::string toHex(const uint8_t value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << value;
    return ss.str();
}

// Кодирует заголовок MBAP (Modbus Application Protocol) для Modbus TCP
std::vector<uint8_t> Codec::encodeModbusHeader(const uint16_t transaction_id,
                                                     const uint16_t pdu_size,
                                                     const uint8_t slave_id) {
    const uint16_t length = 1 + pdu_size; // 1 byte slave_id + pdu_size

    // Формируем заголовок MBAP (6 байт) + slave_id (1 байт)
    std::vector header = {
        static_cast<uint8_t>(transaction_id >> 8 & 0xFF), // Старший байт ID транзакции
        static_cast<uint8_t>(transaction_id & 0xFF), // Младший байт ID транзакции
        PROTOCOL_ID,                                 // ID протокола (0x00)
        PROTOCOL_ID, // Зарезервированное поле (0x00)
        static_cast<uint8_t>(length >> 8 & 0xFF), // Старший байт длины пакета
        static_cast<uint8_t>(length & 0xFF),      // Младший байт длины пакета
        slave_id // ID подчинённого устройства (slave)
    };

    return header;
}

// Кодирует PDU для чтения регистров хранения (функция 0x03)
std::vector<uint8_t> Codec::encodeReadHoldingRegisters(const uint16_t reg_addr,
                                                             const uint16_t reg_count) {
    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_READ_HOLDING); // Добавляем код функции
    appendUint16(pdu, reg_addr); // Добавляем адрес регистра (2 байта, big‑endian)
    appendUint16(pdu, reg_count); // Добавляем количество регистров (2 байта)
    return pdu;
}

// Кодирует PDU для чтения входных регистров (функция 0x04)
std::vector<uint8_t> Codec::encodeReadInputRegisters(const uint16_t reg_addr,
                                                           const uint16_t reg_count) {
    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_READ_INPUT); // Добавляем код функции
    appendUint16(pdu, reg_addr);    // Добавляем адрес регистра
    appendUint16(pdu, reg_count);   // Добавляем количество регистров
    return pdu;
}

// Кодирует PDU для записи одного регистра (функция 0x06)
std::vector<uint8_t> Codec::encodeWriteSingleRegister(const uint16_t reg_addr,
                                                            const uint16_t value) {
    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_WRITE_SINGLE); // Добавляем код функции
    appendUint16(pdu, reg_addr);      // Добавляем адрес регистра
    appendUint16(pdu, value); // Добавляем записываемое значение
    return pdu;
}

// Кодирует PDU для записи нескольких регистров (функция 0x10)
std::vector<uint8_t>
Codec::encodeWriteMultipleRegisters(const uint16_t reg_addr,
                                          const std::vector<uint16_t>& values) {

    // Проверка корректности количества регистров (Modbus ограничивает до 123 регистров за раз)
    if (values.empty() || values.size() > 123) {
        SHOW_WARN("Modbus:Codec",
                  "Invalid register count for write multiple: " + std::to_string(values.size()));
        return {};
    }

    std::vector<uint8_t> pdu;
    pdu.push_back(FUNC_WRITE_MULTI); // Добавляем код функции
    appendUint16(pdu, reg_addr);     // Добавляем начальный адрес
    appendUint16(pdu, static_cast<uint16_t>(values.size())); // Количество регистров
    pdu.push_back(static_cast<uint8_t>(values.size() * 2)); // Количество байт данных

    // Добавляем значения регистров (каждое — 2 байта, big‑endian)
    for (const uint16_t val : values) {
        appendUint16(pdu, val);
    }
    return pdu;
}

Result Codec::parseMbapHeader(const uint16_t target_transaction_id,
                                    const uint8_t target_slave_id,
                                    const std::vector<uint8_t>& raw_mbap) {
    // Проверяем, что заголовок имеет достаточную длину (6 байт)
    if (raw_mbap.size() < MBAP_SIZE) {
        return Result::MBAP_TRUNCATED;
    }

    // Извлекаем поля из заголовка MBAP
    const uint16_t current_transaction_id = (static_cast<uint16_t>(raw_mbap[0]) << 8) | raw_mbap[1];
    const uint16_t proto_id = (static_cast<uint16_t>(raw_mbap[2]) << 8) | raw_mbap[3];
    const uint16_t length = ((static_cast<uint16_t>(raw_mbap[4]) << 8) | raw_mbap[5]) - 1;
    const uint16_t current_slave_id = raw_mbap[6];

    // Валидация полей заголовка
    if (current_transaction_id != target_transaction_id) {
        // Несовпадение ID транзакции — признак потери синхронизации
        SHOW_ERROR("Modbus:Codec",
                   "Transaction ID mismatch. Expected: " + std::to_string(target_transaction_id) +
                       ", Got: " + std::to_string(current_transaction_id));
        return Result::TRANSACTION_MISMATCH;
    }
    if (proto_id != 0) {
        // Некорректный ID протокола
        SHOW_ERROR("Modbus:Codec", "Protocol invalid. Got: " + std::to_string(proto_id));
        return Result::INVALID_PROTOCOL;
    }
    if (length < 2 || length > 253) {
        // Длина пакета выходит за допустимые пределы Modbus TCP
        SHOW_ERROR("Modbus:Codec", "Length invalid. Got: " + std::to_string(length));
        return Result::INVALID_PDU_LENGTH;
    }

    if (current_slave_id != target_slave_id) {
        SHOW_ERROR("Modbus:Codec", "Slave ID mismatch. Expected: " + toHex(current_slave_id) +
                                 ", Got: " + toHex(target_slave_id));
        return Result::SLAVE_MISMATCH;
    }

    return Result::OK;
}

Result Codec::parsePdu(const std::vector<uint8_t>& pdu,
                             std::vector<uint16_t>& modbus_registers) {
    if (pdu.size() < 2) {
        SHOW_ERROR("Modbus:Codec", "PDU too short (" + std::to_string(pdu.size()) + " bytes). " +
                                 "Expected at least 2(func_code + byte_count).");
        return Result::PDU_TRUNCATED;
    }

    const uint8_t func_code = pdu[0];
    if ((func_code & 0x80) != 0) {
        const uint8_t exception_code = pdu[1];
        SHOW_ERROR("Modbus:Codec", "Exception response. Function: " + toHex(func_code & 0x7F) +
                                 ", Exception Code: " + toHex(exception_code));
        return static_cast<Result>(exception_code);
    }

    // 2. Логика для ЧТЕНИЯ (0x03, 0x04)
    if (func_code == FUNC_READ_HOLDING || func_code == FUNC_READ_INPUT) {
        const uint8_t byte_count = pdu[1];
        const size_t expected_pdu_size = 2 + byte_count;

        if (pdu.size() != expected_pdu_size) {
            SHOW_ERROR("Modbus:Codec", "PDU size mismatch. Actual size: " + std::to_string(pdu.size()) +
                                     ", " + "Expected: " + std::to_string(expected_pdu_size));
            return Result::PDU_LENGTH_MISMATCH;
        }

        if (byte_count % 2 != 0) {
            SHOW_ERROR("Modbus:Codec",
                       "Odd byte count (" + std::to_string(byte_count) + ")." +
                           "Register count must be whole number (each register = 2 bytes).");
            return Result::ODD_BYTE_COUNT;
        }

        const size_t register_count = byte_count / 2;
        modbus_registers.resize(register_count);

        for (size_t i = 0; i < register_count; ++i) {
            modbus_registers[i] = (static_cast<uint16_t>(pdu[2 + i * 2]) << 8) |
                                  static_cast<uint16_t>(pdu[2 + i * 2 + 1]);
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
