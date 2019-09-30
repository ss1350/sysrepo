include_directories("${PROJECT_SOURCE_DIR}/inc")
include_directories("${PROJECT_SOURCE_DIR}/swig/cpp/examples")

# turing-machine plugin
add_library(turing-machine SHARED turing-machine.c)
target_link_libraries(turing-machine sysrepo)

# oven plugin
add_library(oven SHARED oven.c)
target_link_libraries(oven sysrepo)

# dect-cuc plugin
add_library(dect-cuc SHARED dect-cuc.cpp)
target_link_libraries(dect-cuc sysrepo)

install(TARGETS dect-cuc DESTINATION ${PLUGINS_DIR})