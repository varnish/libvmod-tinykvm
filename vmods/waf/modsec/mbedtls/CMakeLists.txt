project (mbedtls C)

set(FOLDER "../ModSecurity/others/mbedtls")
set(SOURCES
	${FOLDER}/base64.c
	${FOLDER}/md5.c
	${FOLDER}/sha1.c
)

add_library(mbedtls SHARED ${SOURCES})
target_include_directories(mbedtls PRIVATE "../ModSecurity/others")
target_compile_definitions(mbedtls PRIVATE MBEDTLS_CONFIG_FILE="mbed-tls-config.h")
