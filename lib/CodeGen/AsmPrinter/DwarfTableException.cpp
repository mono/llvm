//===-- CodeGen/AsmPrinter/DwarfTableException.cpp - Dwarf Exception Impl --==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing DWARF exception info into asm files.
// The implementation emits all the necessary tables "by hands".
//
//===----------------------------------------------------------------------===//

#include "DwarfException.h"
#include "llvm/Module.h"
#include "llvm/Constants.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLocation.h"
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
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
using namespace llvm;

static cl::opt<bool> EnableMonoEH("enable-mono-eh-frame", cl::NotHidden,
     cl::desc("Enable generation of Mono specific EH tables"));

static cl::opt<bool> DisableGNUEH("disable-gnu-eh-frame", cl::NotHidden,
     cl::desc("Disable generation of GNU .eh_frame"));


DwarfTableException::DwarfTableException(AsmPrinter *A)
  :  DwarfException(A),
     shouldEmitTable(false), shouldEmitMoves(false),
     shouldEmitTableModule(false), shouldEmitMovesModule(false) {}

DwarfTableException::~DwarfTableException() {}

/// EmitCIE - Emit a Common Information Entry (CIE). This holds information that
/// is shared among many Frame Description Entries.  There is at least one CIE
/// in every non-empty .debug_frame section.
void DwarfTableException::EmitCIE(const Function *PersonalityFn, unsigned Index) {
  // Size and sign of stack growth.
  int stackGrowth = Asm->getTargetData().getPointerSize();
  if (Asm->TM.getFrameLowering()->getStackGrowthDirection() ==
      TargetFrameLowering::StackGrowsDown)
    stackGrowth *= -1;

  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  // Begin eh frame section.
  Asm->OutStreamer.SwitchSection(TLOF.getEHFrameSection());

  MCSymbol *EHFrameSym;
  if (TLOF.isFunctionEHFrameSymbolPrivate())
    EHFrameSym = Asm->GetTempSymbol("EH_frame", Index);
  else
    EHFrameSym = Asm->OutContext.GetOrCreateSymbol(Twine("EH_frame") +
                                                   Twine(Index));
  Asm->OutStreamer.EmitLabel(EHFrameSym);

  Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("section_eh_frame", Index));

  // Define base labels.
  Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_frame_common", Index));

  // Define the eh frame length.
  Asm->OutStreamer.AddComment("Length of Common Information Entry");
  Asm->EmitLabelDifference(Asm->GetTempSymbol("eh_frame_common_end", Index),
                           Asm->GetTempSymbol("eh_frame_common_begin", Index),
                           4);

  // EH frame header.
  Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_frame_common_begin",Index));
  Asm->OutStreamer.AddComment("CIE Identifier Tag");
  Asm->OutStreamer.EmitIntValue(0, 4/*size*/, 0/*addrspace*/);
  Asm->OutStreamer.AddComment("DW_CIE_VERSION");
  Asm->OutStreamer.EmitIntValue(dwarf::DW_CIE_VERSION, 1/*size*/, 0/*addr*/);

  // The personality presence indicates that language specific information will
  // show up in the eh frame.  Find out how we are supposed to lower the
  // personality function reference:

  unsigned LSDAEncoding = TLOF.getLSDAEncoding();
  unsigned FDEEncoding = TLOF.getFDEEncoding();
  unsigned PerEncoding = TLOF.getPersonalityEncoding();

  char Augmentation[6] = { 0 };
  unsigned AugmentationSize = 0;
  char *APtr = Augmentation + 1;

  if (PersonalityFn) {
    // There is a personality function.
    *APtr++ = 'P';
    AugmentationSize += 1 + Asm->GetSizeOfEncodedValue(PerEncoding);
  }

  if (UsesLSDA[Index]) {
    // An LSDA pointer is in the FDE augmentation.
    *APtr++ = 'L';
    ++AugmentationSize;
  }

  if (FDEEncoding != dwarf::DW_EH_PE_absptr) {
    // A non-default pointer encoding for the FDE.
    *APtr++ = 'R';
    ++AugmentationSize;
  }

  if (APtr != Augmentation + 1)
    Augmentation[0] = 'z';

  Asm->OutStreamer.AddComment("CIE Augmentation");
  Asm->OutStreamer.EmitBytes(StringRef(Augmentation, strlen(Augmentation)+1),0);

  // Round out reader.
  Asm->EmitULEB128(1, "CIE Code Alignment Factor");
  Asm->EmitSLEB128(stackGrowth, "CIE Data Alignment Factor");
  Asm->OutStreamer.AddComment("CIE Return Address Column");

  const TargetRegisterInfo *RI = Asm->TM.getRegisterInfo();
  const TargetFrameLowering *TFI = Asm->TM.getFrameLowering();
  Asm->EmitInt8(RI->getDwarfRegNum(RI->getRARegister(), true));

  if (Augmentation[0]) {
    Asm->EmitULEB128(AugmentationSize, "Augmentation Size");

    // If there is a personality, we need to indicate the function's location.
    if (PersonalityFn) {
      Asm->EmitEncodingByte(PerEncoding, "Personality");
      Asm->OutStreamer.AddComment("Personality");
      Asm->EmitReference(PersonalityFn, PerEncoding);
    }
    if (UsesLSDA[Index])
      Asm->EmitEncodingByte(LSDAEncoding, "LSDA");
    if (FDEEncoding != dwarf::DW_EH_PE_absptr)
      Asm->EmitEncodingByte(FDEEncoding, "FDE");
  }

  // Indicate locations of general callee saved registers in frame.
  std::vector<MachineMove> Moves;
  TFI->getInitialFrameState(Moves);
  Asm->EmitFrameMoves(Moves, 0, true);

  // On Darwin the linker honors the alignment of eh_frame, which means it must
  // be 8-byte on 64-bit targets to match what gcc does.  Otherwise you get
  // holes which confuse readers of eh_frame.
  Asm->EmitAlignment(Asm->getTargetData().getPointerSize() == 4 ? 2 : 3);
  Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_frame_common_end", Index));
}

/// EmitFDE - Emit the Frame Description Entry (FDE) for the function.
void DwarfTableException::EmitFDE(const FunctionEHFrameInfo &EHFrameInfo) {
  assert(!EHFrameInfo.function->hasAvailableExternallyLinkage() &&
         "Should not emit 'available externally' functions at all");

  const Function *TheFunc = EHFrameInfo.function;
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  unsigned LSDAEncoding = TLOF.getLSDAEncoding();
  unsigned FDEEncoding = TLOF.getFDEEncoding();

  Asm->OutStreamer.SwitchSection(TLOF.getEHFrameSection());

  // Externally visible entry into the functions eh frame info. If the
  // corresponding function is static, this should not be externally visible.
  if (!TheFunc->hasLocalLinkage() && TLOF.isFunctionEHSymbolGlobal())
    Asm->OutStreamer.EmitSymbolAttribute(EHFrameInfo.FunctionEHSym,MCSA_Global);

  // If corresponding function is weak definition, this should be too.
  if (TheFunc->isWeakForLinker() && Asm->MAI->getWeakDefDirective())
    Asm->OutStreamer.EmitSymbolAttribute(EHFrameInfo.FunctionEHSym,
                                         MCSA_WeakDefinition);

  // If corresponding function is hidden, this should be too.
  if (TheFunc->hasHiddenVisibility())
    if (MCSymbolAttr HiddenAttr = Asm->MAI->getHiddenVisibilityAttr())
      Asm->OutStreamer.EmitSymbolAttribute(EHFrameInfo.FunctionEHSym,
                                           HiddenAttr);

  // If there are no calls then you can't unwind.  This may mean we can omit the
  // EH Frame, but some environments do not handle weak absolute symbols. If
  // UnwindTablesMandatory is set we cannot do this optimization; the unwind
  // info is to be available for non-EH uses.
  if (!EHFrameInfo.adjustsStack && !UnwindTablesMandatory &&
      (!TheFunc->isWeakForLinker() ||
       !Asm->MAI->getWeakDefDirective() ||
       TLOF.getSupportsWeakOmittedEHFrame())) {
    Asm->OutStreamer.EmitAssignment(EHFrameInfo.FunctionEHSym,
                                    MCConstantExpr::Create(0, Asm->OutContext));
    // This name has no connection to the function, so it might get
    // dead-stripped when the function is not, erroneously.  Prohibit
    // dead-stripping unconditionally.
    if (Asm->MAI->hasNoDeadStrip())
      Asm->OutStreamer.EmitSymbolAttribute(EHFrameInfo.FunctionEHSym,
                                           MCSA_NoDeadStrip);
  } else {
    Asm->OutStreamer.EmitLabel(EHFrameInfo.FunctionEHSym);

    // EH frame header.
    Asm->OutStreamer.AddComment("Length of Frame Information Entry");
    Asm->EmitLabelDifference(
                Asm->GetTempSymbol("eh_frame_end", EHFrameInfo.Number),
                Asm->GetTempSymbol("eh_frame_begin", EHFrameInfo.Number), 4);

    Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_frame_begin",
                                                  EHFrameInfo.Number));

    Asm->OutStreamer.AddComment("FDE CIE offset");
    Asm->EmitLabelDifference(
                       Asm->GetTempSymbol("eh_frame_begin", EHFrameInfo.Number),
                       Asm->GetTempSymbol("eh_frame_common",
                                          EHFrameInfo.PersonalityIndex), 4);

    MCSymbol *EHFuncBeginSym =
      Asm->GetTempSymbol("eh_func_begin", EHFrameInfo.Number);

    Asm->OutStreamer.AddComment("FDE initial location");
    Asm->EmitReference(EHFuncBeginSym, FDEEncoding);

    Asm->OutStreamer.AddComment("FDE address range");
    Asm->EmitLabelDifference(Asm->GetTempSymbol("eh_func_end",
                                                EHFrameInfo.Number),
                             EHFuncBeginSym,
                             Asm->GetSizeOfEncodedValue(FDEEncoding));

    // If there is a personality and landing pads then point to the language
    // specific data area in the exception table.
    if (MMI->getPersonalities()[0] != NULL) {
      unsigned Size = Asm->GetSizeOfEncodedValue(LSDAEncoding);

      Asm->EmitULEB128(Size, "Augmentation size");
      Asm->OutStreamer.AddComment("Language Specific Data Area");
      if (EHFrameInfo.hasLandingPads)
        Asm->EmitReference(Asm->GetTempSymbol("exception", EHFrameInfo.Number),
                           LSDAEncoding);
      else
        Asm->OutStreamer.EmitIntValue(0, Size/*size*/, 0/*addrspace*/);

    } else {
      Asm->EmitULEB128(0, "Augmentation size");
    }

    // Indicate locations of function specific callee saved registers in frame.
    Asm->EmitFrameMoves(EHFrameInfo.Moves, EHFuncBeginSym, true);

    // On Darwin the linker honors the alignment of eh_frame, which means it
    // must be 8-byte on 64-bit targets to match what gcc does.  Otherwise you
    // get holes which confuse readers of eh_frame.
    Asm->EmitAlignment(Asm->getTargetData().getPointerSize() == 4 ? 2 : 3);
    Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_frame_end",
                                                  EHFrameInfo.Number));

    // If the function is marked used, this table should be also.  We cannot
    // make the mark unconditional in this case, since retaining the table also
    // retains the function in this case, and there is code around that depends
    // on unused functions (calling undefined externals) being dead-stripped to
    // link correctly.  Yes, there really is.
    if (MMI->isUsedFunction(EHFrameInfo.function))
      if (Asm->MAI->hasNoDeadStrip())
        Asm->OutStreamer.EmitSymbolAttribute(EHFrameInfo.FunctionEHSym,
                                             MCSA_NoDeadStrip);
  }
  Asm->OutStreamer.AddBlankLine();
}

/// EndModule - Emit all exception information that should come after the
/// content.
void DwarfTableException::EndModule() {
  if (!Asm->MAI->isExceptionHandlingDwarf())
    return;

  if (!shouldEmitMovesModule && !shouldEmitTableModule)
    return;

  const std::vector<const Function*> &Personalities = MMI->getPersonalities();

  if (!DisableGNUEH) {
    for (unsigned I = 0, E = Personalities.size(); I < E; ++I)
      EmitCIE(Personalities[I], I);

    for (std::vector<FunctionEHFrameInfo>::iterator
           I = EHFrames.begin(), E = EHFrames.end(); I != E; ++I)
      EmitFDE(*I);
  }

  if (EnableMonoEH)
    EmitMonoEHFrame(Personalities[0]);
}

/// BeginFunction - Gather pre-function exception information. Assumes it's
/// being emitted immediately after the function entry point.
void DwarfTableException::BeginFunction(const MachineFunction *MF) {
  shouldEmitTable = shouldEmitMoves = false;

  // If any landing pads survive, we need an EH table.
  shouldEmitTable = !MMI->getLandingPads().empty();

  // See if we need frame move info.
  shouldEmitMoves =
    !Asm->MF->getFunction()->doesNotThrow() || UnwindTablesMandatory;

  if (shouldEmitMoves || shouldEmitTable)
    // Assumes in correct section after the entry point.
    Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_func_begin",
                                                  Asm->getFunctionNumber()));

  shouldEmitTableModule |= shouldEmitTable;
  shouldEmitMovesModule |= shouldEmitMoves;
}

/// EndFunction - Gather and emit post-function exception information.
///
void DwarfTableException::EndFunction() {
  if (!shouldEmitMoves && !shouldEmitTable) return;

  Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("eh_func_end",
                                                Asm->getFunctionNumber()));

  // Record if this personality index uses a landing pad.
  bool HasLandingPad = !MMI->getLandingPads().empty();
  UsesLSDA[MMI->getPersonalityIndex()] |= HasLandingPad;

  // Map all labels and get rid of any dead landing pads.
  MMI->TidyLandingPads();

  if (!DisableGNUEH && HasLandingPad)
    EmitExceptionTable();

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
                        MMI->getFrameMoves(),
                        Asm->MF->getFunction());

  if (EnableMonoEH)
    PrepareMonoLSDA(&EHFrameInfo);

  EHFrames.push_back(EHFrameInfo);
}

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

/// PrepareMonoLSDA - Collect information needed by EmitMonoLSDA
///
///   This function collects information available only during EndFunction which is needed
/// by EmitMonoLSDA and stores it into EHFrameInfo. It is the same as the
/// beginning of EmitExceptionTable.
///
void DwarfTableException::PrepareMonoLSDA(FunctionEHFrameInfo *EHFrameInfo) {
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

  if (EnableMonoEH) {
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
}

/// EmitMonoLSDA - Mono's version of EmitExceptionTable
///
///   We emit the information inline instead of into a separate section.
///
void DwarfTableException::EmitMonoLSDA(const FunctionEHFrameInfo *EFI) {
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

  assert(Asm->MAI->getExceptionHandlingType() == ExceptionHandling::DwarfTable);

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

  assert(Asm->MAI->getExceptionHandlingType() == ExceptionHandling::DwarfTable);

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
    ConstantInt *ci = dyn_cast<ConstantInt>(GV->getInitializer());

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
void DwarfTableException::EmitMonoEHFrame(const Function *Personality)
{
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  unsigned PerEncoding = TLOF.getPersonalityEncoding();

  // Size and sign of stack growth.
  int stackGrowth = Asm->getTargetData().getPointerSize();
  if (Asm->TM.getFrameLowering()->getStackGrowthDirection() ==
      TargetFrameLowering::StackGrowsDown)
    stackGrowth *= -1;

  //
  // The Mono runtime obtains EH info for LLVM compiled code by reading the GNU .eh_frame
  // created by LLVM. This has certain problems:
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
  // Use something similar to GNU .eh_frame/.eh_frame_hdr:
  //
  // http://refspecs.freestandards.org/LSB_1.3.0/gLSB/gLSB/ehframehdr.html
  //
  // It is hard to get smaller tables without assembler supports, since we can't encode
  // offsets in less that 4 bytes, can't encode information into the upper bits of offsets etc.
  //

  // Can't use rodata as the symbols we reference are in the text segment
  Asm->OutStreamer.SwitchSection(TLOF.getTextSection());

  MCSymbol *EHFrameHdrSym =
	  Asm->OutContext.GetOrCreateSymbol(Twine("mono_eh_frame"));
  MCSymbol *EHFrameEndSym = Asm->GetTempSymbol ("mono_eh_frame_end");

  Asm->EmitAlignment(4);
  Asm->OutStreamer.EmitLabel(EHFrameHdrSym);
  const MCExpr *Length = MakeStartMinusEndExpr(Asm->OutStreamer, *EHFrameHdrSym,
                                               *EHFrameEndSym, 0);
  if (Asm->MAI->hasDotTypeDotSizeDirective()) {
    Asm->OutStreamer.EmitELFSize(EHFrameHdrSym, Length);
    Asm->OutStreamer.EmitSymbolAttribute(EHFrameHdrSym, MCSA_ELF_TypeObject);
  }

  // Header

  Asm->OutStreamer.AddComment("version");
  Asm->OutStreamer.EmitIntValue(1, 1, 0);

  // Search table
  Asm->EmitAlignment(2);
  Asm->OutStreamer.AddComment("fde_count");
  Asm->OutStreamer.EmitIntValue (EHFrames.size(), 4, 0);
  for (std::vector<FunctionEHFrameInfo>::iterator
		   I = EHFrames.begin(), E = EHFrames.end(); I != E; ++I) {
	  const FunctionEHFrameInfo &EHFrameInfo = *I;

      MCSymbol *EHFuncBeginSym =
        Asm->GetTempSymbol("eh_func_begin", EHFrameInfo.Number);
	  MCSymbol *FDEBeginSym = Asm->GetTempSymbol ("mono_eh_func_begin", EHFrameInfo.Number);
	  Asm->EmitLabelDifference(EHFuncBeginSym, EHFrameHdrSym, 4);
	  Asm->EmitLabelDifference(FDEBeginSym, EHFrameHdrSym, 4);
  }
  // Emit a last entry to simplify binary searches and to enable the computation of
  // the size of the last function/FDE entry
  if (EHFrames.size() == 0) {
	  Asm->EmitLabelDifference(EHFrameHdrSym, EHFrameHdrSym, 4);
	  Asm->EmitLabelDifference(EHFrameHdrSym, EHFrameHdrSym, 4);
  } else {
    MCSymbol *Sym1 = Asm->GetTempSymbol("eh_func_end", EHFrames.size() - 1);
    MCSymbol *Sym2 = Asm->GetTempSymbol ("mono_eh_frame_end");
    Asm->EmitLabelDifference(Sym1, EHFrameHdrSym, 4);
    Asm->EmitLabelDifference(Sym2, EHFrameHdrSym, 4);
  }

  // CIE
  // This comes right after the search table
  Asm->EmitULEB128(1, "CIE Code Alignment Factor");
  Asm->EmitSLEB128(stackGrowth, "CIE Data Alignment Factor");
  Asm->OutStreamer.AddComment("CIE Return Address Column");
  const TargetRegisterInfo *RI = Asm->TM.getRegisterInfo();
  const TargetFrameLowering *TFI = Asm->TM.getFrameLowering();
  Asm->EmitInt8(RI->getDwarfRegNum(RI->getRARegister(), true));

  if (Personality) {
    Asm->EmitEncodingByte(PerEncoding, "Personality");
    Asm->OutStreamer.AddComment("Personality");
    Asm->EmitReference(Personality, PerEncoding);
  } else {
    Asm->EmitEncodingByte(dwarf::DW_EH_PE_omit, "Personality");
  }
  // Initial CIE program
  std::vector<MachineMove> Moves;
  TFI->getInitialFrameState(Moves);
  Asm->EmitFrameMoves(Moves, 0, true);

  // FDEs
  Asm->OutStreamer.AddBlankLine();
  for (std::vector<FunctionEHFrameInfo>::iterator
		   I = EHFrames.begin(), E = EHFrames.end(); I != E; ++I) {
	  const FunctionEHFrameInfo &EHFrameInfo = *I;
      int Index = EHFrameInfo.Number;

	  MCSymbol *FDEBeginSym = Asm->GetTempSymbol ("mono_eh_func_begin", Index);
      Asm->OutStreamer.EmitLabel(FDEBeginSym);

      // No need for length, CIE, PC begin, PC range, alignment

      // Emit augmentation
      if (EHFrameInfo.hasLandingPads) {
        // Need an extra has_augmentation field as the augmentation size is always encoded
        // in 4 bytes
        Asm->EmitULEB128(1, "Has augmentation");

        Asm->OutStreamer.AddComment("Augmentation size");
        Asm->EmitLabelDifference(Asm->GetTempSymbol("mono_fde_aug_end", Index),
                                 Asm->GetTempSymbol("mono_fde_aug_begin", Index),
                                 4);

        Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("mono_fde_aug_begin", Index));
        EmitMonoLSDA (&EHFrameInfo);
        Asm->OutStreamer.EmitLabel(Asm->GetTempSymbol("mono_fde_aug_end", Index));
      } else {
        Asm->EmitULEB128(0, "Has augmentation");
      }

      MCSymbol *EHFuncBeginSym =
        Asm->GetTempSymbol("eh_func_begin", EHFrameInfo.Number);
      //
      // The encoding used by EmitFrameMoves is very inefficient, the pc is advanced using
      // DW_CFA_advance_loc4, because it can't determine that the offsets are smaller.
      //
      Asm->EmitFrameMoves(EHFrameInfo.Moves, EHFuncBeginSym, true);

      Asm->OutStreamer.AddBlankLine();
  }

  Asm->OutStreamer.EmitLabel(EHFrameEndSym);
}
