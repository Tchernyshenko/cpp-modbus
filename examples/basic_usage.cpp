#include "modbus/modbus_service.hpp"
#include "transport/tcp/tcp_transport.hpp"
#include <iostream>

int main() {
    // Настройка транспорта
    transport::Config config;
    config.ip = "192.168.1.100";
    config.port = 502;
    config.timeout_ms = 1000;

    // Создание транспорта и сервиса
    auto transport = std::make_unique<transport::TcpTransport>(config);
    modbus::ModbusService client(std::move(transport));

    // Чтение регистров
    std::vector<uint16_t> registers;
    auto result = client.readHoldingRegisters(1, 0, 10, registers);

    if (result == modbus::Result::OK) {
        std::cout << "Success! Read " << registers.size() << " registers." << std::endl;
    } else {
        std::cerr << "Error: " << modbus::toString(result) << std::endl;
    }

    return 0;
}