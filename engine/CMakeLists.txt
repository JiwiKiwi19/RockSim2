# The engine depends on the C++ standard library only, so it compiles
# unchanged for native targets and for WebAssembly.
add_library(rocksim_engine STATIC
  src/world.cpp
  src/contacts.cpp
  src/integrators.cpp
  src/simulation.cpp
  src/scenarios.cpp
  src/analysis.cpp
)
add_library(RockSim::engine ALIAS rocksim_engine)
target_include_directories(rocksim_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(rocksim_engine PUBLIC cxx_std_20)
target_link_libraries(rocksim_engine PRIVATE rocksim_warnings)

# Reproducibility: forbid fused multiply-add contraction. Clang (including
# Apple Clang on arm64) contracts a*b+c into one FMA by default, which rounds
# differently from separate * and + and would make native and WebAssembly
# runs diverge. The engine also avoids libm functions that are not correctly
# rounded (see docs/physics.md).
#
# PUBLIC, not PRIVATE: headers such as rng.hpp define inline functions that
# feed initial conditions. In unoptimized builds the linker keeps one copy of
# each inline function across all object files, so a consumer (tests, CLI)
# compiled with contraction could supply the copy the engine calls. That is
# exactly what broke the golden-hash test in the macOS Debug CI job.
if(NOT MSVC)
  target_compile_options(rocksim_engine PUBLIC -ffp-contract=off)
endif()
