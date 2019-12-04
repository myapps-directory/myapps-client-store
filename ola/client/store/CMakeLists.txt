list(APPEND CMAKE_PREFIX_PATH "${EXTERNAL_PATH}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed
set(CMAKE_AUTOMOC ON)
# Create code from a list of Qt designer ui files
set(CMAKE_AUTOUIC ON)

set(Qt5Widgets_DIR ${QT5PREFIX_DIR}/Qt5Widgets)
set(Qt5Core_DIR ${QT5PREFIX_DIR}/Qt5Core)
set(Qt5Gui_DIR ${QT5PREFIX_DIR}/Qt5Gui)

message("Finding Qt5 from ${Qt5Widgets_DIR}")

find_package (Qt5Core)
find_package (Qt5Gui)
find_package (Qt5Widgets)

if(${Qt5Widgets_FOUND})
    QT5_WRAP_UI(FORMS_HEADERS store_form.ui framelesswindow.ui list_form.ui item_form.ui)
    QT5_ADD_RESOURCES( FORMS_HEADERS darkstyle.qrc framelesswindow.qrc)
    
    set(SRCS
        store_form.ui
        store_main.cpp
        store_main_window.cpp
        store_main_window.hpp
        framelesswindow.h
        framelesswindow.cpp
        store_engine.cpp
        store_engine.hpp
        DarkStyle.cpp
        DarkStyle.h
        windowdragger.h
        windowdragger.cpp
        ${FORMS_HEADERS}
    )

    if(WIN32)
        set_source_files_properties(store_main.cpp PROPERTIES COMPILE_FLAGS -bigobj)
        add_executable(
            ola_client_store
            WIN32
            ${SRCS}
            store_main.rc
        )
    elseif(APPLE)
        add_executable(
            ola_store_store
            MACOSX_BUNDLE
            ${SRCS}
        )
    else()
        add_executable(
            ola_client_store
            ${SRCS}
        )
    endif()

    add_dependencies(ola_client_store build-snappy build-cereal)

    target_link_libraries(
        ola_client_store
		ola_utility
        solid_frame_mprpc
        solid_frame_aio_openssl
        solid_serialization_v2
        ${OPENSSL_LIBRARIES}
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Boost_SYSTEM_LIBRARY}
        Qt5::Widgets
        ${SNAPPY_LIB}
        ${SYS_BASIC_LIBS}
        ${SYS_DYNAMIC_LOAD_LIB}
    )
endif()
