add_llvm_component_library(LLVMR2COptions
        R2COptions.cpp

        ADDITIONAL_HEADER_DIRS
        ${LLVM_MAIN_INCLUDE_DIR}/llvm/R2COptions
        )