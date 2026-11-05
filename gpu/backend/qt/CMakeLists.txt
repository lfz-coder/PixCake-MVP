find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

add_library(pixcake_gpu_backend_qt
  src/qt_backend.cpp
)
target_include_directories(pixcake_gpu_backend_qt
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/gpu/facade/include
)
target_compile_definitions(pixcake_gpu_backend_qt PUBLIC PIXCAKE_GL_BACKEND_QT)
target_link_libraries(pixcake_gpu_backend_qt
  PUBLIC
    Qt6::Gui
    Qt6::OpenGL
    pixcake_warnings
)
target_compile_features(pixcake_gpu_backend_qt PUBLIC cxx_std_17)
