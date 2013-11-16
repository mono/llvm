//===----- JITDwarfEmitter.cpp - Write dwarf tables into memory -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a JITDwarfEmitter object that is used by the JIT to
// write dwarf tables to memory.
//
//===----------------------------------------------------------------------===//

#include "JITDwarfEmitter.h"
#include "JIT.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/JITCodeEmitter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MonoMachineFunctionInfo.h"
#include "llvm/ExecutionEngine/JITMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegisterInfo.h"
using namespace llvm;

JITDwarfEmitter::JITDwarfEmitter(JIT& theJit) : MMI(0), Jit(theJit) {}


unsigned char* JITDwarfEmitter::EmitDwarfTable(MachineFunction& F,
                                               JITCodeEmitter& jce,
                                               unsigned char* StartFunction,
                                               unsigned char* EndFunction,
                                               unsigned char* &EHFramePtr) {
  assert(MMI && "MachineModuleInfo not registered!");

  const TargetMachine& TM = F.getTarget();
  TD = TM.getDataLayout();
  stackGrowthDirection = TM.getFrameLowering()->getStackGrowthDirection();
  RI = TM.getRegisterInfo();
  MAI = TM.getMCAsmInfo();
  JCE = &jce;

  unsigned char* ExceptionTable = EmitMonoLSDA(&F, StartFunction,
                                               EndFunction);

  unsigned char* Result = 0;

  const std::vector<const Function *> Personalities = MMI->getPersonalities();
  EHFramePtr = EmitCommonEHFrame(Personalities[MMI->getPersonalityIndex()]);

  Result = EmitEHFrame(Personalities[MMI->getPersonalityIndex()], EHFramePtr,
                       StartFunction, EndFunction, ExceptionTable);

  return Result;
}

void
JITDwarfEmitter::EmitCFIInstructions(intptr_t BaseLabelPtr,
                                const std::vector<MCCFIInstruction> &Instructions) const {
  unsigned PointerSize = TD->getPointerSize();
  int stackGrowth = stackGrowthDirection == TargetFrameLowering::StackGrowsUp ?
          PointerSize : -PointerSize;
  MCSymbol *BaseLabel = 0;

  for (unsigned i = 0, N = Instructions.size(); i < N; ++i) {
    const MCCFIInstruction &Instr = Instructions[i];
    MCSymbol *Label = Instr.getLabel();

    // Throw out move if the label is invalid.
    if (Label && (*JCE->getLabelLocations())[Label] == 0)
      continue;

    intptr_t LabelPtr = 0;
    if (Label) LabelPtr = JCE->getLabelAddress(Label);

    // Advance row if new location.
    if (BaseLabelPtr && Label && BaseLabel != Label) {
      JCE->emitByte(dwarf::DW_CFA_advance_loc4);
      JCE->emitInt32(LabelPtr - BaseLabelPtr);

      BaseLabel = Label;
      BaseLabelPtr = LabelPtr;
    }

	// Similar to FrameEmitterImpl::EmitCFIInstruction () in MCDwarf.cpp
    switch (Instr.getOperation()) {
    case MCCFIInstruction::OpDefCfaOffset: {
      JCE->emitByte(dwarf::DW_CFA_def_cfa_offset);
      JCE->emitULEB128Bytes(Instr.getOffset());
      break;
    }
    case MCCFIInstruction::OpDefCfa: {
      JCE->emitByte(dwarf::DW_CFA_def_cfa);
      JCE->emitULEB128Bytes(Instr.getRegister());
      JCE->emitULEB128Bytes(-Instr.getOffset());
      break;
    }
    case MCCFIInstruction::OpDefCfaRegister: {
      JCE->emitByte(dwarf::DW_CFA_def_cfa_register);
      JCE->emitULEB128Bytes(Instr.getRegister());
      break;
    }
    case MCCFIInstruction::OpOffset: {
      unsigned Reg = Instr.getRegister();
      int Offset = Instr.getOffset() / stackGrowth;

      if (Offset < 0) {
        JCE->emitByte(dwarf::DW_CFA_offset_extended_sf);
        JCE->emitULEB128Bytes(Reg);
        JCE->emitSLEB128Bytes(Offset);
      } else if (Reg < 64) {
        JCE->emitByte(dwarf::DW_CFA_offset + Reg);
        JCE->emitULEB128Bytes(Offset);
      } else {
        JCE->emitByte(dwarf::DW_CFA_offset_extended);
        JCE->emitULEB128Bytes(Reg);
        JCE->emitULEB128Bytes(Offset);
      }
      break;
    }
    default:
      llvm_unreachable("Unhandled case in switch");
    }
  }
}

/// PadLT - Order landing pads lexicographically by type id.
static bool PadLT(const LandingPadInfo *L, const LandingPadInfo *R) {
  const std::vector<int> &LIds = L->TypeIds, &RIds = R->TypeIds;
  unsigned LSize = LIds.size(), RSize = RIds.size();
  unsigned MinSize = LSize < RSize ? LSize : RSize;

  for (unsigned i = 0; i != MinSize; ++i)
    if (LIds[i] != RIds[i])
      return LIds[i] < RIds[i];

  return LSize < RSize;
}

namespace {

/// PadRange - Structure holding a try-range and the associated landing pad.
struct PadRange {
  // The index of the landing pad.
  unsigned PadIndex;
  // The index of the begin and end labels in the landing pad's label lists.
  unsigned RangeIndex;
};

typedef DenseMap<MCSymbol*, PadRange> RangeMapType;

/// CallSiteEntry - Structure describing an entry in the call-site table.
struct CallSiteEntry {
  MCSymbol *BeginLabel; // zero indicates the start of the function.
  MCSymbol *EndLabel;   // zero indicates the end of the function.
  MCSymbol *PadLabel;   // zero indicates that there is no landing pad.
  int TypeID;
};

}

unsigned char* JITDwarfEmitter::EmitMonoLSDA(MachineFunction* MF,
                                             unsigned char* StartFunction,
                                             unsigned char* EndFunction) const {
  assert(MMI && "MachineModuleInfo not registered!");

  // Map all labels and get rid of any dead landing pads.
  MMI->TidyLandingPads(JCE->getLabelLocations());

  const std::vector<const GlobalVariable *> &TypeInfos = MMI->getTypeInfos();
  const std::vector<LandingPadInfo> &PadInfos = MMI->getLandingPads();

  int ThisSlot = MF->getMonoInfo()->getThisStackSlot();

  if (PadInfos.empty() && ThisSlot == -1)
    return 0;

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());
  for (unsigned i = 0, N = PadInfos.size(); i != N; ++i)
    LandingPads.push_back(&PadInfos[i]);
  std::sort(LandingPads.begin(), LandingPads.end(), PadLT);

  // Compute the call-site table.  Entries must be ordered by address.
  SmallVector<CallSiteEntry, 64> CallSites;

  RangeMapType PadMap;
  for (unsigned i = 0, N = LandingPads.size(); i != N; ++i) {
    const LandingPadInfo *LandingPad = LandingPads[i];
    for (unsigned j=0, E = LandingPad->BeginLabels.size(); j != E; ++j) {
      MCSymbol *BeginLabel = LandingPad->BeginLabels[j];
      assert(!PadMap.count(BeginLabel) && "Duplicate landing pad labels!");
      PadRange P = { i, j };
      PadMap[BeginLabel] = P;
    }
  }

  MCSymbol *LastLabel = 0;
  for (MachineFunction::const_iterator I = MF->begin(), E = MF->end();
        I != E; ++I) {
    for (MachineBasicBlock::const_iterator MI = I->begin(), E = I->end();
          MI != E; ++MI) {
      if (!MI->isLabel()) {
        continue;
      }

      MCSymbol *BeginLabel = MI->getOperand(0).getMCSymbol();
      assert(BeginLabel && "Invalid label!");

      RangeMapType::iterator L = PadMap.find(BeginLabel);

      if (L == PadMap.end())
        continue;

      PadRange P = L->second;
      const LandingPadInfo *LandingPad = LandingPads[P.PadIndex];

      assert(BeginLabel == LandingPad->BeginLabels[P.RangeIndex] &&
              "Inconsistent landing pad map!");

      // Mono emits one landing pad for each CLR exception clause,
      // and the type info contains the clause index
      assert (LandingPad->TypeIds.size() == 1);
      assert (LandingPad->LandingPadLabel);

      LastLabel = LandingPad->EndLabels[P.RangeIndex];
      CallSiteEntry Site = {BeginLabel, LastLabel,
							LandingPad->LandingPadLabel, LandingPad->TypeIds [0]};

      assert(Site.BeginLabel && Site.EndLabel && Site.PadLabel &&
              "Invalid landing pad!");

      // Try to merge with the previous call-site.
      if (CallSites.size()) {
        CallSiteEntry &Prev = CallSites.back();
        if (Site.PadLabel == Prev.PadLabel && Site.TypeID == Prev.TypeID) {
          // Extend the range of the previous entry.
          Prev.EndLabel = Site.EndLabel;
          continue;
        }
      }

      // Otherwise, create a new call-site.
      CallSites.push_back(Site);
    }
  }

  // Begin the exception table.
  JCE->emitAlignmentWithFill(4, 0);

  unsigned char* DwarfExceptionTable = (unsigned char*)JCE->getCurrentPCValue();

  // Keep this in sync with DwarfMonoException::EmitMonoLSDA ()

  // Emit the header.
  JCE->emitULEB128Bytes(0x4d4fef4f);
  JCE->emitULEB128Bytes(1);

  if (ThisSlot != -1) {
    // Emit 'this' location
    unsigned FrameReg;
    int Offset = MF->getTarget ().getFrameLowering ()->getFrameIndexReference (*MF, ThisSlot, FrameReg);
    FrameReg = MF->getTarget ().getRegisterInfo ()->getDwarfRegNum (FrameReg, true);

	JCE->emitByte(dwarf::DW_EH_PE_udata4);
    JCE->emitByte((int)dwarf::DW_OP_bregx);
    JCE->emitULEB128Bytes(FrameReg);
    JCE->emitSLEB128Bytes(Offset);
  } else {
    JCE->emitByte(dwarf::DW_EH_PE_omit);
  }

  JCE->emitULEB128Bytes (CallSites.size ());
  JCE->emitAlignmentWithFill(4, 0);

  // Emit the landing pad site information.
  for (unsigned i = 0; i < CallSites.size(); ++i) {
    CallSiteEntry &S = CallSites[i];
    intptr_t BeginLabelPtr = 0;
    intptr_t EndLabelPtr = 0;

    if (!S.BeginLabel) {
      BeginLabelPtr = (intptr_t)StartFunction;
      JCE->emitInt32(0);
    } else {
      BeginLabelPtr = JCE->getLabelAddress(S.BeginLabel);
      JCE->emitInt32(BeginLabelPtr - (intptr_t)StartFunction);
    }

    if (!S.EndLabel)
      EndLabelPtr = (intptr_t)EndFunction;
    else
      EndLabelPtr = JCE->getLabelAddress(S.EndLabel);

    JCE->emitInt32(EndLabelPtr - BeginLabelPtr);

    if (!S.PadLabel) {
      JCE->emitInt32(0);
    } else {
      unsigned PadLabelPtr = JCE->getLabelAddress(S.PadLabel);
      JCE->emitInt32(PadLabelPtr - (intptr_t)StartFunction);
    }

	unsigned int TypeID = S.TypeID;
    assert (TypeID > 0 && TypeID <= TypeInfos.size ());
    const GlobalVariable *GV = TypeInfos[TypeID - 1];
    assert (GV);
    int ClauseIndex = *(int*)Jit.getOrEmitGlobalVariable(GV);
    JCE->emitInt32 (ClauseIndex);
  }

  JCE->emitAlignmentWithFill(4, 0);

  return DwarfExceptionTable;
}

unsigned char*
JITDwarfEmitter::EmitCommonEHFrame(const Function* Personality) const {
  unsigned PointerSize = TD->getPointerSize();
  int stackGrowth = stackGrowthDirection == TargetFrameLowering::StackGrowsUp ?
          PointerSize : -PointerSize;

  unsigned char* StartCommonPtr = (unsigned char*)JCE->getCurrentPCValue();
  // EH Common Frame header
  JCE->allocateSpace(4, 0);
  unsigned char* FrameCommonBeginPtr = (unsigned char*)JCE->getCurrentPCValue();
  JCE->emitInt32((int)0);
  JCE->emitByte(dwarf::DW_CIE_VERSION);
  JCE->emitString(Personality ? "zPLR" : "zR");
  JCE->emitULEB128Bytes(1);
  JCE->emitSLEB128Bytes(stackGrowth);
  JCE->emitByte(RI->getDwarfRegNum(RI->getRARegister(), true));

  if (Personality) {
    // Augmentation Size: 3 small ULEBs of one byte each, and the personality
    // function which size is PointerSize.
    JCE->emitULEB128Bytes(3 + PointerSize);

    // We set the encoding of the personality as direct encoding because we use
    // the function pointer. The encoding is not relative because the current
    // PC value may be bigger than the personality function pointer.
    if (PointerSize == 4) {
      JCE->emitByte(dwarf::DW_EH_PE_sdata4);
      JCE->emitInt32(((intptr_t)Jit.getPointerToGlobal(Personality)));
    } else {
      JCE->emitByte(dwarf::DW_EH_PE_sdata8);
      JCE->emitInt64(((intptr_t)Jit.getPointerToGlobal(Personality)));
    }

    // LSDA encoding: This must match the encoding used in EmitEHFrame ()
    if (PointerSize == 4)
      JCE->emitULEB128Bytes(dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4);
    else
      JCE->emitULEB128Bytes(dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata8);
    JCE->emitULEB128Bytes(dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4);
  } else {
    JCE->emitULEB128Bytes(1);
    JCE->emitULEB128Bytes(dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4);
  }

  EmitCFIInstructions(0, MAI->getInitialFrameState());

  JCE->emitAlignmentWithFill(PointerSize, dwarf::DW_CFA_nop);

  JCE->emitInt32At((uintptr_t*)StartCommonPtr,
                   (uintptr_t)((unsigned char*)JCE->getCurrentPCValue() -
                               FrameCommonBeginPtr));

  return StartCommonPtr;
}


unsigned char*
JITDwarfEmitter::EmitEHFrame(const Function* Personality,
                             unsigned char* StartCommonPtr,
                             unsigned char* StartFunction,
                             unsigned char* EndFunction,
                             unsigned char* ExceptionTable) const {
  unsigned PointerSize = TD->getPointerSize();

  // EH frame header.
  unsigned char* StartEHPtr = (unsigned char*)JCE->getCurrentPCValue();
  JCE->allocateSpace(4, 0);
  unsigned char* FrameBeginPtr = (unsigned char*)JCE->getCurrentPCValue();
  // FDE CIE Offset
  JCE->emitInt32(FrameBeginPtr - StartCommonPtr);
  JCE->emitInt32(StartFunction - (unsigned char*)JCE->getCurrentPCValue());
  JCE->emitInt32(EndFunction - StartFunction);

  // If there is a personality and landing pads then point to the language
  // specific data area in the exception table.
  if (ExceptionTable) {
    JCE->emitULEB128Bytes(PointerSize == 4 ? 4 : 8);

    if (PointerSize == 4) {
      if (ExceptionTable)
        JCE->emitInt32(ExceptionTable-(unsigned char*)JCE->getCurrentPCValue());
      else
        JCE->emitInt32((int)0);
    } else {
      if (ExceptionTable)
        JCE->emitInt64(ExceptionTable-(unsigned char*)JCE->getCurrentPCValue());
      else
        JCE->emitInt64((int)0);
    }
  } else {
    JCE->emitULEB128Bytes(0);
  }

  EmitCFIInstructions((intptr_t)StartFunction, MMI->getFrameInstructions());

  JCE->emitAlignmentWithFill(PointerSize, dwarf::DW_CFA_nop);

  // Indicate the size of the table
  JCE->emitInt32At((uintptr_t*)StartEHPtr,
                   (uintptr_t)((unsigned char*)JCE->getCurrentPCValue() -
                               StartEHPtr));

  // Double zeroes for the unwind runtime
  if (PointerSize == 8) {
    JCE->emitInt64(0);
    JCE->emitInt64(0);
  } else {
    JCE->emitInt32(0);
    JCE->emitInt32(0);
  }

  return StartEHPtr;
}
