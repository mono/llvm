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
#include "llvm/Module.h"
#include "llvm/Constants.h"
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
#include "llvm/Target/Mangler.h"
#include "llvm/Target/TargetData.h"
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

static const MachineLocation TranslateMachineLocation(
                                                  const MCRegisterInfo &RegInfo,
                                                  const MachineLocation &Loc) {
  unsigned Reg = Loc.getReg() == MachineLocation::VirtualFP ?
    MachineLocation::VirtualFP :
    unsigned(RegInfo.getDwarfRegNum(Loc.getReg(), true));
  const MachineLocation &NewLoc = Loc.isReg() ?
    MachineLocation(Reg) : MachineLocation(Reg, Loc.getOffset());
  return NewLoc;
}

static void EncodeCFIInstructions (MCStreamer &streamer,
                                   const std::vector<MachineMove> &Moves,
                                   std::vector<MCCFIInstruction> &Instructions) {
      MCContext &context = streamer.getContext();
      const MCRegisterInfo &regInfo = context.getRegisterInfo();
      
      for (int i = 0, n = Moves.size(); i != n; ++i) {
        MCSymbol *Label = Moves[i].getLabel();
        const MachineLocation &Dst =
          TranslateMachineLocation(regInfo, Moves[i].getDestination());
        const MachineLocation &Src =
          TranslateMachineLocation(regInfo, Moves[i].getSource());
        MCCFIInstruction Inst(Label, Dst, Src);
        Instructions.push_back(Inst);
      }
}

void EmitCFIInstruction(MCStreamer &Streamer,
                        const MCCFIInstruction &Instr,
                        int &CFAOffset, int DataAlignmentFactor) {
  switch (Instr.getOperation()) {
  case MCCFIInstruction::Move:
  case MCCFIInstruction::RelMove: {
    const MachineLocation &Dst = Instr.getDestination();
    const MachineLocation &Src = Instr.getSource();
    const bool IsRelative = Instr.getOperation() == MCCFIInstruction::RelMove;

    // If advancing cfa.
    if (Dst.isReg() && Dst.getReg() == MachineLocation::VirtualFP) {
      if (IsRelative)
        CFAOffset += Src.getOffset();
      else
        CFAOffset = -Src.getOffset();

      if (Src.getReg() == MachineLocation::VirtualFP) {
        Streamer.AddComment ("def_cfa_offset");
        Streamer.EmitIntValue(dwarf::DW_CFA_def_cfa_offset, 1);
      } else {
        Streamer.AddComment ("def_cfa");
        Streamer.EmitIntValue(dwarf::DW_CFA_def_cfa, 1);
        Streamer.EmitULEB128IntValue(Src.getReg());
      }

      Streamer.EmitULEB128IntValue(CFAOffset);
      return;
    }

    if (Src.isReg() && Src.getReg() == MachineLocation::VirtualFP) {
      assert(Dst.isReg() && "Machine move not supported yet.");
        Streamer.AddComment ("def_cfa_register");
      Streamer.EmitIntValue(dwarf::DW_CFA_def_cfa_register, 1);
      Streamer.EmitULEB128IntValue(Dst.getReg());
      return;
    }

    unsigned Reg = Src.getReg();

    int Offset = Dst.getOffset();
    if (IsRelative)
      Offset -= CFAOffset;
    Offset = Offset / DataAlignmentFactor;

    if (Offset < 0) {
      Streamer.AddComment ("cfa_offset_extended_sf");
      Streamer.EmitIntValue(dwarf::DW_CFA_offset_extended_sf, 1);
      Streamer.EmitULEB128IntValue(Reg);
      Streamer.EmitSLEB128IntValue(Offset);
    } else if (Reg < 64) {
      Streamer.AddComment ("cfa_offset");
      Streamer.EmitIntValue(dwarf::DW_CFA_offset + Reg, 1);
      Streamer.EmitULEB128IntValue(Offset);
    } else {
      Streamer.AddComment ("cfa_offset_extended");
      Streamer.EmitIntValue(dwarf::DW_CFA_offset_extended, 1);
      Streamer.EmitULEB128IntValue(Reg);
      Streamer.EmitULEB128IntValue(Offset);
    }
    return;
  }
  case MCCFIInstruction::Remember:
    Streamer.EmitIntValue(dwarf::DW_CFA_remember_state, 1);
    return;
  case MCCFIInstruction::Restore:
    Streamer.EmitIntValue(dwarf::DW_CFA_restore_state, 1);
    return;
  case MCCFIInstruction::SameValue: {
    unsigned Reg = Instr.getDestination().getReg();
    Streamer.EmitIntValue(dwarf::DW_CFA_same_value, 1);
    Streamer.EmitULEB128IntValue(Reg);
    return;
  }
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
void DwarfException::PrepareMonoLSDA(FunctionEHFrameInfo *EHFrameInfo) {
  const std::vector<const GlobalVariable *> &TypeInfos = MMI->getTypeInfos();
  const std::vector<unsigned> &FilterIds = MMI->getFilterIds();
  const std::vector<LandingPadInfo> &PadInfos = MMI->getLandingPads();

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());

  for (unsigned i = 0, N = PadInfos.size(); i != N; ++i)
    LandingPads.push_back(&PadInfos[i]);

  std::sort(LandingPads.begin(), LandingPads.end(), PadLT);

  // Compute the actions table and gather the first action index for each
  // landing pad site.
  SmallVector<ActionEntry, 32> Actions;
  SmallVector<unsigned, 64> FirstActions;
  ComputeActionsTable(LandingPads, Actions, FirstActions);

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
  SmallVector<CallSiteEntry, 64> CallSites;
  ComputeCallSiteTable(CallSites, PadMap, LandingPads, FirstActions);

  MonoEHFrameInfo *MonoEH = &EHFrameInfo->MonoEH;

  // Save information for EmitMonoLSDA
  MonoEH->MF = Asm->MF;
  MonoEH->FunctionNumber = Asm->getFunctionNumber();
  MonoEH->CallSites.insert(MonoEH->CallSites.begin(), CallSites.begin(), CallSites.end());
  MonoEH->TypeInfos = TypeInfos;
  MonoEH->FilterIds = FilterIds;
  MonoEH->PadInfos = PadInfos;

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
///   We emit the information inline instead of into a separate section.
///
void DwarfException::EmitMonoLSDA(const FunctionEHFrameInfo *EFI) {
  //
  // The code below is a modified/simplified version of EmitExceptionTable
  //

  // Load saved information from EHFrameInfo
  const MonoEHFrameInfo *MonoEH = &EFI->MonoEH;
  const std::vector<const GlobalVariable *> &TypeInfos = MonoEH->TypeInfos;
  const std::vector<unsigned> &FilterIds = MonoEH->FilterIds;
  const std::vector<LandingPadInfo> &PadInfos = MonoEH->PadInfos;
  const std::vector<CallSiteEntry> CallSites = MonoEH->CallSites;
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

  // Compute the actions table and gather the first action index for each
  // landing pad site.
  SmallVector<ActionEntry, 32> Actions;
  SmallVector<unsigned, 64> FirstActions;
  unsigned SizeActions=ComputeActionsTable(LandingPads, Actions, FirstActions);

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

  // Final tallies.

  // Call sites.
  
  unsigned CallSiteTableLength;

  unsigned SiteStartSize  = 4; // dwarf::DW_EH_PE_udata4
  unsigned SiteLengthSize = 4; // dwarf::DW_EH_PE_udata4
  unsigned LandingPadSize = 4; // dwarf::DW_EH_PE_udata4
  CallSiteTableLength = 
    CallSites.size() * (SiteStartSize + SiteLengthSize + LandingPadSize);

  for (unsigned i = 0, e = CallSites.size(); i < e; ++i) {
    CallSiteTableLength += MCAsmInfo::getULEB128Size(CallSites[i].Action);
  }

  // Type infos.
  unsigned TTypeEncoding;
  unsigned TypeFormatSize;

  // The type_info itself is emitted
  TTypeEncoding = dwarf::DW_EH_PE_udata4;
  TypeFormatSize = Asm->GetSizeOfEncodedValue(TTypeEncoding);

  // Emit the LSDA.

  // Emit the LSDA header.
  if (FrameReg != -1) {
    // Keep this in sync with JITDwarfEmitter::EmitExceptionTable ()
    // FIXME: If this method has no clauses, avoid emitting the rest of the info
    // Mark that this is a mono specific LSDA header using a magic value
    Asm->EmitEncodingByte(dwarf::DW_EH_PE_udata4, "@LPStart");
    Asm->EmitULEB128(0x4d4fef4f, "MONO Magic", 0);
    Asm->EmitULEB128(1, "Version", 0);

    // Emit 'this' location
    Asm->OutStreamer.AddComment("bregx");
    Asm->EmitInt8((int)dwarf::DW_OP_bregx);
    Asm->EmitULEB128(FrameReg, "Base reg");
    Asm->EmitSLEB128(ThisOffset, "Offset");
  } else {
    Asm->EmitEncodingByte(dwarf::DW_EH_PE_omit, "@LPStart");
  }
  Asm->EmitEncodingByte(TTypeEncoding, "@TType");

  unsigned SizeTypes = TypeInfos.size() * TypeFormatSize;
  unsigned CallSiteTableLengthSize =
    MCAsmInfo::getULEB128Size(CallSiteTableLength);
  unsigned TTypeBaseOffset =
    sizeof(int8_t) +                            // Call site format
    CallSiteTableLengthSize +                   // Call site table length size
    CallSiteTableLength +                       // Call site table length
    SizeActions +                               // Actions size
    SizeTypes;

  Asm->EmitULEB128(TTypeBaseOffset, "@TType base offset", 0);

  assert(Asm->MAI->getExceptionHandlingType() == ExceptionHandling::DwarfCFI);

  // The call-site table is a list of all call sites that may throw an
  // exception (including C++ 'throw' statements) in the procedure
  // fragment. It immediately follows the LSDA header. Each entry indicates,
  // for a given call, the first corresponding action record and corresponding
  // landing pad.
  //
  // The table begins with the number of bytes, stored as an LEB128
  // compressed, unsigned integer. The records immediately follow the record
  // count. They are sorted in increasing call-site address. Each record
  // indicates:
  //
  //   * The position of the call-site.
  //   * The position of the landing pad.
  //   * The first action record for that call site.
  //
  // A missing entry in the call-site table indicates that a call is not
  // supposed to throw.

  // Emit the landing pad call site table.
  Asm->EmitEncodingByte(dwarf::DW_EH_PE_udata4, "Call site");

  // Add extra padding if it wasn't added to the TType base offset.
  Asm->EmitULEB128(CallSiteTableLength, "Call site table length", 0);

  for (std::vector<CallSiteEntry>::const_iterator
         I = CallSites.begin(), E = CallSites.end(); I != E; ++I) {
    const CallSiteEntry &S = *I;
      
    MCSymbol *EHFuncBeginSym =
      Asm->GetTempSymbol("eh_func_begin", FunctionNumber);
      
    MCSymbol *BeginLabel = S.BeginLabel;
    if (BeginLabel == 0)
      BeginLabel = EHFuncBeginSym;
    MCSymbol *EndLabel = S.EndLabel;
    if (EndLabel == 0)
      EndLabel = Asm->GetTempSymbol("eh_func_end", FunctionNumber);
        
    // Offset of the call site relative to the previous call site, counted in
    // number of 16-byte bundles. The first call site is counted relative to
    // the start of the procedure fragment.
    Asm->OutStreamer.AddComment("Region start");
    Asm->EmitLabelDifference(BeginLabel, EHFuncBeginSym, 4);
      
    Asm->OutStreamer.AddComment("Region length");
    Asm->EmitLabelDifference(EndLabel, BeginLabel, 4);


    // Offset of the landing pad, counted in 16-byte bundles relative to the
    // @LPStart address.
    Asm->OutStreamer.AddComment("Landing pad");
    if (!S.PadLabel)
      Asm->OutStreamer.EmitIntValue(0, 4/*size*/, 0/*addrspace*/);
    else
      Asm->EmitLabelDifference(S.PadLabel, EHFuncBeginSym, 4);

    // Offset of the first associated action record, relative to the start of
    // the action table. This value is biased by 1 (1 indicates the start of
    // the action table), and 0 indicates that there are no actions.
    Asm->EmitULEB128(S.Action, "Action");
  }

  // Emit the Action Table.
  if (Actions.size() != 0) {
    Asm->OutStreamer.AddComment("-- Action Record Table --");
    Asm->OutStreamer.AddBlankLine();
  }
  
  for (SmallVectorImpl<ActionEntry>::const_iterator
         I = Actions.begin(), E = Actions.end(); I != E; ++I) {
    const ActionEntry &Action = *I;
    Asm->OutStreamer.AddComment("Action Record");
    Asm->OutStreamer.AddBlankLine();

    // Type Filter
    //
    //   Used by the runtime to match the type of the thrown exception to the
    //   type of the catch clauses or the types in the exception specification.
    Asm->EmitSLEB128(Action.ValueForTypeID, "  TypeInfo index");

    // Action Record
    //
    //   Self-relative signed displacement in bytes of the next action record,
    //   or 0 if there is no next action record.
    Asm->EmitSLEB128(Action.NextAction, "  Next action");
  }

  // Emit the Catch TypeInfos.
  if (!TypeInfos.empty()) {
    Asm->OutStreamer.AddComment("-- Catch TypeInfos --");
    Asm->OutStreamer.AddBlankLine();
  }
  for (std::vector<const GlobalVariable *>::const_reverse_iterator
         I = TypeInfos.rbegin(), E = TypeInfos.rend(); I != E; ++I) {
    const GlobalVariable *GV = *I;

    //
    // Mono typeinfos are simple constant integers. Emit the constant itself.
    //
    assert(GV);
    const ConstantInt *ci = dyn_cast<ConstantInt>(GV->getInitializer());

    Asm->OutStreamer.AddComment("TypeInfo");
    Asm->OutStreamer.EmitIntValue(ci->getZExtValue(),Asm->GetSizeOfEncodedValue(TTypeEncoding),
                                  0);
  }

  // Emit the Exception Specifications.
  if (!FilterIds.empty()) {
    Asm->OutStreamer.AddComment("-- Filter IDs --");
    Asm->OutStreamer.AddBlankLine();
  }
  for (std::vector<unsigned>::const_iterator
         I = FilterIds.begin(), E = FilterIds.end(); I < E; ++I) {
    unsigned TypeID = *I;
    Asm->EmitULEB128(TypeID, TypeID != 0 ? "Exception specification" : 0);
  }
}

// EmitMonoEHFrame - Emit Mono specific exception handling tables
void DwarfException::EmitMonoEHFrame(const Function *Personality)
{
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  unsigned PerEncoding = TLOF.getPersonalityEncoding();
  unsigned FuncAddrEncoding = TLOF.getMonoEHTableEncoding ();

  // Size and sign of stack growth.
  int stackGrowth = Asm->getTargetData().getPointerSize();
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
  Asm->OutStreamer.EmitIntValue(2, 1, 0);
  Asm->OutStreamer.AddComment ("func addr encoding");
  Asm->OutStreamer.EmitIntValue (FuncAddrEncoding, 1, 0);

  // Search table
  Asm->EmitAlignment(2);
  Streamer.AddComment("fde_count");
  Streamer.EmitIntValue (EHFrames.size(), 4, 0);
  for (std::vector<FunctionEHFrameInfo>::iterator
		   I = EHFrames.begin(), E = EHFrames.end(); I != E; ++I) {
	  const FunctionEHFrameInfo &EHFrameInfo = *I;

      MCSymbol *EHFuncBeginSym =
        Asm->GetTempSymbol("eh_func_begin", EHFrameInfo.Number);
	  MCSymbol *FDEBeginSym = Asm->GetTempSymbol ("mono_eh_func_begin", EHFrameInfo.Number);
	  if (FuncAddrEncoding == dwarf::DW_EH_PE_absptr) {
 		  // On ios, the linker can move functions inside object files so the offsets between two symbols are not assembler constant.
		  Asm->EmitReference (EHFuncBeginSym, FuncAddrEncoding);
	  } else {
 		  // FIXME: Use DW_EH_PE_pcrel in the future
		  Asm->EmitLabelDifference(EHFuncBeginSym, EHFrameHdrSym, 4);
	  }
	  Asm->EmitLabelDifference(FDEBeginSym, EHFrameHdrSym, 4);
  }
  // Emit a last entry to simplify binary searches and to enable the computation of
  // the size of the last function/FDE entry
  if (EHFrames.size() == 0) {
	  Asm->EmitLabelDifference(EHFrameHdrSym, EHFrameHdrSym, 4);
	  Asm->EmitLabelDifference(EHFrameHdrSym, EHFrameHdrSym, 4);
  } else {
    MCSymbol *Sym1 = Asm->GetTempSymbol("eh_func_end", EHFrames[EHFrames.size() - 1].Number);
    MCSymbol *Sym2 = Asm->GetTempSymbol ("mono_eh_frame_end");
	if (FuncAddrEncoding == dwarf::DW_EH_PE_absptr)
		Asm->EmitReference (Sym1, FuncAddrEncoding);
 	else
 		Asm->EmitLabelDifference(Sym1, EHFrameHdrSym, 4);
    Asm->EmitLabelDifference(Sym2, EHFrameHdrSym, 4);
  }

  // CIE
  // This comes right after the search table
  Asm->EmitULEB128(1, "CIE Code Alignment Factor");
  Asm->EmitSLEB128(stackGrowth, "CIE Data Alignment Factor");
  Streamer.AddComment("CIE Return Address Column");
  const TargetRegisterInfo *RI = Asm->TM.getRegisterInfo();
  Asm->EmitInt8(RI->getDwarfRegNum(RI->getRARegister(), true));

  if (Personality) {
    Asm->EmitEncodingByte(PerEncoding, "Personality");
    Streamer.AddComment("Personality");
    Asm->EmitReference(Personality, PerEncoding);
  } else {
    Asm->EmitEncodingByte(dwarf::DW_EH_PE_omit, "Personality");
  }

  int CFAOffset = 0;

  int dataAlignmentFactor = stackGrowth;

  // Initial CIE program
  const std::vector<MachineMove> Moves = 
    Streamer.getContext().getAsmInfo().getInitialFrameState();
  std::vector<MCCFIInstruction> Instructions;
  EncodeCFIInstructions (Streamer, Moves, Instructions);
  EmitCFIInstructions(Streamer, Instructions, NULL, CFAOffset, dataAlignmentFactor);

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

      std::vector<MCCFIInstruction> Instructions;
      EncodeCFIInstructions (Streamer, EHFrameInfo.Moves, Instructions);
      EmitCFIInstructions(Streamer, Instructions, NULL, CFAOffset, dataAlignmentFactor);

      Streamer.AddBlankLine();
  }

  Streamer.EmitLabel(EHFrameEndSym);
}
