//===---*- mode: c++; indent-tabs-mode: nil; c-basic-offset: 2 -*---------===//
//===-- CodeGen/AsmPrinter/MonoException.cpp - Dwarf Exception Impl ------===//
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

#include "MonoException.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLoweringObjectFile.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
using namespace llvm;

//
// The EH tables emitted by this class enable the following functionality:
// - obtaining the GNU EH information for a given method
// - obtaining the DWARF CFI unwind info for a given method
// - obtaining the address of the table itself from user
//   code, ie. it is in the text segment pointed to by
//   a symbol.
//

cl::opt<std::string> MonoEHFrameSymbol("mono-eh-frame-symbol", cl::NotHidden,
                                       cl::desc("Symbol name for the mono eh frame"));

// Emit a CFI instruction in DWARF format
static void
emitCFIInstruction(MCStreamer &Streamer,
                   const MCCFIInstruction &Instr,
                   int &CFAOffset, int DataAlignmentFactor)
{
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

    if (IsRelative) {
      CFAOffset += Instr.getOffset();
    } else {
      // The backends pass in a negative value,
      // then createDefCfaOffset () negates it
      CFAOffset = Instr.getOffset();
      assert(CFAOffset >= 0);
    }

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

    // The backends pass in a negative value,
    // then createDefCfaOffset () negates it
    CFAOffset = Instr.getOffset();
    if (CFAOffset < 0) {
      outs () << CFAOffset << "\n";
      LLVM_BUILTIN_TRAP;
    }
    assert(CFAOffset >= 0);

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
  case MCCFIInstruction::OpGnuArgsSize:
	  llvm_unreachable("Unhandled case in switch");	
	  return;
  }
  llvm_unreachable("Unhandled case in switch");
}

// Emit a list of CFI instructions
static void
emitCFIInstructions(MCStreamer &streamer,
                    const std::vector<MCCFIInstruction> &Instrs,
                    MCSymbol *BaseLabel,
                    const std::vector<MCSymbol*> *Labels,
                    int &CFAOffset,
                    int DataAlignmentFactor)
{
  for (unsigned i = 0, N = Instrs.size(); i < N; ++i) {
    const MCCFIInstruction &Instr = Instrs[i];
    MCSymbol *Label = Labels ? ((*Labels)[i]) : NULL;

    // Advance row if new location.
    if (BaseLabel && Label) {
      MCSymbol *ThisSym = Label;
      if (ThisSym != BaseLabel) {
        streamer.AddComment ("cfa_advance");
        streamer.EmitDwarfAdvanceFrameAddr(BaseLabel, ThisSym);
        BaseLabel = ThisSym;
      }
    }

    emitCFIInstruction(streamer, Instr, CFAOffset, DataAlignmentFactor);
  }
}

MonoException::MonoException(AsmPrinter *A, bool disableGNUEH)
  : EHStreamer(A)
{
  RI = nullptr;
  DisableGNUEH = disableGNUEH;
}

MonoException::~MonoException()
{
}

void
MonoException::beginFunction(const MachineFunction *MF)
{
  EmitFnStart();
  EHLabels.clear();
}

void
MonoException::PrepareMonoLSDA(EHInfo *info)
{
  const MachineFunction *MF = Asm->MF;
  const std::vector<const GlobalValue *> &TypeInfos = MF->getTypeInfos();
  const std::vector<LandingPadInfo> &PadInfos = MF->getLandingPads();

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());

  for (unsigned i = 0, N = PadInfos.size(); i != N; ++i)
    LandingPads.push_back(&PadInfos[i]);

  std::sort(LandingPads.begin(), LandingPads.end(),
          [](const LandingPadInfo *L,
			 const LandingPadInfo *R) { return L->TypeIds < R->TypeIds; });

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

  info->CallSites.insert(info->CallSites.begin(), CallSites.begin(), CallSites.end());
  info->TypeInfos = TypeInfos;
  info->PadInfos = PadInfos;

  int ThisSlot = Asm->MF->getMonoThisSlot();

  if (ThisSlot != -1) {
    unsigned FrameReg;
    info->ThisOffset = Asm->MF->getTarget ().getSubtargetImpl (Asm->MF->getFunction())->getFrameLowering ()->getFrameIndexReference (*Asm->MF, ThisSlot, FrameReg);
    info->FrameReg = Asm->MF->getTarget ().getSubtargetImpl (Asm->MF->getFunction())->getRegisterInfo ()->getDwarfRegNum (FrameReg, true);
  } else {
    info->FrameReg = -1;
  }
}

void
MonoException::EmitFnStart(void)
{
  if (DisableGNUEH && Asm->MAI->getExceptionHandlingType() == ExceptionHandling::ARM)
    static_cast<ARMTargetStreamer*>(Asm->OutStreamer->getTargetStreamer())->emitFnStart();
}

void
MonoException::EmitFnEnd(void)
{
  if (DisableGNUEH && Asm->MAI->getExceptionHandlingType() == ExceptionHandling::ARM)
    static_cast<ARMTargetStreamer*>(Asm->OutStreamer->getTargetStreamer())->emitFnEnd();
}

void
MonoException::endFunction(const MachineFunction *MF)
{
  //
  // Compute a mapping from method names to their AOT method index
  //
  if (FuncIndexes.size () == 0) {
    const Module *m = MMI->getModule ();
    NamedMDNode *indexes = m->getNamedMetadata ("mono.function_indexes");
	if (indexes) {
      for (unsigned int i = 0; i < indexes->getNumOperands (); ++i) {
        MDNode *n = indexes->getOperand (i);
        MDString *s = cast<MDString>(n->getOperand (0));
        auto *idx = mdconst::dyn_extract<ConstantInt>(n->getOperand (1));
        FuncIndexes.insert (std::make_pair(s->getString (), (int)idx->getLimitedValue () + 1));
      }
    }
  }

  // Remember the register info
  RI = MF->getSubtarget().getRegisterInfo();

  MachineFunction *NonConstMF = const_cast<MachineFunction*>(MF);
  NonConstMF->tidyLandingPads();

  int monoMethodIdx = FuncIndexes.lookup (Asm->MF->getFunction ().getName ()) - 1;

  if (monoMethodIdx == -1) {
    EmitFnEnd ();
    return;
  }

  //outs () << "D: " << Asm->MF->getFunction()->getName() << " " << monoMethodIdx << "\n";

  // Save information for use by endModule ()
  EHInfo info;

  info.FunctionNumber = Asm->getFunctionNumber();
  info.BeginSym = Asm->getFunctionBegin ();
  info.EndSym = Asm->getFunctionEnd ();
  info.EHLabels = EHLabels;
  info.MonoMethodIdx = monoMethodIdx;
  info.HasLandingPads = !MF->getLandingPads().empty();
  info.Instructions = MF->getFrameInstructions();
  assert (info.Instructions.size () == info.EHLabels.size());

  if (DisableGNUEH)
    /* ARMAsmPrinter generates references to this */
    Asm->OutStreamer->EmitLabel(Asm->getCurExceptionSym());

  PrepareMonoLSDA(&info);

  Frames.push_back(info);
  EHLabels.clear();

  EmitFnEnd ();
}

/// EmitMonoLSDA - Mono's version of EmitExceptionTable
///
/// The code below is a modified/simplified version of DwarfException::EmitExceptionTable()
/// We emit the information inline instead of into a separate section.
///
void
MonoException::EmitMonoLSDA(const EHInfo *info)
{
  // Load saved information from EHFrameInfo
  const std::vector<const GlobalValue *> &TypeInfos = info->TypeInfos;
  const std::vector<LandingPadInfo> &PadInfos = info->PadInfos;
  const std::vector<MonoCallSiteEntry> CallSites = info->CallSites;
  int FrameReg = info->FrameReg;
  int ThisOffset = info->ThisOffset;

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());

  for (unsigned i = 0, N = PadInfos.size(); i != N; ++i)
    LandingPads.push_back(&PadInfos[i]);

  std::sort(LandingPads.begin(), LandingPads.end(),
          [](const LandingPadInfo *L,
			 const LandingPadInfo *R) { return L->TypeIds < R->TypeIds; });

  // The type_info itself is emitted
  int TTypeEncoding = dwarf::DW_EH_PE_udata4;

  // Emit the LSDA.
  // Keep this in sync with JITDwarfEmitter::EmitExceptionTable ()
  Asm->EmitULEB128(0x4d4fef4f, "MONO Magic");
  Asm->EmitULEB128(1, "Version");

  // Emit the LSDA header.
  if (FrameReg != -1) {
    Asm->EmitEncodingByte(dwarf::DW_EH_PE_udata4, "This encoding");

    // Emit 'this' location
    Asm->OutStreamer->AddComment("bregx");
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
      
    MCSymbol *EHFuncBeginSym = info->BeginSym;
      
    MCSymbol *BeginLabel = S.BeginLabel;
    if (BeginLabel == 0)
      BeginLabel = EHFuncBeginSym;
    MCSymbol *EndLabel = S.EndLabel;
    if (EndLabel == 0)
      EndLabel = info->EndSym;
        
    Asm->OutStreamer->AddComment("Region start");
    Asm->EmitLabelDifference(BeginLabel, EHFuncBeginSym, 4);
      
    Asm->OutStreamer->AddComment("Region length");
    Asm->EmitLabelDifference(EndLabel, BeginLabel, 4);

    Asm->OutStreamer->AddComment("Landing pad");
    if (!S.PadLabel)
      Asm->OutStreamer->EmitIntValue(0, 4);
    else
      Asm->EmitLabelDifference(S.PadLabel, EHFuncBeginSym, 4);

	unsigned int TypeID = S.TypeID;
    assert (TypeID > 0 && TypeID <= TypeInfos.size ());
    const GlobalVariable *GV = dyn_cast<GlobalVariable>(TypeInfos[TypeID - 1]);
    assert (GV);

    //
    // Mono typeinfos are simple constant integers. Emit the constant itself.
    //
    assert(GV);
    const ConstantInt *ci = dyn_cast<ConstantInt>(GV->getInitializer());

    Asm->OutStreamer->AddComment("TypeInfo");
    Asm->OutStreamer->EmitIntValue(ci->getZExtValue(),Asm->GetSizeOfEncodedValue(TTypeEncoding));
  }
}

void
MonoException::endModule()
{
  const TargetLoweringObjectFile &tlof = Asm->getObjFileLowering();
  auto &streamer = *Asm->OutStreamer;

  // Size and sign of stack growth.
  int stackGrowth = -Asm->getDataLayout().getPointerSize();
  int dataAlignmentFactor = stackGrowth;

  // Emit the EH table

  // Can't use rodata as the symbols we reference are in the text segment
  streamer.SwitchSection(tlof.getTextSection());

  MCSymbol *tableSymbol =
    Asm->OutContext.getOrCreateSymbol(Twine(MonoEHFrameSymbol));
  MCSymbol *tableEndSym = Asm->createTempSymbol ("mono_eh_frame_end");

  // Symbol
  Asm->EmitAlignment(4);
  streamer.EmitLabel(tableSymbol);
  streamer.EmitSymbolAttribute(tableSymbol, MCSA_ELF_TypeObject);
  if (Asm->MAI->hasDotTypeDotSizeDirective()) {
    const MCExpr *SizeExp = MCBinaryExpr::createSub(
        MCSymbolRefExpr::create(tableEndSym, Asm->OutContext),
        MCSymbolRefExpr::create(tableSymbol, Asm->OutContext), Asm->OutContext);
    streamer.emitELFSize(cast<MCSymbolELF>(tableSymbol), SizeExp);
  }

  // Header
  streamer.AddComment("version");
  streamer.EmitIntValue(3, 1);
  streamer.AddComment ("func addr encoding");
  // Unused
  streamer.EmitIntValue (0, 1);

  // Search table
  Asm->EmitAlignment(2);
  streamer.AddComment("fde_count");
  streamer.EmitIntValue (Frames.size(), 4);

  MCSymbol *lastBegin = nullptr;
  MCSymbol *lastEnd = nullptr;
  for (std::vector<EHInfo>::iterator
		   I = Frames.begin(), E = Frames.end(); I != E; ++I) {
	  EHInfo &info = *I;

      info.FDESym = Asm->createTempSymbol ("mono_fde");

      streamer.AddComment("mono method idx");
      streamer.EmitIntValue (info.MonoMethodIdx, 4);

	  Asm->EmitLabelDifference(info.FDESym, tableSymbol, 4);
      lastBegin = info.BeginSym;
      lastEnd = info.EndSym;
  }

  // Emit a last entry to simplify binary searches and to enable the computation of
  // the size of the last function/FDE entry
  if (Frames.size() == 0) {
	  streamer.EmitIntValue (-1, 4);
	  Asm->EmitLabelDifference(tableSymbol, tableSymbol, 4);
  } else {
	  // Emit the size of the last function, since it cannot be computed using the next table entry
	  Asm->EmitLabelDifference(lastEnd, lastBegin, 4);
	  Asm->EmitLabelDifference(tableEndSym, tableSymbol, 4);
  }

  // CIE
  // This comes right after the search table
  Asm->EmitULEB128(1, "CIE Code Alignment Factor");
  Asm->EmitSLEB128(stackGrowth, "CIE Data Alignment Factor");
  streamer.AddComment("CIE Return Address Column");
  // RI can be null if there are no methods
  if (RI)
    Asm->EmitInt8(RI->getDwarfRegNum(RI->getRARegister(), true));
  Asm->EmitEncodingByte(dwarf::DW_EH_PE_omit, "Personality");

  int cfaOffset = 0;

  // Initial CIE program
  emitCFIInstructions(streamer, streamer.getContext().getAsmInfo()->getInitialFrameState(), NULL, NULL, cfaOffset, stackGrowth);
  streamer.AddComment("End of CIE program");
  streamer.EmitIntValue(dwarf::DW_CFA_nop, 1);

  int cieCfaOffset = cfaOffset;

  // FDEs
  streamer.AddBlankLine();
  for (std::vector<EHInfo>::iterator
		   I = Frames.begin(), E = Frames.end(); I != E; ++I) {
	  const EHInfo &info = *I;

      streamer.EmitLabel(info.FDESym);

      // Emit augmentation
      if (info.HasLandingPads || info.FrameReg != -1) {
        // Need an extra has_augmentation field as the augmentation size is always encoded
        // in 4 bytes
        Asm->EmitULEB128(1, "Has augmentation");

        MCSymbol *fdeBeginSym = Asm->OutContext.createTempSymbol("mono_fde_aug_begin", info.FunctionNumber);
        MCSymbol *fdeEndSym = Asm->OutContext.createTempSymbol("mono_fde_aug_end", info.FunctionNumber);

        streamer.AddComment("Augmentation size");
        Asm->EmitLabelDifference(fdeEndSym, fdeBeginSym, 4);

        streamer.EmitLabel(fdeBeginSym);
        EmitMonoLSDA (&info);
        streamer.EmitLabel(fdeEndSym);
      } else {
        Asm->EmitULEB128(0, "Has augmentation");
      }

      // Emit unwind info
      cfaOffset = cieCfaOffset;
      emitCFIInstructions(streamer, info.Instructions, info.BeginSym, &info.EHLabels, cfaOffset, dataAlignmentFactor);

      streamer.AddBlankLine();
  }

  streamer.EmitLabel(tableEndSym);
  Asm->EmitAlignment(3);
}

void
MonoException::beginInstruction(const MachineInstr *MI)
{
	if (MI->getOpcode() == TargetOpcode::CFI_INSTRUCTION) {
		unsigned CFIIndex = MI->getOperand(0).getCFIIndex();

		//outs () << "D: " << CFIIndex << " " << EHLabels.size() << "\n";

		/* Emit a label and save the label-cfi index association */
		if (CFIIndex != EHLabels.size())
			assert (0);

		MCSymbol *Label = Asm->OutContext.createTempSymbol();
		Asm->OutStreamer->EmitLabel(Label);

		EHLabels.push_back(Label);
	}
}
