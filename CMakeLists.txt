cmake_minimum_required(VERSION 3.26)

# Project name and language
project(DynResAMTvsDPP LANGUAGES C CXX)

# Set compiler
set(CMAKE_CXX_COMPILER "mpic++")

# Optional: compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto")

# Include directories (e.g., for DMR headers)
include_directories(
        $ENV{HOME}/docker-cluster/install/ompi/include
        $ENV{HOME}/docker-cluster/install/pmix/include
        $ENV{HOME}/docker-cluster/install/prrte/include
        $ENV{HOME}/docker-cluster/install/dmr/include
)

# Source files
file(GLOB_RECURSE SRC_FILES
        ${PROJECT_SOURCE_DIR}/src/*.cpp
        ${PROJECT_SOURCE_DIR}/src/*.c
)

# Executable
add_executable(DynResAMTvsDPP ${SRC_FILES})

# Link additional libraries if needed
target_link_directories(DynResAMTvsDPP PRIVATE
        $ENV{HOME}/docker-cluster/install/ompi/lib
        $ENV{HOME}/docker-cluster/install/pmix/lib
        $ENV{HOME}/docker-cluster/install/prrte/lib
        $ENV{HOME}/docker-cluster/install/dmr/lib
)

target_link_libraries(DynResAMTvsDPP
        ssl crypto           # standard system libs
        mpi pmix prrte dmr   # these will be searched in the above lib paths
)
