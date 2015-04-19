cmake_minimum_required(VERSION 2.8)
project(oscprompt)

find_package(PkgConfig REQUIRED)

pkg_search_module(LIBLO liblo>=0.25 REQUIRED)
mark_as_advanced(LIBLO_LIBRARIES)

pkg_search_module(LIBRTOSC librtosc>=0.1.0 REQUIRED)

pkg_search_module(JACK jack)

find_package(Curses REQUIRED)
mark_as_advanced(CURSES_CURSES_H_PATH)
mark_as_advanced(CURSES_FORM_LIBRARY)
mark_as_advanced(CURSES_HAVE_CURSES_H)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -Wall -Wextra")
set(BUILD_EXAMPLE FALSE CACHE BOOL "Build Example Application")


#Main executable
add_executable(oscprompt main.cpp render.cpp)
target_link_libraries(oscprompt ${LIBLO_LIBRARIES}
                                ${CURSES_LIBRARIES}
                                ${LIBRTOSC_LIBRARIES}
                                rtosc-cpp
                                m)

#Demo App
if(BUILD_EXAMPLE AND JACK_FOUND)
    add_executable(example-backend example.cpp)
    target_link_libraries(example-backend ${LIBLO_LIBRARIES}
                                          ${LIBRTOSC_LIBRARIES}
                                          rtosc-cpp
                                          m
                                          ${JACK_LIBRARIES})
endif(BUILD_EXAMPLE AND JACK_FOUND)

install(TARGETS oscprompt RUNTIME DESTINATION bin)
