
# compiling to WASM
// make sure that clang does not link with stdlib, doesn't expect a main function, and allows the extern functions
clang --% --target=wasm32 -Wl,--allow-undefined -nostdlib -Wl,--no-entry -Wl,--export-all -O3 -o main.wasm wasm.c