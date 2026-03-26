#include "modbus/modbus_service.hpp"

#include <thread>

#include "logger/logger.hpp"
#include "modbus/modbus_codec.hpp"

namespace modbus {

constexpr auto MBAP_SIZE = 6;

ModbusService::ModbusService(std::unique_ptr<transport::ITransport> transport) :
    transport_(std::move(transport)) {
    {
        std::scoped_lock lock(mutex_);
        transaction_id_ = INIT_TRANS_ID;
    }
}

Result ModbusService::readHoldingRegisters(const uint8_t slave_id, const uint16_t reg_addr,
                                           const uint16_t reg_count,
                                           std::vector<uint16_t>& response) {
    std::scoped_lock lock(mutex_);
    return executeRequest(
        slave_id,
        [reg_addr, reg_count] {
            return ModbusCodec::encodeReadHoldingRegisters(reg_addr, reg_count);
        },
        &response);
}

Result ModbusService::readInputRegisters(const uint8_t slave_id, const uint16_t reg_addr,
                                         const uint16_t reg_count,
                                         std::vector<uint16_t>& response) {
    std::scoped_lock lock(mutex_);
    return executeRequest(
        slave_id,
        [reg_addr, reg_count] {
            return ModbusCodec::encodeReadInputRegisters(reg_addr, reg_count);
        },
        &response);
}

Result ModbusService::writeSingleRegister(const uint8_t slave_id, const uint16_t reg_addr,
                                          const uint16_t value) {
    std::scoped_lock lock(mutex_);
    return executeRequest(
        slave_id,
        [reg_addr, value] { return ModbusCodec::encodeWriteSingleRegister(reg_addr, value); },
        nullptr);
}

Result ModbusService::writeMultipleRegisters(const uint8_t slave_id, const uint16_t reg_addr,
                                             const std::vector<uint16_t>& values) {
    std::scoped_lock lock(mutex_);
    return executeRequest(
        slave_id,
        [reg_addr, values] { return ModbusCodec::encodeWriteMultipleRegisters(reg_addr, values); },
        nullptr);
}

template<typename EncodeFunc>
Result ModbusService::executeRequest(uint8_t slave_id, EncodeFunc encode_pdu,
                                     std::vector<uint16_t>* modbus_registers) {
    const auto pdu = encode_pdu();
    const uint16_t trans_id = transaction_id_.fetch_add(1, std::memory_order_relaxed);

    for (int attempt = 0; attempt < transport_->config().retries; ++attempt) {
        if (attempt > 0) {
            SHOW_WARN("Modbus", "Retry " + std::to_string(attempt) + " for transaction " +
                                    std::to_string(trans_id));
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
        }

        {
            // 1. Формируем и отправляем запрос
            auto frame = ModbusCodec::encodeModbusHeader(
                trans_id, static_cast<uint16_t>(pdu.size()), slave_id);
            frame.insert(frame.end(), pdu.begin(), pdu.end());

            {
                std::scoped_lock lock(transport_mutex_);
                if (const auto send_result = transport_->send(frame);
                    send_result != transport::Result::OK) {
                    SHOW_WARN("TCP", "Send failed (attempt " + std::to_string(attempt + 1) +
                                         "): " + toString(send_result));
                    continue; // Повторяем отправку
                }
            }

            // 2. Читаем ответ
            std::vector<uint8_t> mbap(MBAP_SIZE);

            {
                std::scoped_lock lock(transport_mutex_);
                if (const auto recv_result = transport_->receive(mbap, MBAP_SIZE);
                    recv_result != transport::Result::OK) {
                    SHOW_WARN("Modbus", "Receive failed (attempt " + std::to_string(attempt + 1) +
                                            "): " + toString(recv_result));
                    continue; // Повторяем всю операцию
                }
            }

            if (const auto parse_result = ModbusCodec::parseMbapHeader(trans_id, mbap);
                parse_result != Result::OK) {
                SHOW_ERROR("Modbus", "Parse MBAP failed: " + toString(parse_result));
                return parse_result;
            }

            const uint16_t length = (static_cast<uint16_t>(mbap[4]) << 8) | mbap[5];

            std::vector<uint8_t> pdu(length);
            {
                std::scoped_lock lock(transport_mutex_);
                if (const auto recv_result = transport_->receive(pdu, length);
                    recv_result != transport::Result::OK) {
                    SHOW_WARN("TCP", "Receive failed (attempt " + std::to_string(attempt + 1) +
                                         "): " + toString(recv_result));
                    continue; // Повторяем всю операцию
                }
            }

            // 3. Парсим ответ
            if (modbus_registers) {
                if (const auto parse_result =
                        ModbusCodec::parsePdu(slave_id, pdu, *modbus_registers);
                    parse_result != Result::OK) {
                    SHOW_ERROR("Modbus", "Parse PDU failed: " + toString(parse_result));
                    return parse_result; // Ошибки парсинга не повторяются
                }
            }

            return Result::OK;
        }
    }

    SHOW_ERROR("Modbus", "All retries exhausted for transaction " + std::to_string(trans_id));
    return Result::FATAL_ERROR;
}

} // namespace modbus
