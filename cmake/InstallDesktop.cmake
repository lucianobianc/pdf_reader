# Resolve the executable at install time so --prefix and DESTDIR work correctly.
# Desktop Exec values are parsed twice: by the desktop file reader, then argv.
set(NIGHTREADER_DESKTOP_EXEC "${CMAKE_INSTALL_PREFIX}/bin/nightreader")
string(REPLACE "\\" "\\\\\\\\" NIGHTREADER_DESKTOP_EXEC "${NIGHTREADER_DESKTOP_EXEC}")
string(REPLACE "\"" "\\\\\"" NIGHTREADER_DESKTOP_EXEC "${NIGHTREADER_DESKTOP_EXEC}")
string(REPLACE "`" "\\\\`" NIGHTREADER_DESKTOP_EXEC "${NIGHTREADER_DESKTOP_EXEC}")
string(REPLACE "$" "\\\\$" NIGHTREADER_DESKTOP_EXEC "${NIGHTREADER_DESKTOP_EXEC}")
# A literal percent must not be mistaken for a desktop field code such as %f.
string(REPLACE "%" "%%" NIGHTREADER_DESKTOP_EXEC "${NIGHTREADER_DESKTOP_EXEC}")
set(desktop_directory "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/applications")
file(MAKE_DIRECTORY "${desktop_directory}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../resources/nightreader.desktop.in"
    "${desktop_directory}/nightreader.desktop" @ONLY)
# configure_file is used for prefix substitution; include its output in the manifest.
list(APPEND CMAKE_INSTALL_MANIFEST_FILES "${CMAKE_INSTALL_PREFIX}/share/applications/nightreader.desktop")

# Refresh the user's desktop caches when the tools are available. Package builds
# leave cache updates to the package manager's install hooks.
if("$ENV{DESTDIR}" STREQUAL "")
    find_program(desktop_database_tool update-desktop-database)
    if(desktop_database_tool)
        execute_process(COMMAND "${desktop_database_tool}" "${desktop_directory}"
            RESULT_VARIABLE desktop_result)
        if(NOT desktop_result EQUAL 0)
            message(WARNING "Could not update the desktop application database.")
        endif()
    endif()
    find_program(icon_cache_tool gtk-update-icon-cache)
    if(icon_cache_tool)
        execute_process(COMMAND "${icon_cache_tool}" -q -t "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor"
            RESULT_VARIABLE icon_result)
        if(NOT icon_result EQUAL 0)
            message(WARNING "Could not update the application icon cache.")
        endif()
    endif()
endif()
