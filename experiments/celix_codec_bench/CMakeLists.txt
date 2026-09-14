cmake_minimum_required(VERSION 3.20)
project(talkpipe_codec_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Celix REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(MSGPACK_INCLUDE_DIR msgpack.hpp REQUIRED)

add_library(talkpipe_codec_bench_codec STATIC
    src/Codecs.cc
)
set_target_properties(talkpipe_codec_bench_codec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(talkpipe_codec_bench_codec
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MSGPACK_INCLUDE_DIR}
)
target_link_libraries(talkpipe_codec_bench_codec
    PUBLIC
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
)

target_compile_options(talkpipe_codec_bench_codec PRIVATE -Wall -Wextra -Wpedantic)

add_celix_bundle(TalkPipeCodecBridgeBundle
    VERSION 0.1.0
    SOURCES src/BridgeBundle.cc
)
target_link_libraries(TalkPipeCodecBridgeBundle PRIVATE talkpipe_codec_bench_codec)

add_celix_bundle(TalkPipeCodecBenchBundle
    VERSION 0.1.0
    SOURCES src/BenchBundle.cc
)
target_link_libraries(TalkPipeCodecBenchBundle PRIVATE talkpipe_codec_bench_codec)

add_celix_container(TalkPipeCodecBenchRuntime
    CXX
    BUNDLES
        TalkPipeCodecBridgeBundle
        TalkPipeCodecBenchBundle
)
