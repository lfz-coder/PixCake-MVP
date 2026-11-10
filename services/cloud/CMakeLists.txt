add_library(pixcake_cloud
  src/cloud_session.cpp
  src/cloud_service.cpp
  src/cloud_validation.cpp
)
target_include_directories(pixcake_cloud PUBLIC include)
target_link_libraries(pixcake_cloud PUBLIC pixcake_account pixcake_support pixcake_warnings)
