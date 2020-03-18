cmake_minimum_required (VERSION 3.0.2)
project (all_vmods C)

if (NOT VARNISH_PLUS)
	add_subdirectory(bodyaccess)
endif()
add_subdirectory(accept)
add_subdirectory(cookie)
add_subdirectory(example)
add_subdirectory(header)
add_subdirectory(jwt)
add_subdirectory(std)
add_subdirectory(urlplus)
add_subdirectory(waf)


enable_testing()
