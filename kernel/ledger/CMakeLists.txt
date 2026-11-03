add_library(pixcake_ledger
  src/credit_ledger.cpp
)
target_include_directories(pixcake_ledger PUBLIC include)
target_link_libraries(pixcake_ledger PUBLIC pixcake_support pixcake_warnings)
