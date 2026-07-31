if(NOT DEFINED TEST_METADATA_FILE OR TEST_METADATA_FILE STREQUAL "")
    message(FATAL_ERROR "TEST_METADATA_FILE is required")
endif()

if(NOT DEFINED EXPECTED_TIMEOUT_SECONDS OR
   NOT EXPECTED_TIMEOUT_SECONDS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "EXPECTED_TIMEOUT_SECONDS must be a positive integer")
endif()

if(NOT EXISTS "${TEST_METADATA_FILE}")
    message(FATAL_ERROR "GoogleTest metadata does not exist: ${TEST_METADATA_FILE}")
endif()

file(STRINGS "${TEST_METADATA_FILE}" metadata_lines)

set(discovered_test_count 0)
set(timeout_property_count 0)
set(representative_timeout_found FALSE)
set(timeout_pattern "TIMEOUT[^0-9]*${EXPECTED_TIMEOUT_SECONDS}([^0-9]|$)")

foreach(line IN LISTS metadata_lines)
    if(line MATCHES "^add_test\\(")
        math(EXPR discovered_test_count "${discovered_test_count} + 1")
    elseif(line MATCHES "^set_tests_properties\\(")
        if(line MATCHES "${timeout_pattern}")
            math(EXPR timeout_property_count "${timeout_property_count} + 1")
        endif()

        if(line MATCHES "RedisServiceRuntimeTest\\.SetGetExistsDeleteRoundTrip")
            if(line MATCHES "${timeout_pattern}")
                set(representative_timeout_found TRUE)
            endif()
        endif()
    endif()
endforeach()

if(discovered_test_count EQUAL 0)
    message(FATAL_ERROR "GoogleTest metadata contains no discovered tests")
endif()

if(NOT timeout_property_count EQUAL discovered_test_count)
    message(FATAL_ERROR
        "Expected ${discovered_test_count} discovered tests with "
        "${EXPECTED_TIMEOUT_SECONDS}-second timeouts, found ${timeout_property_count}"
    )
endif()

if(NOT representative_timeout_found)
    message(FATAL_ERROR
        "RedisServiceRuntimeTest.SetGetExistsDeleteRoundTrip lacks the expected timeout"
    )
endif()

message(STATUS
    "Verified ${timeout_property_count} discovered tests with "
    "${EXPECTED_TIMEOUT_SECONDS}-second timeouts"
)
