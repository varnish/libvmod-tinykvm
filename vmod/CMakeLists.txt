cmake_minimum_required (VERSION 3.0.2)
project (all_vmods C)

if (NOT VARNISH_PLUS)
	add_subdirectory(bodyaccess)
endif()
add_subdirectory(cookie)
add_subdirectory(example)
add_subdirectory(header)


enable_testing()
