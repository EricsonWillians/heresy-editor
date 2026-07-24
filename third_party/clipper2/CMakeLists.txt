add_library(clipper2 STATIC
    src/clipper.engine.cpp
    src/clipper.offset.cpp
)

target_include_directories(clipper2 PUBLIC include)
set_target_properties(clipper2 PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/out/library
)
