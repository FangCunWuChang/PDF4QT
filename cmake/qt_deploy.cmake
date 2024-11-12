find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt)
message(STATUS "Windeployqt executable path: ${WINDEPLOYQT_EXECUTABLE}")

get_filename_component(WINDEPLOYQT_DIR ${WINDEPLOYQT_EXECUTABLE} DIRECTORY)
set(WINDEPLOYQT_DEBUG_EXECUTABLE "${WINDEPLOYQT_DIR}/windeployqt.debug.bat")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(WINDEPLOYQT_CMD "${WINDEPLOYQT_DEBUG_EXECUTABLE}")
    set(WINDEPLOYQT_TYPE_ARGS "--debug")
    if(MSVC)
        set(WINDEPLOYQT_TYPE_ARGS ${WINDEPLOYQT_TYPE_ARGS} "--pdb")
    endif()
else()
    set(WINDEPLOYQT_CMD "${WINDEPLOYQT_EXECUTABLE}")
    set(WINDEPLOYQT_TYPE_ARGS "--release")
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(VCPKG_PKG_ROOT_DIR ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/)
else()
    set(VCPKG_PKG_ROOT_DIR ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/)
endif()
set(VCPKG_PKG_BIN_DIR ${VCPKG_PKG_ROOT_DIR}/bin/)
set(QT_PKG_ROOT_DIR ${VCPKG_PKG_ROOT_DIR}/Qt6/)

include("${CMAKE_CURRENT_LIST_DIR}/check_target_link_qt.cmake")

function(qt_deploy_target target_name)
    check_target_link_qt(${target_name})

    if(${target_name}_links_qt)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND
                "${WINDEPLOYQT_CMD}" "$<TARGET_FILE:${target_name}>"
                --verbose 0
                --no-quick-import
                --force
                ${WINDEPLOYQT_TYPE_ARGS}
        )
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_PKG_ROOT_DIR}/plugins/texttospeech/"
                "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}/texttospeech/"
        )
    endif()
endfunction()
