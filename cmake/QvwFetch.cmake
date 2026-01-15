# cmake/QvwFetch.cmake
include(FetchContent)

function(qvw_fetch name)
  set(options)
  set(oneValueArgs SOURCE_DIR GIT_REPOSITORY GIT_TAG)
  set(multiValueArgs)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Cache var name: QVW_<NAME>_SOURCE_DIR (uppercased)
  string(TOUPPER "${name}" NAME_UP)
  string(REPLACE "-" "_" NAME_UP "${NAME_UP}")
  string(REPLACE "." "_" NAME_UP "${NAME_UP}")
  set(OVERRIDE_VAR "QVW_${NAME_UP}_SOURCE_DIR")

  set(${OVERRIDE_VAR} "" CACHE PATH "Use a local source dir for ${name} (packaging/offline builds)")

  if(${OVERRIDE_VAR})
    message(STATUS "QVW: Using local ${name} from ${${OVERRIDE_VAR}}")
    FetchContent_Declare(${name} SOURCE_DIR "${${OVERRIDE_VAR}}")
  else()
    FetchContent_Declare(${name}
      GIT_REPOSITORY "${ARG_GIT_REPOSITORY}"
      GIT_TAG        "${ARG_GIT_TAG}"
    )
  endif()

  FetchContent_MakeAvailable(${name})
endfunction()
