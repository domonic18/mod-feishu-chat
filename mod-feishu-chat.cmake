# Add module include directories so source files can find vendored headers
target_include_directories(modules PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/libs"
)

# Enable HTTPS support in cpp-httplib; OpenSSL is already linked via the 'common' target
target_compile_definitions(modules PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
