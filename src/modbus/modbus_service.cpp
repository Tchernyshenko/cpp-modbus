#include "modbus/modbus_service.hpp"

#include <thread>

#include "logger/logger.hpp"
#include "modbus/modbus_codec.hpp"

namespace modbus::service {

Service::Service(std::unique_ptr<transport::IModbusTcpTransport> transport) :
    transport_(std::move(transport)) {
    {
        std::scoped_lock lock(mutex_);
        transaction_id_ = INIT_TRANS_ID;
    }
}

// Читает регистры хранения (Holding Registers) с устройства Modbus
Result Service::readHoldingRegisters(const uint8_t slave_id, const uint16_t reg_addr,
                                     const uint16_t reg_count, std::vector<uint16_t>& response) {
    return executeRequest(
        slave_id,
        [reg_addr, reg_count] {
            return codec::Codec::encodeReadHoldingRegisters(reg_addr, reg_count);
        },
        &response); // Передаём указатель на вектор для заполнения данными
}

// Читает входные регистры (Input Registers) с устройства Modbus
Result Service::readInputRegisters(const uint8_t slave_id, const uint16_t reg_addr,
                                   const uint16_t reg_count, std::vector<uint16_t>& response) {
    return executeRequest(
        slave_id,
        [reg_addr, reg_count] {
            return codec::Codec::encodeReadInputRegisters(reg_addr, reg_count);
        },
        &response); // Передаём указатель на вектор для заполнения данными
}

// Записывает одно значение в регистр устройства Modbus
Result Service::writeSingleRegister(const uint8_t slave_id, const uint16_t reg_addr,
                                    const uint16_t value) {
    // clang-format off

    return executeRequest(
        slave_id,
        [reg_addr, value] {
            return codec::Codec::encodeWriteSingleRegister(reg_addr, value);
        },
        nullptr); // Передаём указатель на вектор для заполнения данными

    // clang-format on
}

// Записывает несколько значений в регистры устройства Modbus
Result Service::writeMultipleRegisters(const uint8_t slave_id, const uint16_t reg_addr,
                                       const std::vector<uint16_t>& values) {
    // clang-format off

    return executeRequest(
        slave_id,
        [reg_addr, values] {
            return codec::Codec::encodeWriteMultipleRegisters(reg_addr, values);
        },
        nullptr); // Передаём указатель на вектор для заполнения данными

    // clang-format on
}

// Общий метод выполнения Modbus‑запроса с повторными попытками
template<typename EncodeFunc>
Result Service::executeRequest(const uint8_t current_slave_id, EncodeFunc encode_pdu,
                               std::vector<uint16_t>* modbus_registers) {
    const auto pdu = encode_pdu();

    // Выполняем до количества повторных попыток, заданного в конфигурации транспорта
    for (int attempt = 0; attempt < transport_->config().retries; ++attempt) {
        if (attempt > 0) {
            // Логируем повторную попытку
            SHOW_WARN("Modbus:Service", "Retry " + std::to_string(attempt) + " for Slave " +
                                            std::to_string(current_slave_id));
            std::this_thread::sleep_for(std::chrono::milliseconds(attempt * 100));
        }

        std::scoped_lock lock(transport_mutex_);

        // 1. Формирование и отправка Modbus‑фрейма

        // Получаем уникальный ID транзакции (атомарно увеличиваем счётчик)
        const uint16_t current_transaction_id =
            transaction_id_.fetch_add(1, std::memory_order_relaxed);
        // Кодируем заголовок MBAP (Modbus Application Protocol)
        auto frame = codec::Codec::encodeModbusHeader(
            current_transaction_id, static_cast<uint16_t>(pdu.size()), current_slave_id);
        // Добавляем PDU к заголовку
        frame.insert(frame.end(), pdu.begin(), pdu.end());

        // Отправляем фрейм через транспортный слой
        if (const auto res = transport_->send(frame); res != transport::Result::OK) {
            continue;
        }

        // 2. Чтение заголовка MBAP (6 байт)
        std::vector<uint8_t> mbap(codec::MBAP_SIZE);
        if (const auto res = transport_->receive(mbap, codec::MBAP_SIZE);
            res != transport::Result::OK) {
            continue;
        }

        // 3. Проверка заголовка и ID транзакции
        // clang-format off

        if (const auto res = codec::Codec::parseMbapHeader(current_transaction_id, current_slave_id, mbap); res != Result::OK) {
            transport_->disconnect();
            return res;
        }

        // clang-format on

        // 4. Вычисляем длину оставшегося PDU из поля длины в MBAP
        const uint16_t length = ((static_cast<uint16_t>(mbap[4]) << 8) | mbap[5]) - 1;
        std::vector<uint8_t> pdu(length);

        // Читаем остальную часть ответа (PDU)
        if (const auto res = transport_->receive(pdu, length); res != transport::Result::OK) {
            continue;
        }

        // 5. Парсим полученный PDU
        std::vector<uint16_t> temp; // Временный буфер для операций без ответа
        auto& modbus_regs_ref = modbus_registers ? *modbus_registers : temp;
        if (const auto res = codec::Codec::parsePdu(pdu, modbus_regs_ref); res != Result::OK) {
            return res;
        }

        return Result::OK;
    }
    // Если все попытки исчерпаны — логируем ошибку и возвращаем FATAL_ERROR
    SHOW_ERROR("Modbus:Service",
               "All retries exhausted for Slave " + std::to_string(current_slave_id));
    return Result::FATAL_ERROR;
}

} // namespace modbus
