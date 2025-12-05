cmake_minimum_required(VERSION 3.12)

project(csl
    DESCRIPTION "A Config Schema Language Utility")

set(CMAKE_CXX_STANDARD 20)

option(BUILD_WASM "Build for WebAssembly using Emscripten" OFF)

if (WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if (BUILD_WASM)
        add_compile_definitions(STDIO_ONLY)
    else()
        add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)
    endif()
endif()

if (BUILD_WASM)
    include_directories(
        $<TARGET_PROPERTY:INTERFACE_INCLUDE_DIRECTORIES>
    )
    
    set(RUNTIME_METHODS_LIST
        FS
        callMain
    )
    list(JOIN RUNTIME_METHODS_LIST "," RUNTIME_METHODS)
    
    set(EMSCRIPTEN_FLAGS
        "-s NO_DYNAMIC_EXECUTION=1"
        "-s MODULARIZE=1"
        "-s EXIT_RUNTIME=1"
        "-s FORCE_FILESYSTEM=1"
        "-s EXPORT_NAME=CslModule"
        "-s EXPORTED_RUNTIME_METHODS=${RUNTIME_METHODS}"
        "-lnodefs.js"
    )
    set(EMSCRIPTEN_LEGACY_FLAGS
        ${EMSCRIPTEN_FLAGS}
        "-s WASM=0"
    )
    set(EMSCRIPTEN_WASM_BUNDLE_FLAGS
        ${EMSCRIPTEN_FLAGS}
        "-s SINGLE_FILE=1"
    )
    
    list(JOIN EMSCRIPTEN_FLAGS             " " EMSCRIPTEN_FLAGS_STR)
    list(JOIN EMSCRIPTEN_LEGACY_FLAGS      " " EMSCRIPTEN_LEGACY_FLAGS_STR)
    list(JOIN EMSCRIPTEN_WASM_BUNDLE_FLAGS " " EMSCRIPTEN_WASM_BUNDLE_FLAGS_STR)
endif()

set(CSL_SOURCES
    shared/Shared.cpp
    lexer/CslLexer.cpp
    parser/CslParser.cpp
    langsvr/CslLangSvr.cpp
    driver/Csl.cpp
    docgen/HtmlDocGen.cpp
)

if (BUILD_WASM)
    add_executable(CslWasm ${CSL_SOURCES})
    set_target_properties(
        CslWasm
        PROPERTIES LINK_FLAGS ${EMSCRIPTEN_FLAGS_STR}
    )

    add_executable(CslWasmBundle ${CSL_SOURCES})
    set_target_properties(
        CslWasmBundle
        PROPERTIES LINK_FLAGS ${EMSCRIPTEN_WASM_BUNDLE_FLAGS_STR}
    )

    add_executable(CslLegacy ${CSL_SOURCES})
    set_target_properties(
        CslLegacy
        PROPERTIES LINK_FLAGS ${EMSCRIPTEN_LEGACY_FLAGS_STR}
    )
else()
    add_executable(csl ${CSL_SOURCES})
    add_executable(csl-test
        shared/Shared.cpp
        lexer/CslLexer.cpp
        parser/CslParser.cpp
        driver/Test.cpp
    )
endif()
