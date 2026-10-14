# WebAssembly module for the web app. Configure with:
#   emcmake cmake -S . -B build/wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build/wasm
# The output (rocksim.mjs, wasm embedded) is written to web/src/wasm/.
add_executable(rocksim_wasm bindings.cpp)
target_link_libraries(rocksim_wasm PRIVATE RockSim::engine rocksim_warnings)
set(ROCKSIM_WASM_OUT ${PROJECT_SOURCE_DIR}/web/src/wasm)
set_target_properties(rocksim_wasm PROPERTIES
  OUTPUT_NAME rocksim
  SUFFIX ".mjs"
  RUNTIME_OUTPUT_DIRECTORY ${ROCKSIM_WASM_OUT})
target_link_options(rocksim_wasm PRIVATE
  --bind
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createRockSim
  -sENVIRONMENT=web,worker,node
  -sALLOW_MEMORY_GROWTH=1
  # Embed the .wasm in the JS module: one file, no path resolution issues in
  # workers or bundlers. Costs ~33% size on the wasm part.
  -sSINGLE_FILE=1
  -sFILESYSTEM=0
  -sASSERTIONS=0)
