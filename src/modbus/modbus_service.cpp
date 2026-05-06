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
    return executeRequest(
        slave_id,
        [reg_addr, reg_count] {
            return ModbusCodec::encodeReadInputRegisters(reg_addr, reg_count);
        },
        &response);
}

Result ModbusService::writeSingleRegister(const uint8_t slave_id, const uint16_t reg_addr,
                                          const uint16_t value) {
    return executeRequest(
        slave_id,
        [reg_addr, value] { return ModbusCodec::encodeWriteSingleRegister(reg_addr, value); },
        nullptr);
}

Result ModbusService::writeMultipleRegisters(const uint8_t slave_id, const uint16_t reg_addr,
                                             const std::vector<uint16_t>& values) {
    return executeRequest(
        slave_id,
        [reg_addr, values] { return ModbusCodec::encodeWriteMultipleRegisters(reg_addr, values); },
        nullptr);
}

template<typename EncodeFunc>
Result ModbusService::executeRequest(uint8_t slave_id, EncodeFunc encode_pdu,
                                     std::vector<uint16_t>* modbus_registers) {
    const auto pdu = encode_pdu();

    for (int attempt = 0; attempt < transport_->config().retries; ++attempt) {
        if (attempt > 0) {
            SHOW_WARN("Modbus", "Retry " + std::to_string(attempt) + " for Slave " +
                                    std::to_string(slave_id));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::scoped_lock lock(transport_mutex_);

        // 1. Отправка
        const uint16_t current_trans_id = transaction_id_.fetch_add(1, std::memory_order_relaxed);
        auto frame = ModbusCodec::encodeModbusHeader(current_trans_id,
                                                     static_cast<uint16_t>(pdu.size()), slave_id);
        frame.insert(frame.end(), pdu.begin(), pdu.end());

        auto send_res = transport_->send(frame);
        if (send_res != transport::Result::OK) {
            // KISS: Обязательно логируем причину, почему не отправили (например, "No route to
            // host")
            SHOW_ERROR("Modbus", "Send failed: " + transport::toString(send_res));
            continue;
        }

        // 2. Чтение MBAP
        std::vector<uint8_t> mbap(MBAP_SIZE);
        auto recv_res = transport_->receive(mbap, MBAP_SIZE);
        if (recv_res != transport::Result::OK) {
            SHOW_WARN("Modbus", "Header receive timeout/error: " + transport::toString(recv_res));
            continue;
        }

        // 3. Проверка заголовка и ID транзакции
        if (const auto parse_result = ModbusCodec::parseMbapHeader(current_trans_id, mbap);
            parse_result != Result::OK) {

            SHOW_ERROR("Modbus", "Sync lost (ID mismatch). Disconnecting...");
            // При несовпадении ID сбрасываем TCP соединение!
            transport_->disconnect();
            return parse_result;
        }

        // 4. Вычисляем длину оставшегося PDU
        const uint16_t length = (static_cast<uint16_t>(mbap[4]) << 8) | mbap[5];
        std::vector<uint8_t> pdu(length);

        if (const auto recv_result = transport_->receive(pdu, length);
            recv_result != transport::Result::OK) {
            SHOW_WARN("TCP", "Receive failed (attempt " + std::to_string(attempt + 1) +
                                 "): " + toString(recv_result));
            continue; // Повторяем всю операцию
        }

        // 5. Парсим ответ
        std::vector<uint16_t> dummy_regs;
        auto& regs_ref = modbus_registers ? *modbus_registers : dummy_regs;
        if (const auto parse_result = ModbusCodec::parsePdu(slave_id, pdu, regs_ref);
            parse_result != Result::OK) {
            return parse_result;
        }

        return Result::OK;
    }
    SHOW_ERROR("Modbus", "All retries exhausted for Slave " + std::to_string(slave_id));
    return Result::FATAL_ERROR;
}

} // namespace modbus
