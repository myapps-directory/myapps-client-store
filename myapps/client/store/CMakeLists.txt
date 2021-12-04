list(APPEND CMAKE_PREFIX_PATH "${EXTERNAL_PATH}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Instruct CMake to run moc automatically when needed
set(CMAKE_AUTOMOC ON)
# Create code from a list of Qt designer ui files
set(CMAKE_AUTOUIC ON)

set(Qt5_DIR ${QT5PREFIX_DIR}/Qt5)

message("Finding Qt5 from ${Qt5_DIR}")
find_package(Qt5 COMPONENTS Core Widgets Gui)

if(Qt5_FOUND AND WIN32 AND NOT TARGET Qt5::windeployqt)
    
    message("Finding Qt5 windeployqt")
    get_target_property(_qt5_qmake_location Qt5::qmake IMPORTED_LOCATION)

    execute_process(
        COMMAND "${_qt5_qmake_location}" -query QT_INSTALL_PREFIX
        RESULT_VARIABLE return_code
        OUTPUT_VARIABLE qt5_install_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(imported_location "${qt5_install_prefix}/bin/windeployqt.exe")

    if(EXISTS ${imported_location})
        add_executable(Qt5::windeployqt IMPORTED)

        set_target_properties(Qt5::windeployqt PROPERTIES
            IMPORTED_LOCATION ${imported_location}
        )
    endif()
endif()

if(${Qt5Widgets_FOUND})
    
    QT5_WRAP_UI(FORMS_HEADERS store_form.ui list_form.ui item_form.ui account_form.ui about_form.ui configure_form.ui)
    QT5_ADD_RESOURCES(FORMS_HEADERS store_main.qrc)
    
    set(SRCS
        store_form.ui
        store_main.cpp
        store_main_window.cpp
        store_main_window.hpp
        store_engine.cpp
        store_engine.hpp
        ${FORMS_HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/upload.in
        ${CMAKE_CURRENT_SOURCE_DIR}/store.build.yml
    )

    if(WIN32)
        set_source_files_properties(store_main.cpp PROPERTIES COMPILE_FLAGS "-DMYAPPS_FRONT_URL=\\\"${MYAPPS_FRONT_URL}\\\" -bigobj")
        add_executable(
            myapps_store
            WIN32
            ${SRCS}
            store_main.rc
        )

        if(TARGET Qt5::windeployqt)
            if(CMAKE_BUILD_TYPE MATCHES "debug")
                set(BUILD_TYPE "--debug")
            else()
                set(BUILD_TYPE "--release")
            endif()
            # execute windeployqt in a tmp directory after build
            add_custom_command(TARGET myapps_store
                POST_BUILD
                #COMMAND ${CMAKE_COMMAND} -E remove_directory $<TARGET_FILE_DIR:myapps_store>/windeployqt
                COMMAND set PATH=%PATH%$<SEMICOLON>${qt5_install_prefix}/bin
                COMMAND Qt5::windeployqt ${BUILD_TYPE} "$<TARGET_FILE_DIR:myapps_store>/$<TARGET_FILE_NAME:myapps_store>"
            )

            # copy deployment directory during installation
            install(DIRECTORY $<TARGET_FILE_DIR:myapps_store>//windeployqt/ DESTINATION bin PATTERN "windeployqt/*")
        endif()


        add_executable(
            myapps_client_launcher
            WIN32
            launcher_main.cpp
        )

        add_executable(
            myapps_client_env
            env_main.cpp
        )

        add_dependencies(myapps_client_launcher myapps_store)
        target_link_libraries(
            myapps_client_launcher
            ${SYS_BASIC_LIBS}
            ${SYS_DYNAMIC_LOAD_LIB}
        )

        target_include_directories(myapps_client_launcher PRIVATE
            ${Boost_INCLUDE_DIRS}
        )

    elseif(APPLE)
        add_executable(
            myapps_store
            MACOSX_BUNDLE
            ${SRCS}
        )
    else()
        add_executable(
            myapps_store
            ${SRCS}
        )
    endif()

    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
        DEPENDS
        upload.in
        store.build.yml
        COMMENT "Force trigger rebuild when artifact files do change"
    )

    add_dependencies(myapps_store build-snappy build-cereal)
    
    set_target_properties(myapps_store
        PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:myapps_store>)

    target_link_libraries( myapps_store
        myapps_client_utility
		myapps_utility
        SolidFrame::solid_frame_mprpc
        SolidFrame::solid_frame_aio_openssl
        SolidFrame::solid_serialization_v3
        Boost::program_options
        Boost::system
        Qt5::Widgets
        ${SNAPPY_LIB}
        ${SYS_BASIC_LIBS}
        ${SYS_DYNAMIC_LOAD_LIB}
    )

    add_custom_command(TARGET myapps_store POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Core> $<TARGET_FILE_DIR:myapps_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Gui> $<TARGET_FILE_DIR:myapps_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE_DIR:myapps_store>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/myapps/client/utility/certs  $<TARGET_FILE_DIR:myapps_store>/certs
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/store.build.yml $<TARGET_FILE_DIR:myapps_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENSSL_SSL_DLL} $<TARGET_FILE_DIR:myapps_store>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENSSL_CRYPTO_DLL} $<TARGET_FILE_DIR:myapps_store>
        COMMAND ${CMAKE_COMMAND} -DINFILE="${CMAKE_CURRENT_SOURCE_DIR}/upload.in"  -DOUTFILE=$<TARGET_FILE_DIR:myapps_store>/upload -DPROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}" -P "${CMAKE_SOURCE_DIR}/cmake/generic-configure-file.cmake"
    )
endif()
