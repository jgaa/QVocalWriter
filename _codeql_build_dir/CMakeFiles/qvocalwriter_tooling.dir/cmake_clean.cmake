file(REMOVE_RECURSE
  "QVocalWriter/qml/Main.qml"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/qvocalwriter_tooling.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
