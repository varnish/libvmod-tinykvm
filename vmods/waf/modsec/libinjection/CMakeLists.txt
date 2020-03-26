project (libinjection C)

set(FOLDER "../ModSecurity/others/libinjection")
set(SOURCES
	${FOLDER}/src/libinjection_html5.c
	${FOLDER}/src/libinjection_sqli.c
	${FOLDER}/src/libinjection_xss.c
)

add_library(injection SHARED ${SOURCES})
target_include_directories(injection PRIVATE "../ModSecurity/others")
