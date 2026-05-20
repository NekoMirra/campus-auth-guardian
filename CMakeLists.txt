cmake_minimum_required(VERSION 3.10)
project(CampusAuthGuardian)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

# Windows-specific
add_definitions(-D_WIN32_WINNT=0x0A00)
add_definitions(-DWINVER=0x0A00)

if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -finput-charset=utf-8 -fexec-charset=utf-8)
endif()

add_executable(CampusAuthGuardian WIN32
    src/main.cpp
)

# Windows libraries
if(MSVC)
    target_link_libraries(CampusAuthGuardian wininet ws2_32 comctl32 shell32 iphlpapi ole32 propsys shlwapi)
else()
    target_link_libraries(CampusAuthGuardian -lwininet -lws2_32 -lcomctl32 -lshell32 -liphlpapi -lole32 -lpropsys -lshlwapi)
endif()

# Create output directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

install(TARGETS CampusAuthGuardian
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    README.md
    DESTINATION .
)