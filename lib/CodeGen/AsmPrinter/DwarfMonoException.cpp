//===-- CodeGen/AsmPrinter/DwarfMonoException.cpp - Dwarf Exception Impl ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to emit Mono specific exception handling tables.
// It is based on code in DwarfException.cpp and MCDwarf.cpp.
//
//===----------------------------------------------------------------------===//

#include "DwarfException.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MonoMachineFunctionInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
using namespace llvm;

// FIXME:
static cl::opt<bool> DisableGNUEH("disable-gnu-eh-frame", cl::NotHidden,
                                  cl::desc("Disable generation of GNU .eh_frame"));

static inline const MCExpr *MakeStartMinusEndExpr(const MCStreamer &MCOS,
                                                  const MCSymbol &Start,
                                                  const MCSymbol &End,
                                                  int IntVal) {
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  const MCExpr *Res =
    MCSymbolRefExpr::Create(&End, Variant, MCOS.getContext());
  const MCExpr *RHS =
    MCSymbolRefExpr::Create(&Start, Variant, MCOS.getContext());
  const MCExpr *Res1 =
    MCBinaryExpr::Create(MCBinaryExpr::Sub, Res, RHS, MCOS.getContext());
  const MCExpr *Res2 =
    MCConstantExpr::Create(IntVal, MCOS.getContext());
  const MCExpr *Res3 =
    MCBinaryExpr::Create(MCBinaryExpr::Sub, Res1, Res2, MCOS.getContext());
  return Res3;
}

void EmitCFIInstruction(MCStreamer &Streamer,
                        const MCCFIInstruction &Instr,
                        int &CFAOffset, int DataAlignmentFactor) {
  // Same as MCDwarf::EmitCFIInstruction ()
  // FIXME: Unify
  int dataAlignmentFactor = DataAlignmentFactor;
  bool VerboseAsm = Streamer.isVerboseAsm();

  switch (Instr.getOperation()) {
  case MCCFIInstruction::OpWindowSave: {
    Streamer.EmitIntValue(dwarf::DW_CFA_GNU_window_save, 1);
    return;
  }
  case MCCFIInstruction::OpUndefined: {
    unsigned Reg = Instr.getRegister();
    if (VerboseAsm) {
      Streamer.AddComment("DW_CFA_undefined");
      Streamer.AddComment(Twine("Reg ") + Twine(Reg));
    }
    Streamer.EmitIntValue(dwarf::DW_CFA_undefined, 1);
    Streamer.EmitULEB128IntValue(Reg);
    return;
  }
  case MCCFIInstruction::OpAdjustCfaOffset:
  case MCCFIInstruction::OpDefCfaOffset: {
    const bool IsRelative =
      Instr.getOperation() == MCCFIInstruction::OpAdjustCfaOffset;

    if (VerboseAsm)
      Streamer.AddComment("DW_CFA_def_cfa_offset");
    Streamer.EmitIntValue(dwarf::DW_CFA_def_cfa_offset, 1);

    if (IsRelative)
      CFAOffset += Instr.getOffset();
    else
      // The backends pass in a negative value,
      // then createDefCfaOffset () negates it
      CFAOffset = Instr.getOffset();

    if (VerboseAsm)
      Streamer.AddComment(Twine("Offset " + Twine(CFAOffset)));
    Streamer.EmitULEB128IntValue(CFAOffset);

    return;
  }
  case MCCFIInstruction::OpDefCfa: {
    if (VerboseAsm)
      Streamer.AddComment("DW_CFA_def_cfa");
    Streamer.EmitIntValue(dwarf::DW_CFA_def_cfa, 1);

    if (VerboseAsm)
      Streamer.AddComment(Twine("Reg ") + Twine(Instr.getRegister()));
    Streamer.EmitULEB128IntValue(Instr.getRegister());

    // createX86MCAsmInfo () passes in a positive value, which is
    // negated by createDefCfa ()
    CFAOffset = -Instr.getOffset();

    if (VerboseAsm)
      Streamer.AddComment(Twine("Offset " + Twine(CFAOffset)));
    Streamer.EmitULEB128IntValue(CFAOffset);

    return;
  }

  case MCCFIInstruction::OpDefCfaRegister: {
    if (VerboseAsm)
      Streamer.AddComment("DW_CFA_def_cfa_register");
    Streamer.EmitIntValue(dwarf::DW_CFA_def_cfa_register, 1);

    if (VerboseAsm)
      Streamer.AddComment(Twine("Reg ") + Twine(Instr.getRegister()));
    Streamer.EmitULEB128IntValue(Instr.getRegister());

    return;
  }

  case MCCFIInstruction::OpOffset:
  case MCCFIInstruction::OpRelOffset: {
    const bool IsRelative =
      Instr.getOperation() == MCCFIInstruction::OpRelOffset;

    unsigned Reg = Instr.getRegister();
    int Offset = Instr.getOffset();
    if (IsRelative)
      Offset -= CFAOffset;
    Offset = Offset / dataAlignmentFactor;

    if (Offset < 0) {
      if (VerboseAsm) Streamer.AddComment("DW_CFA_offset_extended_sf");
      Streamer.EmitIntValue(dwarf::DW_CFA_offset_extended_sf, 1);
      if (VerboseAsm) Streamer.AddComment(Twine("Reg ") + Twine(Reg));
      Streamer.EmitULEB128IntValue(Reg);
      if (VerboseAsm) Streamer.AddComment(Twine("Offset ") + Twine(Offset));
      Streamer.EmitSLEB128IntValue(Offset);
    } else if (Reg < 64) {
      if (VerboseAsm) Streamer.AddComment(Twine("DW_CFA_offset + Reg(") +
                                          Twine(Reg) + ")");
      Streamer.EmitIntValue(dwarf::DW_CFA_offset + Reg, 1);
      if (VerboseAsm) Streamer.AddComment(Twine("Offset ") + Twine(Offset));
      Streamer.EmitULEB128IntValue(Offset);
    } else {
      if (VerboseAsm) Streamer.AddComment("DW_CFA_offset_extended");
      Streamer.EmitIntValue(dwarf::DW_CFA_offset_extended, 1);
      if (VerboseAsm) Streamer.AddComment(Twine("Reg ") + Twine(Reg));
      Streamer.EmitULEB128IntValue(Reg);
      if (VerboseAsm) Streamer.AddComment(Twine("Offset ") + Twine(Offset));
      Streamer.EmitULEB128IntValue(Offset);
    }
    return;
  }
  case MCCFIInstruction::OpRememberState:
    if (VerboseAsm) Streamer.AddComment("DW_CFA_remember_state");
    Streamer.EmitIntValue(dwarf::DW_CFA_remember_state, 1);
    return;
  case MCCFIInstruction::OpRestoreState:
    if (VerboseAsm) Streamer.AddComment("DW_CFA_restore_state");
    Streamer.EmitIntValue(dwarf::DW_CFA_restore_state, 1);
    return;
  case MCCFIInstruction::OpSameValue: {
    unsigned Reg = Instr.getRegister();
    if (VerboseAsm) Streamer.AddComment("DW_CFA_same_value");
    Streamer.EmitIntValue(dwarf::DW_CFA_same_value, 1);
    if (VerboseAsm) Streamer.AddComment(Twine("Reg ") + Twine(Reg));
    Streamer.EmitULEB128IntValue(Reg);
    return;
  }
  case MCCFIInstruction::OpRestore: {
    unsigned Reg = Instr.getRegister();
    if (VerboseAsm) {
      Streamer.AddComment("DW_CFA_restore");
      Streamer.AddComment(Twine("Reg ") + Twine(Reg));
    }
    Streamer.EmitIntValue(dwarf::DW_CFA_restore | Reg, 1);
    return;
  }
  case MCCFIInstruction::OpEscape:
    if (VerboseAsm) Streamer.AddComment("Escape bytes");
    Streamer.EmitBytes(Instr.getValues());
    return;
  case MCCFIInstruction::OpRegister:
	  llvm_unreachable("Unhandled case in switch");	
	  return;
  }
  llvm_unreachable("Unhandled case in switch");
}

/// EmitFrameMoves - Emit frame instructions to describe the layout of the
/// frame.
void EmitCFIInstructions(MCStreamer &streamer,
                         const std::vector<MCCFIInstruction> &Instrs,
                         MCSymbol *BaseLabel,
                         int &CFAOffset,
                         int DataAlignmentFactor) {
  for (unsigned i = 0, N = Instrs.size(); i < N; ++i) {
    const MCCFIInstruction &Instr = Instrs[i];
    MCSymbol *Label = Instr.getLabel();
    // Throw out move if the label is invalid.
    //if (Label && !Label->isDefined()) continue; // Not emitted, in dead code.

    // Advance row if new location.
    if (BaseLabel && Label) {
      MCSymbol *ThisSym = Label;
      if (ThisSym != BaseLabel) {
        streamer.AddComment ("cfa_advance");
        streamer.EmitDwarfAdvanceFrameAddr(BaseLabel, ThisSym);
        BaseLabel = ThisSym;
      }
    }

    EmitCFIInstruction(streamer, Instr, CFAOffset, DataAlignmentFactor);
  }
}

/// PrepareMonoLSDA - Collect information needed by EmitMonoLSDA
///
///   This function collects information available only during EndFunction which is needed
/// by EmitMonoLSDA and stores it into EHFrameInfo. It is the same as the
/// beginning of EmitExceptionTable.
///
void DwarfMonoException::PrepareMonoLSDA(FunctionEHFrameInfo *EHFrameInfo) {
  const std::vector<const GlobalVariable *> &TypeInfos = MMI->getTypeInfos();
  const std::vector<LandingPadInfo> &PadInfos = MMI->getLandingPads();
  const MachineFunction *MF = Asm->MF;

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());

  for (unsigned i = 0, N = PadInfos.size(); i != N; ++i)
    LandingPads.push_back(&PadInfos[i]);

  std::sort(LandingPads.begin(), LandingPads.end(), PadLT);

#if 0
  // Compute the actions table and gather the first action index for each
  // landing pad site.
  SmallVector<ActionEntry, 32> Actions;
  SmallVector<unsigned, 64> FirstActions;
  ComputeActionsTable(LandingPads, Actions, FirstActions);
#endif

  // Invokes and nounwind calls have entries in PadMap (due to being bracketed
  // by try-range labels when lowered).  Ordinary calls do not, so appropriate
  // try-ranges for them need be deduced when using DWARF exception handling.
  RangeMapType PadMap;
  for (unsigned i = 0, N = LandingPads.size(); i != N; ++i) {
    const LandingPadInfo *LandingPad = LandingPads[i];
    for (unsigned j = 0, E = LandingPad->BeginLabels.size(); j != E; ++j) {
      MCSymbol *BeginLabel = LandingPad->BeginLabels[j];
      assert(!PadMap.count(BeginLabel) && "Duplicate landing pad labels!");
      PadRange P = { i, j };
      PadMap[BeginLabel] = P;
    }
  }

  // Compute the call-site table.
  SmallVector<MonoCallSiteEntry, 64> CallSites;
  //ComputeCallSiteTable(CallSites, PadMap, LandingPads, FirstActions);

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
      MonoCallSiteEntry Site = {BeginLabel, LastLabel,
							LandingPad->LandingPadLabel, LandingPad->TypeIds [0]};

      assert(Site.BeginLabel && Site.EndLabel && Site.PadLabel &&
              "Invalid landing pad!");

	  // FIXME: This doesn't work because it includes ranges outside clauses
#if 0
      // Try to merge with the previous call-site.
      if (CallSites.size()) {
        MonoCallSiteEntry &Prev = CallSites.back();
        if (Site.PadLabel == Prev.PadLabel && Site.TypeID == Prev.TypeID) {
          // Extend the range of the previous entry.
          Prev.EndLabel = Site.EndLabel;
          continue;
        }
      }
#endif

      // Otherwise, create a new call-site.
      CallSites.push_back(Site);
    }
  }

  //
  // Compute a mapping from method names to their AOT method index
  //
  if (FuncIndexes.size () == 0) {
    const Module *m = MMI->getModule ();
    NamedMDNode *indexes = m->getNamedMetadata ("mono.function_indexes");
	if (indexes) {
      for (unsigned int i = 0; i < indexes->getNumOperands (); ++i) {
        MDNode *n = indexes->getOperand (i);
        MDString *s = (MDString*)n->getOperand (0);
        ConstantInt *idx = (ConstantInt*)n->getOperand (1);
        FuncIndexes.GetOrCreateValue (s->getString (), (int)idx->getLimitedValue () + 1);
      }
    }
  }

  MonoEHFrameInfo *MonoEH = &EHFrameInfo->MonoEH;

  // Save information for EmitMonoLSDA
  MonoEH->MF = Asm->MF;
  MonoEH->FunctionNumber = Asm->getFunctionNumber();
  MonoEH->CallSites.insert(MonoEH->CallSites.begin(), CallSites.begin(), CallSites.end());
  MonoEH->TypeInfos = TypeInfos;
  MonoEH->PadInfos = PadInfos;
  MonoEH->MonoMethodIdx = FuncIndexes.lookup (Asm->MF->getFunction ()->getName ()) - 1;
  //outs()<<"A:"<<Asm->MF->getFunction()->getName() << " " << MonoEH->MonoMethodIdx << "\n";

  int ThisSlot = Asm->MF->getMonoInfo()->getThisStackSlot();

  if (ThisSlot != -1) {
    unsigned FrameReg;
    MonoEH->ThisOffset = Asm->MF->getTarget ().getFrameLowering ()->getFrameIndexReference (*Asm->MF, ThisSlot, FrameReg);
    MonoEH->FrameReg = Asm->MF->getTarget ().getRegisterInfo ()->getDwarfRegNum (FrameReg, true);
  } else {
    MonoEH->FrameReg = -1;
  }
}

/// EmitMonoLSDA - Mono's version of EmitExceptionTable
///
/// The code below is a modified/simplified version of DwarfException::EmitExceptionTable()
/// We emit the information inline instead of into a separate section.
///
void DwarfMonoException::EmitMonoLSDA(const FunctionEHFrameInfo *EFI) {
  // Load saved information from EHFrameInfo
  const MonoEHFrameInfo *MonoEH = &EFI->MonoEH;
  const std::vector<const GlobalVariable *> &TypeInfos = MonoEH->TypeInfos;
  const std::vector<LandingPadInfo> &PadInfos = MonoEH->PadInfos;
  const std::vector<MonoCallSiteEntry> CallSites = MonoEH->CallSites;
  int FunctionNumber = MonoEH->FunctionNumber;
  int FrameReg = MonoEH->FrameReg;
  int ThisOffset = MonoEH->ThisOffset;

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());

  for (unsigned i = 0, N = PadInfos.size(); i != N; ++i)
    LandingPads.push_back(&PadInfos[i]);

  std::sort(LandingPads.begin(), LandingPads.end(), PadLT);

  // Invokes and nounwind calls have entries in PadMap (due to being bracketed
  // by try-range labels when lowered).  Ordinary calls do not, so appropriate
  // try-ranges for them need be deduced when using DWARF exception handling.
  RangeMapType PadMap;
  for (unsigned i = 0, N = LandingPads.size(); i != N; ++i) {
    const LandingPadInfo *LandingPad = LandingPads[i];
    for (unsigned j = 0, E = LandingPad->BeginLabels.size(); j != E; ++j) {
      MCSymbol *BeginLabel = LandingPad->BeginLabels[j];
      assert(!PadMap.count(BeginLabel) && "Duplicate landing pad labels!");
      PadRange P = { i, j };
      PadMap[BeginLabel] = P;
    }
  }

  assert(Asm->MAI->getExceptionHandlingType() == ExceptionHandling::DwarfCFI);

  // The type_info itself is emitted
  int TTypeEncoding = dwarf::DW_EH_PE_udata4;

  // Emit the LSDA.
  // Keep this in sync with JITDwarfEmitter::EmitExceptionTable ()
  Asm->EmitULEB128(0x4d4fef4f, "MONO Magic", 0);
  Asm->EmitULEB128(1, "Version", 0);

  // Emit the LSDA header.
  if (FrameReg != -1) {
    Asm->EmitEncodingByte(dwarf::DW_EH_PE_udata4, "This encoding");

    // Emit 'this' location
    Asm->OutStreamer.AddComment("bregx");
    Asm->EmitInt8((int)dwarf::DW_OP_bregx);
    Asm->EmitULEB128(FrameReg, "Base reg");
    Asm->EmitSLEB128(ThisOffset, "Offset");
  } else {
    Asm->EmitEncodingByte(dwarf::DW_EH_PE_omit, "@This encoding");
  }

  Asm->EmitULEB128 (CallSites.size (), "Number of call sites");
  Asm->EmitAlignment(2);
  for (std::vector<MonoCallSiteEntry>::const_iterator
         I = CallSites.begin(), E = CallSites.end(); I != E; ++I) {
    const MonoCallSiteEntry &S = *I;
      
    MCSymbol *EHFuncBeginSym =
      Asm->GetTempSymbol("eh_func_begin", FunctionNumber);
      
    MCSymbol *BeginLabel = S.BeginLabel;
    if (BeginLabel == 0)
      BeginLabel = EHFuncBeginSym;
    MCSymbol *EndLabel = S.EndLabel;
    if (EndLabel == 0)
      EndLabel = Asm->GetTempSymbol("eh_func_end", FunctionNumber);
        
    Asm->OutStreamer.AddComment("Region start");
    Asm->EmitLabelDifference(BeginLabel, EHFuncBeginSym, 4);
      
    Asm->OutStreamer.AddComment("Region length");
    Asm->EmitLabelDifference(EndLabel, BeginLabel, 4);

    Asm->OutStreamer.AddComment("Landing pad");
    if (!S.PadLabel)
      Asm->OutStreamer.EmitIntValue(0, 4);
    else
      Asm->EmitLabelDifference(S.PadLabel, EHFuncBeginSym, 4);

	unsigned int TypeID = S.TypeID;
    assert (TypeID > 0 && TypeID <= TypeInfos.size ());
    const GlobalVariable *GV = TypeInfos[TypeID - 1];
    assert (GV);

    //
    // Mono typeinfos are simple constant integers. Emit the constant itself.
    //
    assert(GV);
    const ConstantInt *ci = dyn_cast<ConstantInt>(GV->getInitializer());

    Asm->OutStreamer.AddComment("TypeInfo");
    Asm->OutStreamer.EmitIntValue(ci->getZExtValue(),Asm->GetSizeOfEncodedValue(TTypeEncoding));
  }
}

// EmitMonoEHFrame - Emit Mono specific exception handling tables
void DwarfMonoException::EmitMonoEHFrame(const Function *Personality)
{
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  unsigned FuncAddrEncoding = TLOF.getMonoEHTableEncoding ();

  // Size and sign of stack growth.
  int stackGrowth = Asm->getDataLayout().getPointerSize();
  if (Asm->TM.getFrameLowering()->getStackGrowthDirection() ==
      TargetFrameLowering::StackGrowsDown)
    stackGrowth *= -1;

  MCStreamer& Streamer = Asm->OutStreamer;

  //
  // The Mono runtime obtains EH info for LLVM JITted code by decoding the GNU EH frame
  // created by LLVM. For static compilation, this has certain problems:
  // - .eh_frame resides in a different segment, which makes it impossible to read it at
  // runtime on some platforms, like the iphone
  // - the iphone linker can't generate .eh_frame_hdr
  // - the table is very verbose, a single FDE is about 20-24 bytes on arm, negating any
  // code size gains obtained by using LLVM.
  // - the information is spread across three data structures: the eh_frame, the LSDA and
  //   type_info, which are potentially in different sections/segments, leading to relocation/
  //   linking problems.
  // An alternative would be using the official ARM EH tables. This has problems too:
  // - it is also in a separate section
  // - it is very badly specified
  // - it needs GNU assembler/linker support to work.
  //
  // To solve these problems, we generate our own custom EH tables:
  // - the table is in the rodata segment, pointed to by a local symbol.
  // - it contains an .eh_frame_hdr style lookup table.
  // - the LSDA and type_info are embedded into the FDE entries.
  //
  // http://refspecs.freestandards.org/LSB_1.3.0/gLSB/gLSB/ehframehdr.html
  //
  // It is hard to get smaller tables without assembler support, since we can't encode
  // offsets in less that 4 bytes, can't encode information into the upper bits of offsets etc.
  //

  // Can't use rodata as the symbols we reference are in the text segment
  Streamer.SwitchSection(TLOF.getTextSection());

  MCSymbol *EHFrameHdrSym =
	  Asm->OutContext.GetOrCreateSymbol(Twine("mono_eh_frame"));
  MCSymbol *EHFrameEndSym = Asm->GetTempSymbol ("mono_eh_frame_end");

  Asm->EmitAlignment(4);
  Streamer.EmitLabel(EHFrameHdrSym);
  const MCExpr *Length = MakeStartMinusEndExpr(Streamer, *EHFrameHdrSym,
                                               *EHFrameEndSym, 0);
  if (Asm->MAI->hasDotTypeDotSizeDirective()) {
    Streamer.EmitELFSize(EHFrameHdrSym, Length);
    Streamer.EmitSymbolAttribute(EHFrameHdrSym, MCSA_ELF_TypeObject);
  }

  // Header

  Streamer.AddComment("version");
  Asm->OutStreamer.EmitIntValue(3, 1);
  Asm->OutStreamer.AddComment ("func addr encoding");
  Asm->OutStreamer.EmitIntValue (FuncAddrEncoding, 1);

  // Search table
  Asm->EmitAlignment(2);
  Streamer.AddComment("fde_count");
  Streamer.EmitIntValue (EHFrames.size(), 4);
  for (std::vector<FunctionEHFrameInfo>::iterator
		   I = EHFrames.begin(), E = EHFrames.end(); I != E; ++I) {
	  const FunctionEHFrameInfo &EHFrameInfo = *I;

      //
      // Instead of the function address, we emit the mono method index, which is easier to encode.
      //
      Streamer.AddComment("mono method idx");
      Streamer.EmitIntValue (EHFrameInfo.MonoEH.MonoMethodIdx, 4);
	  MCSymbol *FDEBeginSym = Asm->GetTempSymbol ("mono_eh_func_begin", EHFrameInfo.Number);
	  Asm->EmitLabelDifference(FDEBeginSym, EHFrameHdrSym, 4);
  }
  // Emit a last entry to simplify binary searches and to enable the computation of
  // the size of the last function/FDE entry
  if (EHFrames.size() == 0) {
	  Streamer.EmitIntValue (-1, 4);
	  Asm->EmitLabelDifference(EHFrameHdrSym, EHFrameHdrSym, 4);
  } else {
	  // Emit the size of the last function, since it cannot be computed using the next table entry
	  MCSymbol *Sym1 = Asm->GetTempSymbol("eh_func_begin", EHFrames.size() - 1);
	  MCSymbol *Sym2 = Asm->GetTempSymbol("eh_func_end", EHFrames.size() - 1);
	  Asm->EmitLabelDifference(Sym2, Sym1, 4);
	  MCSymbol *Sym3 = Asm->GetTempSymbol ("mono_eh_frame_end");
	  Asm->EmitLabelDifference(Sym3, EHFrameHdrSym, 4);
  }

  // CIE
  // This comes right after the search table
  Asm->EmitULEB128(1, "CIE Code Alignment Factor");
  Asm->EmitSLEB128(stackGrowth, "CIE Data Alignment Factor");
  Streamer.AddComment("CIE Return Address Column");
  const TargetRegisterInfo *RI = Asm->TM.getRegisterInfo();
  Asm->EmitInt8(RI->getDwarfRegNum(RI->getRARegister(), true));

  Asm->EmitEncodingByte(dwarf::DW_EH_PE_omit, "Personality");

  int CFAOffset = 0;

  int dataAlignmentFactor = stackGrowth;

  // Initial CIE program
  EmitCFIInstructions(Streamer, Streamer.getContext().getAsmInfo()->getInitialFrameState(), NULL, CFAOffset, dataAlignmentFactor);

  int CIECFAOffset = CFAOffset;

  // FDEs
  Streamer.AddBlankLine();
  for (std::vector<FunctionEHFrameInfo>::iterator
		   I = EHFrames.begin(), E = EHFrames.end(); I != E; ++I) {
	  const FunctionEHFrameInfo &EHFrameInfo = *I;
      int Index = EHFrameInfo.Number;

	  MCSymbol *FDEBeginSym = Asm->GetTempSymbol ("mono_eh_func_begin", Index);
      Streamer.EmitLabel(FDEBeginSym);

      // No need for length, CIE, PC begin, PC range, alignment

      // Emit augmentation
      if (EHFrameInfo.hasLandingPads) {
        // Need an extra has_augmentation field as the augmentation size is always encoded
        // in 4 bytes
        Asm->EmitULEB128(1, "Has augmentation");

        Streamer.AddComment("Augmentation size");
        Asm->EmitLabelDifference(Asm->GetTempSymbol("mono_fde_aug_end", Index),
                                 Asm->GetTempSymbol("mono_fde_aug_begin", Index),
                                 4);

        Streamer.EmitLabel(Asm->GetTempSymbol("mono_fde_aug_begin", Index));
        EmitMonoLSDA (&EHFrameInfo);
        Streamer.EmitLabel(Asm->GetTempSymbol("mono_fde_aug_end", Index));
      } else {
        Asm->EmitULEB128(0, "Has augmentation");
      }

      CFAOffset = CIECFAOffset;

      EmitCFIInstructions(Streamer, EHFrameInfo.Instructions, NULL, CFAOffset, dataAlignmentFactor);

      Streamer.AddBlankLine();
  }

  Streamer.EmitLabel(EHFrameEndSym);
}

DwarfMonoException::DwarfMonoException(AsmPrinter *A)
  : DwarfException(A) {}

DwarfMonoException::~DwarfMonoException() {}

void DwarfMonoException::BeginFunction(const MachineFunction *MF)
{
  Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_func_begin",
                                                Asm->getFunctionNumber()));
}

void DwarfMonoException::EndFunction() {
  Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_func_end",
                                                Asm->getFunctionNumber()));

  MMI->TidyLandingPads();

  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  MCSymbol *FunctionEHSym =
    Asm->GetSymbolWithGlobalValueBase(Asm->MF->getFunction(), ".eh",
                                      TLOF.isFunctionEHFrameSymbolPrivate());

  // Save EH frame information
  FunctionEHFrameInfo EHFrameInfo =
    FunctionEHFrameInfo(FunctionEHSym,
                        Asm->getFunctionNumber(),
                        MMI->getPersonalityIndex(),
                        Asm->MF->getFrameInfo()->adjustsStack(),
                        !MMI->getLandingPads().empty(),
                        MMI->getFrameInstructions(),
                        Asm->MF->getFunction());

  PrepareMonoLSDA(&EHFrameInfo);

  EHFrames.push_back(EHFrameInfo);
}

void DwarfMonoException::EndModule() {
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  // Emit references to all used personality functions
  const std::vector<const Function*> &Personalities = MMI->getPersonalities();
  for (size_t i = 0, e = Personalities.size(); i != e; ++i) {
    if (!Personalities[i])
      continue;
    MCSymbol *Sym = TLOF.getSymbol(*Asm->Mang, Personalities[i]);
    TLOF.emitPersonalityValue(Asm->OutStreamer, Asm->TM, Sym);
  }

  EmitMonoEHFrame(Personalities[0]);
}
