add_executable(MetalSmokeTest main.cpp)

target_link_libraries(MetalSmokeTest PRIVATE PoseidonMTL)

set_target_properties(MetalSmokeTest PROPERTIES
    OUTPUT_NAME "MetalSmokeTest"
)
