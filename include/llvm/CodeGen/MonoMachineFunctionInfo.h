#ifndef LLVM_CODEGEN_MACHINMONOEFUNCTION_H
#define LLVM_CODEGEN_MACHINMONOEFUNCTION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class MonoMachineFunctionInfo {

  int ThisStackSlot;

 public:

  MonoMachineFunctionInfo(MachineFunction &MF) : ThisStackSlot (-1) {
  }

  virtual ~MonoMachineFunctionInfo () {
  }

  void setThisStackSlot (int slot) {
	  ThisStackSlot = slot;
  }

  int getThisStackSlot () const {
	  return ThisStackSlot;
  }
};

}

#endif
