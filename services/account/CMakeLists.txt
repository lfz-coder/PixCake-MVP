add_library(pixcake_account
  src/account_session.cpp
  src/login_flow.cpp
)
target_include_directories(pixcake_account PUBLIC include)
target_link_libraries(pixcake_account PUBLIC pixcake_support pixcake_warnings)
