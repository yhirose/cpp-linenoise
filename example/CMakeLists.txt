add_executable(example example.cpp)
target_link_libraries(example PRIVATE linenoise)

# Verify that including the header from multiple translation units links
# cleanly (the library state must be shared, not duplicated).
add_executable(multiinclude
    multiinclude/main.cpp
    multiinclude/f1.cpp
    multiinclude/f2.cpp
)
target_link_libraries(multiinclude PRIVATE linenoise)
