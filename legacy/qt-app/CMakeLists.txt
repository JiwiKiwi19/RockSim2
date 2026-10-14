cmake_minimum_required(VERSION 3.16)

project(ROCsim VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt automation
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Detect HIP availability
if(UNIX AND EXISTS /opt/rocm/bin/hipcc)
    set(HIP_AVAILABLE TRUE)
    set(CMAKE_CXX_COMPILER /opt/rocm/bin/hipcc)
    message(STATUS "HIP detected - GPU acceleration enabled")
else()
    set(HIP_AVAILABLE FALSE)
    message(STATUS "HIP not found - building without GPU support")
endif()

# Find Qt6
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets LinguistTools OpenGLWidgets Multimedia)
find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools OpenGLWidgets Core Multimedia)

# Translation files
set(TS_FILES ROCsim_en_CA.ts)

# Project sources
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    triangleglwidget.h
    triangleglwidget.cpp
    openglwidget.h
    openglwidget.cpp
    particlesglwidget.h
    particlesglwidget.cpp
    globals.cpp
    globals.h
    newsimulation.h
    newsimulation.cpp
    newsimulation.ui
    particlekernel.cpp
    particlekernel.h
    springmassglwidget.cpp
    springmassglwidget.h
)

# Conditionally add HIP sources
if(HIP_AVAILABLE)
    list(APPEND PROJECT_SOURCES
        particlekernel.h particlekernel.cpp
    )
endif()

# STYLESHEET CONFIGURATION (Work in progress -Jacob)
configure_file(${CMAKE_SOURCE_DIR}/style.qss ${CMAKE_BINARY_DIR}/style.qss COPYONLY)

# Compile Qt resources
qt_add_resources(RESOURCES_RCC
    resources.qrc
)

# Add executable
qt_add_executable(ROCsim
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${RESOURCES_RCC}
    style.qss
    simulationfacade.h simulationfacade.cpp
    homescreen.h homescreen.cpp homescreen.ui
    simulation.h
    ROCsim_icon.rc
    simulationtype.h
)

# Set environment variable for Qt platform
set_target_properties(ROCsim PROPERTIES
    VS_DEBUGGER_ENVIRONMENT "QT_QPA_PLATFORM=xcb"
)

# Translations
qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})

# Link Qt libraries
target_link_libraries(ROCsim PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Multimedia
)

# Platform-specific OpenGL linking
if(UNIX AND NOT APPLE)
    # Linux/WSL - use standard OpenGL
    find_package(OpenGL REQUIRED)
    target_link_libraries(ROCsim PRIVATE OpenGL::GL)
elseif(APPLE)
    # macOS
    find_package(OpenGL REQUIRED)
    target_link_libraries(ROCsim PRIVATE OpenGL::GL)
elseif(WIN32)
    # Native Windows only
    target_link_libraries(ROCsim PRIVATE opengl32)
endif()

# HIP macro
if(HIP_AVAILABLE)
    target_compile_definitions(ROCsim PRIVATE HIP_ENABLED)
endif()

# macOS / Windows bundle properties
## For older Qt versions (< 6.1.0) set an explicit bundle identifier property.
## Use the unexpanded `QT_VERSION` token in the if() call so CMake parses it safely.
if (QT_VERSION VERSION_LESS "6.1.0")
    set_target_properties(ROCsim PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.ROCsim")
endif()

# Common target properties
set_target_properties(ROCsim PROPERTIES
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
)

# Install rules
include(GNUInstallDirs)
install(TARGETS ROCsim
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Finalize Qt executable
if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(ROCsim)
endif()


# cmake_minimum_required(VERSION 3.16)

# project(ROCsim VERSION 0.1 LANGUAGES CXX)

# set(CMAKE_AUTOUIC ON)
# set(CMAKE_AUTOMOC ON)
# set(CMAKE_AUTORCC ON)

# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # Detect HIP availability
# if(UNIX AND EXISTS /opt/rocm/bin/hipcc)
#     set(HIP_AVAILABLE TRUE)
#     set(CMAKE_CXX_COMPILER /opt/rocm/bin/hipcc)
#     message(STATUS "HIP detected - GPU acceleration enabled")
# else()
#     set(HIP_AVAILABLE FALSE)
#     message(STATUS "HIP not found - building without GPU support")
# endif()

# find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
# find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools OpenGLWidgets)

# set(TS_FILES ROCsim_en_CA.ts)

# set(PROJECT_SOURCES
#         main.cpp
#         mainwindow.cpp
#         mainwindow.h
#         mainwindow.ui
#         triangleglwidget.h triangleglwidget.cpp
#         toptoolbar.h toptoolbar.cpp
#         openglwidget.h openglwidget.cpp
#         righttoolbar.h righttoolbar.cpp
#         particlesglwidget.h particlesglwidget.cpp
#         particlesglwidget.h particlesglwidget.cpp
#         globals.h globals.cpp
#         ${TS_FILES}
# )

# # Conditionally add HIP sources only if available
# if(HIP_AVAILABLE)
#     list(APPEND PROJECT_SOURCES
#         hiptest.h
#         hiptest.cpp
#     )
# endif()

# if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
#     find_package(Qt6 REQUIRED COMPONENTS Core)

#     qt_add_executable(ROCsim
#         MANUAL_FINALIZATION
#         ${PROJECT_SOURCES}
#         resources.qrc
#         styles/style.qss
#         resources.qrc
#     )
# # Define target properties for Android with Qt 6 as:
# #    set_property(TARGET ROCsim APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
# #                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# # For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

#     qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
# else()
#     if(ANDROID)
#         add_library(ROCsim SHARED
#             ${PROJECT_SOURCES}
#         )
# # Define properties for Android with Qt 5 after find_package() calls as:
# #    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#     else()
#         add_executable(ROCsim
#             ${PROJECT_SOURCES}
#         )
#     endif()

#     qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
# endif()

# # Link Qt libraries
# target_link_libraries(ROCsim PRIVATE
#     Qt${QT_VERSION_MAJOR}::Widgets
#     Qt${QT_VERSION_MAJOR}::OpenGLWidgets
# )

# # Platform-specific OpenGL linking
# if(WIN32)
#     target_link_libraries(ROCsim PRIVATE opengl32)
# else()
#     find_package(OpenGL REQUIRED)
#     target_link_libraries(ROCsim PRIVATE OpenGL::GL)
# endif()

# # Define HIP_ENABLED macro if HIP is available
# if(HIP_AVAILABLE)
#     target_compile_definitions(ROCsim PRIVATE HIP_ENABLED)
# endif()

# # Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# # If you are developing for iOS or macOS you should consider setting an
# # explicit, fixed bundle identifier manually though.
# if(${QT_VERSION} VERSION_LESS 6.1.0)
#   set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.ROCsim)
# endif()
# set_target_properties(ROCsim PROPERTIES
#     ${BUNDLE_ID_OPTION}
#     MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
#     MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
#     MACOSX_BUNDLE TRUE
#     WIN32_EXECUTABLE TRUE
# )

# include(GNUInstallDirs)
# install(TARGETS ROCsim
#     BUNDLE DESTINATION .
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
# )

# if(QT_VERSION_MAJOR EQUAL 6)
#     qt_finalize_executable(ROCsim)
# endif()