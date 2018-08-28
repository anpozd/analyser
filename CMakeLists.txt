
cmake_minimum_required(VERSION 2.6)
project(analyser)

find_package(Boost 1.60.0 REQUIRED COMPONENTS system filesystem program_options)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  link_directories(${Boost_LIBRARY_DIRS})
endif(Boost_FOUND)

set(USED_LIBS ${Boost_SYSTEM_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY})

add_executable(analyser analyser.cpp)
target_link_libraries(analyser ${USED_LIBS})
