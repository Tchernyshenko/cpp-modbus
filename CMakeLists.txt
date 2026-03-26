cmake_minimum_required(VERSION 3.18)
project(cpp_modbus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Настройка внешних библиотек
add_subdirectory(third_party/logger)

# Создаем целевой проект
add_library(cpp_modbus STATIC
        src/modbus/modbus_service.cpp
        src/modbus/modbus_codec.cpp
        src/transport/tcp/tcp_transport.cpp
)

# Подключаем заголовки
target_include_directories(cpp_modbus PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Линкуем библиотеки с проектом
target_link_libraries(cpp_modbus PRIVATE cpp_logger)

if(BUILD_EXAMPLES)
    add_executable(modbus_example examples/basic_usage.cpp)
    target_link_libraries(modbus_example PRIVATE cpp_modbus)
endif()


