add_library(quine-storage
    engine.cpp
    shard.cpp
    compaction.cpp
)

target_link_libraries(quine-storage PUBLIC
    quine-core # If it needs logging/utils later
)

target_include_directories(quine-storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
