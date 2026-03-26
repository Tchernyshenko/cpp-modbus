# cpp_modbus
Современная, модульная и потокобезопасная библиотека **Modbus TCP** на C++17.

Библиотека реализует клиентскую часть протокола Modbus TCP с четким разделением ответственности (SOLID):
* **Ядро протокола:** Кодирование/декодирование MBAP заголовков и PDU кадров.
* **Транспортный слой:** Абстрактный интерфейс ITransport, позволяющий легко менять способ передачи данных (TCP, RTU, UDP) или использовать моки для тестирования.
* **Безопасность:** Полная потокобезопасность внутри экземпляра клиента (std::mutex, std::atomic).

## **✨ Особенности**
* **Стандарт C++17:** Использование std::optional, std::string_view, умных указателей и std::scoped_lock. 
* **Модульность:** Транспорт вынесен в отдельный интерфейс. Легко добавить поддержку RTU (UART) или Mock-транспорт для юнит-тестов.
* **Детализированные ошибки:** Enum Result разделяет сетевые ошибки (таймауты, разрывы) и протокольные ошибки Modbus (exception codes).
* **Потокобезопасность:** Встроенная синхронизация запросов и ответов для безопасного использования в многопоточных приложениях.
* **Логирование:** Интеграция с внешней библиотекой логирования (через Git submodule).

## **📂 Структура проекта**
```
cpp_modbus/
├── include/modbus/          # Публичные заголовки
│   ├── modbus_service.hpp   # Основной класс клиента
│   ├── result.hpp           # Коды ошибок
│   └── modbus_codec.hpp     # Утилиты кодирования
├── include/transport/       # Интерфейсы транспорта
│   ├── transport.hpp        # ITransport, Config
│   └── tcp/
│       └── tcp_transport.hpp
├── src/                     # Реализация
├── third_party/             # Зависимости (git submodules)
│   └── logger/              # Библиотека логирования
├── examples/                # Примеры использования
└── CMakeLists.txt           # Сборка через CMake
```

## **🛠 Требования**
* Компилятор с поддержкой C++17 (GCC 7+, Clang 5+, MSVC 2017+)
* CMake 3.18+
* Linux (POSIX сокеты) <-- ⚠️ Примечание: Для работы под Windows потребуется адаптация файла src/transport/tcp/tcp_transport.cpp (замена API сокетов).

## **📦 Установка и сборка**
Библиотека использует Git Submodules для управления зависимостями (логгер).

### 1. Клонирование репозитория

```bash
git clone --recursive git@github.com:Tchernyshenko/cpp-modbus.git
cd cpp_modbus
```
**Важно: Флаг --recursive обязателен для загрузки подмодуля с логгером. Если вы уже склонировали без него, выполните:**
```bash
git submodule update --init --recursive
```

### 2. Сборка через CMake

```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON
make
```
_Это соберет статическую библиотеку libcpp_modbus.a и пример исполняемого файла modbus_example._

## **🚀 Быстрый старт**

### Подключение в ваш проект (CMake)

Если вы используете add_subdirectory:
```bash
# В вашем CMakeLists.txt
add_subdirectory(path/to/cpp_modbus)

target_link_libraries(your_target PRIVATE cpp_modbus)
```
_Убедитесь, что в вашем проекте также инициализированы подмодули, чтобы найти зависимость cpp_logger._

### **Пример кода (C++)**
```
#include <iostream>
#include <vector>
#include <memory>

// Заголовки библиотеки
#include "modbus/modbus_service.hpp"
#include "modbus/transport/tcp/tcp_transport.hpp"
#include "modbus/result.hpp"

// Заголовки логгера (нужны, если вы хотите видеть логи подключения)
#include "logger/logger.hpp" 

int main() {

    // 1. Настройка транспорта
    transport::Config config;
    config.ip = "192.168.1.50";      // IP адрес устройства
    config.port = 502;               // Порт Modbus
    config.timeout_ms = 1000;        // Таймаут операции
    config.retries = 3;              // Количество попыток повтора

    // 2. Создание транспорта и сервиса
    auto transport = std::make_unique<transport::TcpTransport>(config);
    modbus::ModbusService client(std::move(transport));

    // 3. Чтение регистров (Holding Registers, функция 03)
    // Читаем 10 регистров начиная с адреса 0 от устройства с ID 1
    std::vector<uint16_t> registers;
    uint8_t slave_id = 1;
    
    const auto result = client.readHoldingRegisters(slave_id, 0, 10, registers);

    if (result == modbus::Result::OK) {
        std::cout << "Success! Read " << registers.size() << " registers:\n";
        for (size_t i = 0; i < registers.size(); ++i) {
            std::cout << "Reg[" << i << "] = " << registers[i] << "\n";
        }
    } else {
        std::cerr << "Error occurred: " << modbus::toString(result) << std::endl;
        
        // Обработка специфических ошибок
        if (result == modbus::Result::ILLEGAL_DATA_ADDRESS) {
            std::cerr << "Неверный адрес регистра на устройстве." << std::endl;
        } else if (result == modbus::Result::CONNECT_TIMEOUT) {
            std::cerr << "Устройство не отвечает по сети." << std::endl;
        }
    }

    return 0;
}
```

## **🔌 Расширение: Добавление своего транспорта**
Благодаря интерфейсу transport::ITransport, вы можете реализовать собственный транспорт (например, для последовательного порта RS-485/RTU), не трогая логику Modbus.
```
#include "transport/transport.hpp"

namespace transport {
class RtuSerialTransport : public ITransport {
public:
    explicit RtuSerialTransport(Config config) : config_(std::move(config)) {}
    
    Result connect() override { 
        // Открытие COM-порта (termios)
        return Result::OK; 
    }
    
    void disconnect() override { 
        // Закрытие порта
    }
    
    Result send(const std::vector<uint8_t>& data) override { 
        // Запись в порт
        return Result::OK; 
    }
    
    Result receive(std::vector<uint8_t>& out, size_t size) override { 
        // Чтение из порта с таймаутом
        return Result::OK; 
    }
    
    const Config& config() const override { return config_; }

private:
    Config config_;
    int fd_ = -1;
};
} // namespace transport
```
Затем просто передайте его в конструктор:
```
auto rtu = std::make_unique<transport::RtuSerialTransport>(config);
modbus::ModbusService client(std::move(rtu));
```
_Логика парсинга Modbus останется той же самой._

## **📄 Лицензия**
Распространяется под лицензией MIT. См. файл LICENSE.

## **🤝  Вклад в развитие**

* Форкните репозиторий.
* Создайте ветку для фичи (git checkout -b feature/NewTransport).
* Закоммитьте изменения.
* Отправьте Pull Request.