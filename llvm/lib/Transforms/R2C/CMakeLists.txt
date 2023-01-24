add_llvm_component_library(LLVMr2c
        DiversityPass.cpp
        HeapBoobyTrapPass.cpp
        R2CMarker.cpp
        CollectStatistics.cpp
        GlobalRandomization.cpp

        ADDITIONAL_HEADER_DIRS
        ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
        ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/R2C

        DEPENDS
        intrinsics_gen
        )
