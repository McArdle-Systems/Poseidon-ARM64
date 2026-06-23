set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Native build - prevent CMake from treating this as cross-compilation
set(CMAKE_CROSSCOMPILING FALSE)

# Apple Silicon only for now — Intel Macs are out of scope (the whole reason
# this backend exists is that Apple Silicon has zero OpenGL support).
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
