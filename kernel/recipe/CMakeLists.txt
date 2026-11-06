add_library(pixcake_recipe
  src/recipe.cpp
)
target_include_directories(pixcake_recipe PUBLIC include)
target_link_libraries(pixcake_recipe PUBLIC pixcake_support pixcake_warnings)
