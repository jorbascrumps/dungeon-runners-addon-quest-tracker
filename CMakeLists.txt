cmake_minimum_required(VERSION 3.20)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++)
project(QuestTracker CXX)

set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(UPSTREAM external/Dungeon-Runners-Addons)
add_library(QuestTracker SHARED src/addon.cpp src/quests.cpp src/journal.cpp src/overlay.cpp ${UPSTREAM}/Source/AardvarkUI/ui.cpp)
target_include_directories(QuestTracker PRIVATE src ${UPSTREAM})
target_link_libraries(QuestTracker PRIVATE advapi32 gdi32)   # SHA-256 of ui.bin (windows_hash.h); AardvarkUI's GDI font path
target_compile_definitions(QuestTracker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
# Undecorated stdcall export names, and no runtime DLLs to ship alongside.
target_link_options(QuestTracker PRIVATE -Wl,--kill-at -static -static-libgcc -static-libstdc++ -s)
set_target_properties(QuestTracker PROPERTIES OUTPUT_NAME Addon PREFIX "")

set(DIST ${CMAKE_SOURCE_DIR}/dist/QuestTracker)
add_custom_command(TARGET QuestTracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:QuestTracker> ${DIST}/Addon.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/addon.ini ${DIST}/addon.ini
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/LICENSE ${DIST}/LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/${UPSTREAM}/LICENSE ${DIST}/LICENSE-Dungeon-Runners-Addons.txt)

set(DEPLOY_DIR "" CACHE PATH "Extra folder to copy Addon.dll into after each build")
if(DEPLOY_DIR)
    add_custom_command(TARGET QuestTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:QuestTracker> ${DEPLOY_DIR}/Addon.dll VERBATIM)
endif()
