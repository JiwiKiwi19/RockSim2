add_executable(rocksim-cli main.cpp)
target_link_libraries(rocksim-cli PRIVATE RockSim::engine rocksim_warnings)
