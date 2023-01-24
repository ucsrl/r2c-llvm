# R2C

## Benchmarking
If you want to run the benchmarks, follow the instructions from https://github.com/fberlakovich/r2c-benchmarking.
The repository contains benchmarking infrastructure that automatically builds the R²C compiler and runs SPEC CPU 2017 and webserver benchmarks.

## Building Webservers
To build the webservers, you need a fully built version of the R²C compiler.
If you have run the benchmarks with the benchmarking infrastructure already, you can reuse compiler already built.
Alternatively, you can manually build the compiler.

> **WARNING**: Building the webservers requires *a lot* of CPU and memory, especially when using a large number of parallel build jobs. 
> This is especially true because the R²C compiler was not optimized for compilation speed. 
> If you encounter OOM errors, try building with fewer parallel jobs.  

### Reusing the compiler build from the benchmarking infrastructure
You can find the built compiler in `<benchmarking-dir>/build/packages/llvm-src-13.0.0/obj/bin`, where `<benchmarking-dir>` is the directory with the `r2c-benchmarking` code.
Note that you will find this directory only if you have at least benchmark before (see the `r2c-benchmarking` repository for details).

### Building the compiler manually
1. clone this repository
1. checkout branch `r2c`
1. make sure you have the gold linker installed and set as the system default linker 
1. create a build directory 
1. `cd <build-dir>`
1. build the compiler
    1. For a debug build: `cmake -G Ninja -DLLVM_ENABLE_PROJECTS='clang;compiler-rt' -DLLVM_ENABLE_ASSERTIONS=TRUE -DCMAKE_BUILD_TYPE=Debug -DCMAKE_SHARED_LINKER_FLAGS='-Wl,--gdb-index' -DCMAKE_EXE_LINKER_FLAGS='-Wl,--gdb-index' -DBUILD_SHARED_LIBS=true -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_BINUTILS_INCDIR=/usr/include <src-dir>/llvm`
    2. For a release build: `cmake -G Ninja -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_ENABLE_ASSERTIONS=TRUE -DBUILD_SHARED_LIBS=true -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_BINUTILS_INCDIR=/usr/include <src-dir>/llvm`
1. ninja -j <number of parallel jobs>


### Building Webkit

To build the GTK version with R²C protections enabled, follow these instructions:

1. Fetch the Webkit source from https://github.com/fberlakovich/Webkit, e.g., to `webkit/src`

   The repository contains the version of Webkit we built for the paper plus a commit with the required modifications.
   See the Limitations section in the paper for why the modifications are neccessary.
1. Make sure to install all the dependencies required to build Webkit (see https://trac.webkit.org/wiki/BuildingGtk#Dependencies)
1. Create a build directory, e.g., `webkit/build` and run cmake with the following options 

   ``
   export COMPILER_FLAGS="-flto=thin -O3 -fbtras=10 -fheap-boobytraps=10" export LDFLAGS="-flto=thin -Wl,--plugin-opt,-fast-isel=false -fbtras=10 -fheap-boobytraps=10 -Wl,--plugin-opt,-assume-btra-callee=maybe -Wl,--plugin-opt,-prolog-min-padding-instructions=1 -Wl,--plugin-opt,-prolog-max-padding-instructions=5 -Wl,--plugin-opt,-shuffle-functions=true -Wl,--plugin-opt,-shuffle-globals=true -Wl,--plugin-opt,-randomize-reg-alloc=true" && cmake -DCMAKE_C_COMPILER:FILEPATH=<llvm-build>/bin/clang -DCMAKE_RANLIB=<llvm-build>/bin/llvm-ranlib -DCMAKE_NM=<llvm-build>/bin/llvm-nm -DCMAKE_AR=<llvm-build>/bin/llvm-ar -DCMAKE_CXX_COMPILER:FILEPATH=<llvm-build>/bin/clang++ -DCMAKE_C_FLAGS=$COMPILER_FLAGS -DCMAKE_CXX_FLAGS=$COMPILER_FLAGS -DPORT="GTK" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release -G Ninja -DUSE_WPE_RENDERER=Off -DENABLE_GAMEPAD=OFF -DDEVELOPER_MODE=ON -DENABLE_EXPERIMENTAL_FEATURES=ON "<webkit-src>"
   ``
   
   Make sure to replace
   1. `<llvm-build>` with your LLVM build directory
   2. `<webkit-src>` with your Webkit source directory

1. Run `ninja`

After building successfully, you will find the binaries below the `bin` directory.
You can run the tests, by running one of the `test*` binaries or start the browser by running `MiniBrowser`.