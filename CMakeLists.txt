cmake_minimum_required(VERSION 3.10)
project(BitMEXHFTBot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(PYBIND11_FINDPYTHON ON)
if(DEFINED ENV{CONDA_PREFIX})
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} $ENV{CONDA_PREFIX})
endif()
find_package(pybind11 CONFIG REQUIRED)

   # Find WebSocket++
    execute_process(
     COMMAND brew --prefix websocketpp
     OUTPUT_VARIABLE WEBSOCKETPP_ROOT
     OUTPUT_STRIP_TRAILING_WHITESPACE
   )
   set(WEBSOCKETPP_INCLUDE_DIR "${WEBSOCKETPP_ROOT}/include")
   if(NOT EXISTS "${WEBSOCKETPP_INCLUDE_DIR}/websocketpp/client.hpp")
     message(FATAL_ERROR "WebSocket++ headers not found. Expected at ${WEBSOCKETPP_INCLUDE_DIR}")
   endif()
   include_directories(${WEBSOCKETPP_INCLUDE_DIR})

   # If you're using Boost with WebSocket++, you might also need:
   find_package(Boost REQUIRED COMPONENTS system)
   include_directories(${Boost_INCLUDE_DIRS})

include_directories(${CURL_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(BitMEXHFTBot
    main.cpp
    BitMEXHFTBot.cpp
    RateLimiter.cpp
    TradingRules.cpp
    Logger.cpp
    Config.cpp
    ConfigManager.cpp
)

target_link_libraries(BitMEXHFTBot
    ${CURL_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    pybind11::module
    ${Boost_LIBRARIES}
    Python::Python
)

pybind11_add_module(bitmex_hft_bot 
    python_bindings.cpp 
    BitMEXHFTBot.cpp 
    Config.cpp 
    ConfigManager.cpp
)