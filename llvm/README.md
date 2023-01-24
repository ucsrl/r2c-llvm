# Return Address Decoys Defense

## Building the compiler
1. clone this repository
1. clone clang into src/tools/clang
1. checkout branch return-address-decoys
1. make sure you have the gold linker installed and set as the system default linker 
1. create a build directory 
1. ``cd <build-dir>``
1. ``cmake -G Ninja -DLLVM_USE_SPLIT_DWARF=TRUE -DLLVM_ENABLE_ASSERTIONS=TRUE -DCMAKE_BUILD_TYPE=Debug -DCMAKE_SHARED_LINKER_FLAGS='-Wl,--gdb-index' -DCMAKE_EXE_LINKER_FLAGS='-Wl,--gdb-index' -DBUILD_SHARED_LIBS=true -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_BINUTILS_INCDIR=/usr/include <src-dir>``
1. ninja -j <number of parallel jobs>

## Building protected programs
As the compiler extension relies on LTO, the defense parameters must be passed as linker options:
For example, the option ``-x86-min-before-return-address-decoys=5`` must be passed as
``-Wl,--plugin-opt,-x86-min-after-return-address-decoys=5``.

Furthermore, if an optimization level >0 is used, frame pointer omission must be disabled. The extension currently
does not handle updating the assembly metadata correctly that allows omitting the frame pointer. 

The FastISel selection instruction is also not supported and must be disabled with ``-fast-isel=false``

For example, to build a binary with 5 decoys before and 5 after the return address, the following commandline could be used.

``<path to custom clang++> -O0 -mno-omit-leaf-frame-pointer -fno-omit-frame-pointer -fno-inline -flto  -Wl,--plugin-opt,-x86-min-after-return-address-decoys=5 -Wl,--plugin-opt,-x86-min-before-return-address-decoys=5 -Wl,--plugin-opt,-fast-isel=false -Wl,--plugin-opt,-stats``