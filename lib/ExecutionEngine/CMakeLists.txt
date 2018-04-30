if(NOT LLVM_BUILD_EXECUTION_ENGINE)
    set(EXCLUDE_FROM_ALL ON)
endif()

add_llvm_library(LLVMExecutionEngine
  ExecutionEngine.cpp
  ExecutionEngineBindings.cpp
  GDBRegistrationListener.cpp
  SectionMemoryManager.cpp
  TargetSelect.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ExecutionEngine

  DEPENDS
  intrinsics_gen
  )

if(BUILD_SHARED_LIBS)
  target_link_libraries(LLVMExecutionEngine PUBLIC LLVMRuntimeDyld)
endif()

if(LLVM_BUILD_EXECUTION_ENGINE)
add_subdirectory(Interpreter)
add_subdirectory(MCJIT)
add_subdirectory(Orc)
add_subdirectory(RuntimeDyld)
endif()

if( LLVM_USE_OPROFILE )
  add_subdirectory(OProfileJIT)
endif( LLVM_USE_OPROFILE )

if( LLVM_USE_INTEL_JITEVENTS )
  add_subdirectory(IntelJITEvents)
endif( LLVM_USE_INTEL_JITEVENTS )
