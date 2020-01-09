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
    QT5_WRAP_UI(FORMS_HEADERS store_form.ui framelesswindow.ui list_form.ui item_form.ui account_form.ui about_form.ui)
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

        add_executable(
            ola_client_launcher
            WIN32
            launcher_main.cpp
        )

        add_executable(
            ola_client_env
            env_main.cpp
        )

        add_dependencies(ola_client_launcher ola_client_store)
        target_link_libraries(
            ola_client_launcher
            ${SYS_BASIC_LIBS}
            ${SYS_DYNAMIC_LOAD_LIB}
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
    set_target_properties(
        ola_client_store PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:ola_client_store>)

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

    add_custom_command(TARGET ola_client_store POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Core> $<TARGET_FILE_DIR:ola_client_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Gui> $<TARGET_FILE_DIR:ola_client_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE_DIR:ola_client_store>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/certs  $<TARGET_FILE_DIR:ola_client_store>/certs
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/ola_store.build $<TARGET_FILE_DIR:ola_client_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/ola_store.media $<TARGET_FILE_DIR:ola_client_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENSSL_SSL_DLL} $<TARGET_FILE_DIR:ola_client_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENSSL_CRYPTO_DLL} $<TARGET_FILE_DIR:ola_client_store>
        COMMAND ${CMAKE_COMMAND} -DINFILE="${CMAKE_CURRENT_SOURCE_DIR}/ola_upload.in"  -DOUTFILE=$<TARGET_FILE_DIR:ola_client_store>/ola_upload -DPROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/GenericConfigureFile.cmake"
    )
endif()
