//===---*- mode: c++; indent-tabs-mode: nil -*----------------------------===//
//===-- MonoException.h - Dwarf Exception Framework -----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_MONOEXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_MONOEXCEPTION_H

#include "EHStreamer.h"
#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {

class TargetRegisterInfo;

class MonoException : public EHStreamer {
public:
    MonoException(AsmPrinter *A, bool disableGNUEH);
  virtual ~MonoException();

  virtual void endModule();

  virtual void beginFunction(const MachineFunction *MF);

  virtual void endFunction(const MachineFunction *);

  virtual void beginInstruction(const MachineInstr *MI);
private:

  struct MonoCallSiteEntry {
    // The 'try-range' is BeginLabel .. EndLabel.
    MCSymbol *BeginLabel; // zero indicates the start of the function.
    MCSymbol *EndLabel;   // zero indicates the end of the function.

    // The landing pad starts at PadLabel.
    MCSymbol *PadLabel;   // zero indicates that there is no landing pad.
    int TypeID;
  };

  // Per-function EH info
  struct EHInfo {
    int FunctionNumber, MonoMethodIdx;
	MCSymbol *BeginSym, *EndSym, *FDESym;
	std::vector<MCSymbol*> EHLabels;
    std::vector<MCCFIInstruction> Instructions;
    std::vector<MonoCallSiteEntry> CallSites;
    std::vector<const GlobalValue *> TypeInfos;
    std::vector<LandingPadInfo> PadInfos;
    int FrameReg;
    int ThisOffset;
    bool HasLandingPads;

    EHInfo() {
      FunctionNumber = 0;
      MonoMethodIdx = 0;
      BeginSym = nullptr;
      EndSym = nullptr;
      FrameReg = -1;
      ThisOffset = 0;
      HasLandingPads = 0;
    }
  };

  void PrepareMonoLSDA(EHInfo *info);
  void EmitMonoLSDA(const EHInfo *info);
  void EmitFnStart();
  void EmitFnEnd();

  std::vector<MCSymbol*> EHLabels;
  std::vector<EHInfo> Frames;
  StringMap<int> FuncIndexes;
  const TargetRegisterInfo *RI;
  bool DisableGNUEH;
};
} // End of namespace llvm

#if 0
class DwarfMonoException : public EHStreamer {

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
#endif

#endif

