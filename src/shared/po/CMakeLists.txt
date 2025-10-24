# SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Find msgfmt for compiling .po files
find_program(MSGFMT_EXECUTABLE msgfmt)

if(MSGFMT_EXECUTABLE)
    # Get list of all .po files
    file(GLOB PO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.po")

    foreach(PO_FILE ${PO_FILES})
        # Get language code from filename (e.g., pl.po -> pl)
        get_filename_component(LANG ${PO_FILE} NAME_WE)

        # Define output .mo file path
        set(MO_FILE "${CMAKE_CURRENT_BINARY_DIR}/${LANG}.mo")

        # Compile .po to .mo
        add_custom_command(
            OUTPUT ${MO_FILE}
            COMMAND ${MSGFMT_EXECUTABLE} -o ${MO_FILE} ${PO_FILE}
            DEPENDS ${PO_FILE}
            COMMENT "Compiling ${LANG} translation"
        )

        # Install .mo file to system locale directory
        install(FILES ${MO_FILE}
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/locale/${LANG}/LC_MESSAGES
                RENAME krunner_yubikey.mo)

        # Add to build target
        list(APPEND MO_FILES ${MO_FILE})
    endforeach()

    # Create custom target for all translations
    add_custom_target(translations ALL DEPENDS ${MO_FILES})

    message(STATUS "Translation files will be compiled and installed")
else()
    message(WARNING "msgfmt not found - translations will not be compiled")
endif()
