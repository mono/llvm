//===-- DwarfException.h - Dwarf Exception Framework -----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf exception info into asm files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXCEPTION_H

#include "EHStreamer.h"
#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {
class MachineFunction;
class ARMTargetStreamer;

class DwarfCFIException : public EHStreamer {
  /// shouldEmitPersonality - Per-function flag to indicate if .cfi_personality
  /// should be emitted.
  bool shouldEmitPersonality;

  /// shouldEmitLSDA - Per-function flag to indicate if .cfi_lsda
  /// should be emitted.
  bool shouldEmitLSDA;

  /// shouldEmitMoves - Per-function flag to indicate if frame moves info
  /// should be emitted.
  bool shouldEmitMoves;

  AsmPrinter::CFIMoveType moveTypeModule;

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  DwarfCFIException(AsmPrinter *A);
  virtual ~DwarfCFIException();

  /// endModule - Emit all exception information that should come after the
  /// content.
  void endModule() override;

  /// beginFunction - Gather pre-function exception information.  Assumes being
  /// emitted immediately after the function entry point.
  void beginFunction(const MachineFunction *MF) override;

  /// endFunction - Gather and emit post-function exception information.
  void endFunction(const MachineFunction *) override;
};

class ARMException : public EHStreamer {
  void emitTypeInfos(unsigned TTypeEncoding) override;
  ARMTargetStreamer &getTargetStreamer();

  /// shouldEmitCFI - Per-function flag to indicate if frame CFI info
  /// should be emitted.
  bool shouldEmitCFI;

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  ARMException(AsmPrinter *A);
  virtual ~ARMException();

  /// endModule - Emit all exception information that should come after the
  /// content.
  void endModule() override;

  /// beginFunction - Gather pre-function exception information.  Assumes being
  /// emitted immediately after the function entry point.
  void beginFunction(const MachineFunction *MF) override;

  /// endFunction - Gather and emit post-function exception information.
  void endFunction(const MachineFunction *) override;
};

class DwarfMonoException : public EHStreamer {

  struct MonoCallSiteEntry {
    // The 'try-range' is BeginLabel .. EndLabel.
    MCSymbol *BeginLabel; // zero indicates the start of the function.
    MCSymbol *EndLabel;   // zero indicates the end of the function.

    // The landing pad starts at PadLabel.
    MCSymbol *PadLabel;   // zero indicates that there is no landing pad.
    int TypeID;
  };

  // Mono specific
  struct MonoEHFrameInfo {
    const MachineFunction *MF;
    std::vector<MonoCallSiteEntry> CallSites;
    std::vector<const GlobalVariable *> TypeInfos;
    std::vector<unsigned> FilterIds;
    std::vector<LandingPadInfo> PadInfos;
    int FunctionNumber;
    int MonoMethodIdx;
    int FrameReg;
    int ThisOffset;
  };

  struct FunctionEHFrameInfo {
    MCSymbol *FunctionEHSym;  // L_foo.eh
    unsigned Number;
    unsigned PersonalityIndex;
    bool adjustsStack;
    bool hasLandingPads;
    std::vector<MCCFIInstruction> Instructions;
	std::vector<MCSymbol*> EHLabels;
    const Function *function;

    MonoEHFrameInfo MonoEH;

    FunctionEHFrameInfo(MCSymbol *EHSym, unsigned Num, unsigned P,
                        bool hC, bool hL,
                        const std::vector<MCCFIInstruction> &M,
						const std::vector<MCSymbol*> &EHLabels,
                        const Function *f):
      FunctionEHSym(EHSym), Number(Num), PersonalityIndex(P),
      adjustsStack(hC), hasLandingPads(hL), Instructions(M), EHLabels(EHLabels), function (f) { }
  };

  std::vector<FunctionEHFrameInfo> EHFrames;

  StringMap<int> FuncIndexes;
  std::vector<MCSymbol*> EHLabels;

public:
  DwarfMonoException(AsmPrinter *A);
  virtual ~DwarfMonoException();

  virtual void endModule();

  virtual void beginFunction(const MachineFunction *MF);

  virtual void endFunction(const MachineFunction *);

  virtual void beginInstruction(const MachineInstr *MI);

  // EmitMonoEHFrame - Emit Mono specific exception handling tables
  void EmitMonoEHFrame(const Function *Personality);

  void PrepareMonoLSDA(FunctionEHFrameInfo *EHFrameInfo);

  void EmitMonoLSDA(const FunctionEHFrameInfo *EHFrameInfo);
};

} // End of namespace llvm

#endif
