#
# varnish tests using CTest
#

set(TESTS_PATH  ${VARNISH_PATH}/bin/varnishtest/tests)
set(VARNISHD    ${CMAKE_BINARY_DIR}/varnishd)
set(VARNISHTEST ${CMAKE_BINARY_DIR}/varnishtest)

# can be overridden with -DTESTS=xxx.vtc
if (NOT TESTS)
	file(GLOB TESTS ${TESTS_PATH}/*.vtc)
	set(FAILING_TESTS
		# these tests will inexplicably fail
		#tests_a00001
	)
endif()

# we can only run tests when fork is enabled, for now
# TODO: this is, of course, fixable
if (NOT SINGLE_PROCESS)
	enable_testing()
	foreach (TEST ${TESTS})
		set(VMOD_PATH "${CMAKE_BINARY_DIR}")
		get_filename_component(TNAME ${TEST} NAME_WE)
		add_test(
			NAME tests_${TNAME}
			COMMAND ${VARNISHTEST} -l "-pvmod_path=${VMOD_PATH}" ${TEST}
			WORKING_DIRECTORY ${TESTS_PATH}
		)
		list(APPEND LIST_OF_TESTS tests_${TNAME})
	endforeach()
	set_tests_properties(${LIST_OF_TESTS}
		PROPERTIES  ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}:$ENV{PATH}"
					TIMEOUT 30
					SKIP_RETURN_CODE 77
	)
	set_tests_properties(${FAILING_TESTS}
		PROPERTIES WILL_FAIL TRUE
	)
endif()
