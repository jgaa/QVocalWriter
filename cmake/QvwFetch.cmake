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

  # Prevent third-party install() rules from polluting the top-level
  # install tree. We only want QVocalWriter artifacts installed.
  set(_qvw_prev_skip_install_rules "${CMAKE_SKIP_INSTALL_RULES}")
  set(CMAKE_SKIP_INSTALL_RULES ON)
  FetchContent_MakeAvailable(${name})
  if(DEFINED _qvw_prev_skip_install_rules)
    set(CMAKE_SKIP_INSTALL_RULES "${_qvw_prev_skip_install_rules}")
  else()
    unset(CMAKE_SKIP_INSTALL_RULES)
  endif()

  # Top-level cmake_install.cmake still includes subdirectory install scripts.
  # When install rules are skipped for dependencies, ensure an empty script
  # exists so include() succeeds and installs nothing from that dependency.
  set(_qvw_dep_bin_dir "${${name}_BINARY_DIR}")
  if(_qvw_dep_bin_dir)
    set(_qvw_dep_install_script "${_qvw_dep_bin_dir}/cmake_install.cmake")
    if(NOT EXISTS "${_qvw_dep_install_script}")
      file(MAKE_DIRECTORY "${_qvw_dep_bin_dir}")
      file(WRITE "${_qvw_dep_install_script}" "# intentionally empty\n")
    endif()
  endif()
endfunction()
