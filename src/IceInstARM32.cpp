//===- subzero/src/IceInstARM32.cpp - ARM32 instruction implementation ----===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the InstARM32 and OperandARM32 classes, primarily the
/// constructors and the dump()/emit() methods.
///
//===----------------------------------------------------------------------===//

#include "IceInstARM32.h"

#include "IceAssemblerARM32.h"
#include "IceCfg.h"
#include "IceCfgNode.h"
#include "IceInst.h"
#include "IceOperand.h"
#include "IceRegistersARM32.h"
#include "IceTargetLoweringARM32.h"

namespace Ice {
namespace ARM32 {

namespace {

// maximum number of registers allowed in vpush/vpop.
static constexpr SizeT VpushVpopMaxConsecRegs = 16;

const struct TypeARM32Attributes_ {
  const char *WidthString;    // b, h, <blank>, or d
  const char *VecWidthString; // i8, i16, i32, f32, f64
  int8_t SExtAddrOffsetBits;
  int8_t ZExtAddrOffsetBits;
} TypeARM32Attributes[] = {
#define X(tag, elementty, int_width, vec_width, sbits, ubits, rraddr, shaddr)  \
  { int_width, vec_width, sbits, ubits }                                       \
  ,
    ICETYPEARM32_TABLE
#undef X
};

const struct InstARM32ShiftAttributes_ {
  const char *EmitString;
} InstARM32ShiftAttributes[] = {
#define X(tag, emit)                                                           \
  { emit }                                                                     \
  ,
    ICEINSTARM32SHIFT_TABLE
#undef X
};

const struct InstARM32CondAttributes_ {
  CondARM32::Cond Opposite;
  const char *EmitString;
} InstARM32CondAttributes[] = {
#define X(tag, encode, opp, emit)                                              \
  { CondARM32::opp, emit }                                                     \
  ,
    ICEINSTARM32COND_TABLE
#undef X
};

} // end of anonymous namespace

const char *InstARM32::getWidthString(Type Ty) {
  return TypeARM32Attributes[Ty].WidthString;
}

const char *InstARM32::getVecWidthString(Type Ty) {
  return TypeARM32Attributes[Ty].VecWidthString;
}

const char *InstARM32Pred::predString(CondARM32::Cond Pred) {
  return InstARM32CondAttributes[Pred].EmitString;
}

void InstARM32Pred::dumpOpcodePred(Ostream &Str, const char *Opcode,
                                   Type Ty) const {
  Str << Opcode << getPredicate() << "." << Ty;
}

CondARM32::Cond InstARM32::getOppositeCondition(CondARM32::Cond Cond) {
  return InstARM32CondAttributes[Cond].Opposite;
}

void InstARM32::startNextInst(const Cfg *Func) const {
  if (auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>())
    Asm->incEmitTextSize(InstSize);
}

void InstARM32::emitUsingTextFixup(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  GlobalContext *Ctx = Func->getContext();
  if (Ctx->getFlags().getDisableHybridAssembly()) {
    UnimplementedError(Ctx->getFlags());
    return;
  }
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  std::string Buffer;
  llvm::raw_string_ostream StrBuf(Buffer);
  OstreamLocker L(Ctx);
  Ostream &OldStr = Ctx->getStrEmit();
  Ctx->setStrEmit(StrBuf);
  // Start counting instructions here, so that emit() methods don't
  // need to call this for the first instruction.
  Asm->resetEmitTextSize();
  Asm->incEmitTextSize(InstSize);
  emit(Func);
  Ctx->setStrEmit(OldStr);
  Asm->emitTextInst(StrBuf.str(), Asm->getEmitTextSize());
}

void InstARM32::emitIAS(const Cfg *Func) const { emitUsingTextFixup(Func); }

void InstARM32Pred::emitUnaryopGPR(const char *Opcode,
                                   const InstARM32Pred *Inst, const Cfg *Func,
                                   bool NeedsWidthSuffix) {
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 1);
  Type SrcTy = Inst->getSrc(0)->getType();
  Str << "\t" << Opcode;
  if (NeedsWidthSuffix)
    Str << getWidthString(SrcTy);
  Str << Inst->getPredicate() << "\t";
  Inst->getDest()->emit(Func);
  Str << ", ";
  Inst->getSrc(0)->emit(Func);
}

void InstARM32Pred::emitUnaryopFP(const char *Opcode, const InstARM32Pred *Inst,
                                  const Cfg *Func) {
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 1);
  Type SrcTy = Inst->getSrc(0)->getType();
  Str << "\t" << Opcode << Inst->getPredicate() << getVecWidthString(SrcTy)
      << "\t";
  Inst->getDest()->emit(Func);
  Str << ", ";
  Inst->getSrc(0)->emit(Func);
}

void InstARM32Pred::emitTwoAddr(const char *Opcode, const InstARM32Pred *Inst,
                                const Cfg *Func) {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 2);
  Variable *Dest = Inst->getDest();
  assert(Dest == Inst->getSrc(0));
  Str << "\t" << Opcode << Inst->getPredicate() << "\t";
  Dest->emit(Func);
  Str << ", ";
  Inst->getSrc(1)->emit(Func);
}

void InstARM32Pred::emitThreeAddr(const char *Opcode, const InstARM32Pred *Inst,
                                  const Cfg *Func, bool SetFlags) {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 2);
  Str << "\t" << Opcode << (SetFlags ? "s" : "") << Inst->getPredicate()
      << "\t";
  Inst->getDest()->emit(Func);
  Str << ", ";
  Inst->getSrc(0)->emit(Func);
  Str << ", ";
  Inst->getSrc(1)->emit(Func);
}

void InstARM32::emitThreeAddrFP(const char *Opcode, const InstARM32 *Inst,
                                const Cfg *Func) {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 2);
  Str << "\t" << Opcode << getVecWidthString(Inst->getDest()->getType())
      << "\t";
  Inst->getDest()->emit(Func);
  Str << ", ";
  Inst->getSrc(0)->emit(Func);
  Str << ", ";
  Inst->getSrc(1)->emit(Func);
}

void InstARM32::emitFourAddrFP(const char *Opcode, const InstARM32 *Inst,
                               const Cfg *Func) {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 3);
  assert(Inst->getSrc(0) == Inst->getDest());
  Str << "\t" << Opcode << getVecWidthString(Inst->getDest()->getType())
      << "\t";
  Inst->getDest()->emit(Func);
  Str << ", ";
  Inst->getSrc(1)->emit(Func);
  Str << ", ";
  Inst->getSrc(2)->emit(Func);
}

void InstARM32Pred::emitFourAddr(const char *Opcode, const InstARM32Pred *Inst,
                                 const Cfg *Func) {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 3);
  Str << "\t" << Opcode << Inst->getPredicate() << "\t";
  Inst->getDest()->emit(Func);
  Str << ", ";
  Inst->getSrc(0)->emit(Func);
  Str << ", ";
  Inst->getSrc(1)->emit(Func);
  Str << ", ";
  Inst->getSrc(2)->emit(Func);
}

template <InstARM32::InstKindARM32 K>
void InstARM32FourAddrGPR<K>::emitIAS(const Cfg *Func) const {
  emitUsingTextFixup(Func);
}

template <InstARM32::InstKindARM32 K>
void InstARM32ThreeAddrFP<K>::emitIAS(const Cfg *Func) const {
  emitUsingTextFixup(Func);
}

template <> void InstARM32Mla::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 3);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->mla(getDest(), getSrc(0), getSrc(1), getSrc(2), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Mls::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 3);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->mls(getDest(), getSrc(0), getSrc(1), getSrc(2), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Pred::emitCmpLike(const char *Opcode, const InstARM32Pred *Inst,
                                const Cfg *Func) {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(Inst->getSrcSize() == 2);
  Str << "\t" << Opcode << Inst->getPredicate() << "\t";
  Inst->getSrc(0)->emit(Func);
  Str << ", ";
  Inst->getSrc(1)->emit(Func);
}

OperandARM32Mem::OperandARM32Mem(Cfg * /* Func */, Type Ty, Variable *Base,
                                 ConstantInteger32 *ImmOffset, AddrMode Mode)
    : OperandARM32(kMem, Ty), Base(Base), ImmOffset(ImmOffset), Index(nullptr),
      ShiftOp(kNoShift), ShiftAmt(0), Mode(Mode) {
  // The Neg modes are only needed for Reg +/- Reg.
  assert(!isNegAddrMode());
  NumVars = 1;
  Vars = &this->Base;
}

OperandARM32Mem::OperandARM32Mem(Cfg *Func, Type Ty, Variable *Base,
                                 Variable *Index, ShiftKind ShiftOp,
                                 uint16_t ShiftAmt, AddrMode Mode)
    : OperandARM32(kMem, Ty), Base(Base), ImmOffset(0), Index(Index),
      ShiftOp(ShiftOp), ShiftAmt(ShiftAmt), Mode(Mode) {
  if (Index->isRematerializable()) {
    llvm::report_fatal_error("Rematerializable Index Register is not allowed.");
  }
  NumVars = 2;
  Vars = Func->allocateArrayOf<Variable *>(2);
  Vars[0] = Base;
  Vars[1] = Index;
}

OperandARM32ShAmtImm::OperandARM32ShAmtImm(ConstantInteger32 *SA)
    : OperandARM32(kShAmtImm, IceType_i8), ShAmt(SA) {}

bool OperandARM32Mem::canHoldOffset(Type Ty, bool SignExt, int32_t Offset) {
  int32_t Bits = SignExt ? TypeARM32Attributes[Ty].SExtAddrOffsetBits
                         : TypeARM32Attributes[Ty].ZExtAddrOffsetBits;
  if (Bits == 0)
    return Offset == 0;
  // Note that encodings for offsets are sign-magnitude for ARM, so we check
  // with IsAbsoluteUint().
  // Scalar fp, and vector types require an offset that is aligned to a multiple
  // of 4.
  if (isScalarFloatingType(Ty) || isVectorType(Ty))
    return Utils::IsAligned(Offset, 4) && Utils::IsAbsoluteUint(Bits, Offset);
  return Utils::IsAbsoluteUint(Bits, Offset);
}

OperandARM32FlexImm::OperandARM32FlexImm(Cfg * /* Func */, Type Ty,
                                         uint32_t Imm, uint32_t RotateAmt)
    : OperandARM32Flex(kFlexImm, Ty), Imm(Imm), RotateAmt(RotateAmt) {
  NumVars = 0;
  Vars = nullptr;
}

bool OperandARM32FlexImm::canHoldImm(uint32_t Immediate, uint32_t *RotateAmt,
                                     uint32_t *Immed_8) {
  // Avoid the more expensive test for frequent small immediate values.
  if (Immediate <= 0xFF) {
    *RotateAmt = 0;
    *Immed_8 = Immediate;
    return true;
  }
  // Note that immediate must be unsigned for the test to work correctly.
  for (int Rot = 1; Rot < 16; Rot++) {
    uint32_t Imm8 = Utils::rotateLeft32(Immediate, 2 * Rot);
    if (Imm8 <= 0xFF) {
      *RotateAmt = Rot;
      *Immed_8 = Imm8;
      return true;
    }
  }
  return false;
}

OperandARM32FlexFpImm::OperandARM32FlexFpImm(Cfg * /*Func*/, Type Ty,
                                             uint32_t ModifiedImm)
    : OperandARM32Flex(kFlexFpImm, Ty), ModifiedImm(ModifiedImm) {}

bool OperandARM32FlexFpImm::canHoldImm(Operand *C, uint32_t *ModifiedImm) {
  switch (C->getType()) {
  default:
    llvm::report_fatal_error("Unhandled fp constant type.");
  case IceType_f32: {
    // We violate llvm naming conventions a bit here so that the constants are
    // named after the bit fields they represent. See "A7.5.1 Operation of
    // modified immediate constants, Floating-point" in the ARM ARM.
    static constexpr uint32_t a = 0x80000000u;
    static constexpr uint32_t B = 0x40000000;
    static constexpr uint32_t bbbbb = 0x3E000000;
    static constexpr uint32_t cdefgh = 0x01F80000;
    static constexpr uint32_t AllowedBits = a | B | bbbbb | cdefgh;
    static_assert(AllowedBits == 0xFFF80000u,
                  "Invalid mask for f32 modified immediates.");
    const float F32 = llvm::cast<ConstantFloat>(C)->getValue();
    const uint32_t I32 = *reinterpret_cast<const uint32_t *>(&F32);
    if (I32 & ~AllowedBits) {
      // constant has disallowed bits.
      return false;
    }

    if ((I32 & bbbbb) != bbbbb && (I32 & bbbbb)) {
      // not all bbbbb bits are 0 or 1.
      return false;
    }

    if (((I32 & B) != 0) == ((I32 & bbbbb) != 0)) {
      // B ^ b = 0;
      return false;
    }

    *ModifiedImm = ((I32 & a) ? 0x80 : 0x00) | ((I32 & bbbbb) ? 0x40 : 0x00) |
                   ((I32 & cdefgh) >> 19);
    return true;
  }
  case IceType_f64: {
    static constexpr uint32_t a = 0x80000000u;
    static constexpr uint32_t B = 0x40000000;
    static constexpr uint32_t bbbbbbbb = 0x3FC00000;
    static constexpr uint32_t cdefgh = 0x003F0000;
    static constexpr uint32_t AllowedBits = a | B | bbbbbbbb | cdefgh;
    static_assert(AllowedBits == 0xFFFF0000u,
                  "Invalid mask for f64 modified immediates.");
    const double F64 = llvm::cast<ConstantDouble>(C)->getValue();
    const uint64_t I64 = *reinterpret_cast<const uint64_t *>(&F64);
    if (I64 & 0xFFFFFFFFu) {
      // constant has disallowed bits.
      return false;
    }
    const uint32_t I32 = I64 >> 32;

    if (I32 & ~AllowedBits) {
      // constant has disallowed bits.
      return false;
    }

    if ((I32 & bbbbbbbb) != bbbbbbbb && (I32 & bbbbbbbb)) {
      // not all bbbbb bits are 0 or 1.
      return false;
    }

    if (((I32 & B) != 0) == ((I32 & bbbbbbbb) != 0)) {
      // B ^ b = 0;
      return false;
    }

    *ModifiedImm = ((I32 & a) ? 0x80 : 0x00) |
                   ((I32 & bbbbbbbb) ? 0x40 : 0x00) | ((I32 & cdefgh) >> 16);
    return true;
  }
  }
}

OperandARM32FlexFpZero::OperandARM32FlexFpZero(Cfg * /*Func*/, Type Ty)
    : OperandARM32Flex(kFlexFpZero, Ty) {}

OperandARM32FlexReg::OperandARM32FlexReg(Cfg *Func, Type Ty, Variable *Reg,
                                         ShiftKind ShiftOp, Operand *ShiftAmt)
    : OperandARM32Flex(kFlexReg, Ty), Reg(Reg), ShiftOp(ShiftOp),
      ShiftAmt(ShiftAmt) {
  NumVars = 1;
  auto *ShiftVar = llvm::dyn_cast_or_null<Variable>(ShiftAmt);
  if (ShiftVar)
    ++NumVars;
  Vars = Func->allocateArrayOf<Variable *>(NumVars);
  Vars[0] = Reg;
  if (ShiftVar)
    Vars[1] = ShiftVar;
}

InstARM32Br::InstARM32Br(Cfg *Func, const CfgNode *TargetTrue,
                         const CfgNode *TargetFalse,
                         const InstARM32Label *Label, CondARM32::Cond Pred)
    : InstARM32Pred(Func, InstARM32::Br, 0, nullptr, Pred),
      TargetTrue(TargetTrue), TargetFalse(TargetFalse), Label(Label) {}

bool InstARM32Br::optimizeBranch(const CfgNode *NextNode) {
  // If there is no next block, then there can be no fallthrough to optimize.
  if (NextNode == nullptr)
    return false;
  // Intra-block conditional branches can't be optimized.
  if (Label)
    return false;
  // If there is no fallthrough node, such as a non-default case label for a
  // switch instruction, then there is no opportunity to optimize.
  if (getTargetFalse() == nullptr)
    return false;

  // Unconditional branch to the next node can be removed.
  if (isUnconditionalBranch() && getTargetFalse() == NextNode) {
    assert(getTargetTrue() == nullptr);
    setDeleted();
    return true;
  }
  // If the fallthrough is to the next node, set fallthrough to nullptr to
  // indicate.
  if (getTargetFalse() == NextNode) {
    TargetFalse = nullptr;
    return true;
  }
  // If TargetTrue is the next node, and TargetFalse is not nullptr (which was
  // already tested above), then invert the branch condition, swap the targets,
  // and set new fallthrough to nullptr.
  if (getTargetTrue() == NextNode) {
    assert(Predicate != CondARM32::AL);
    setPredicate(getOppositeCondition(getPredicate()));
    TargetTrue = getTargetFalse();
    TargetFalse = nullptr;
    return true;
  }
  return false;
}

bool InstARM32Br::repointEdges(CfgNode *OldNode, CfgNode *NewNode) {
  bool Found = false;
  if (TargetFalse == OldNode) {
    TargetFalse = NewNode;
    Found = true;
  }
  if (TargetTrue == OldNode) {
    TargetTrue = NewNode;
    Found = true;
  }
  return Found;
}

template <InstARM32::InstKindARM32 K>
void InstARM32ThreeAddrGPR<K>::emitIAS(const Cfg *Func) const {
  emitUsingTextFixup(Func);
}

template <> void InstARM32Adc::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->adc(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Add::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->add(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32And::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->and_(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Bic::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->bic(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Eor::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->eor(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Asr::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->asr(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Lsl::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->lsl(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Lsr::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->lsr(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Orr::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->orr(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Mul::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->mul(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Rsb::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->rsb(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Rsc::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->rsc(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Sbc::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->sbc(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Sdiv::emitIAS(const Cfg *Func) const {
  assert(!SetFlags);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->sdiv(getDest(), getSrc(0), getSrc(1), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Sub::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->sub(getDest(), getSrc(0), getSrc(1), SetFlags, getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Udiv::emitIAS(const Cfg *Func) const {
  assert(!SetFlags);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->udiv(getDest(), getSrc(0), getSrc(1), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Vadd::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  const Variable *Dest = getDest();
  switch (Dest->getType()) {
  default:
    // TODO(kschimpf) Figure if more cases are needed.
    Asm->setNeedsTextFixup();
    break;
  case IceType_f32:
    Asm->vadds(getDest(), getSrc(0), getSrc(1), CondARM32::AL);
    break;
  case IceType_f64:
    Asm->vaddd(getDest(), getSrc(0), getSrc(1), CondARM32::AL);
    break;
  }
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

InstARM32Call::InstARM32Call(Cfg *Func, Variable *Dest, Operand *CallTarget)
    : InstARM32(Func, InstARM32::Call, 1, Dest) {
  HasSideEffects = true;
  addSource(CallTarget);
}

InstARM32Label::InstARM32Label(Cfg *Func, TargetARM32 *Target)
    : InstARM32(Func, InstARM32::Label, 0, nullptr),
      Number(Target->makeNextLabelNumber()) {}

IceString InstARM32Label::getName(const Cfg *Func) const {
  return ".L" + Func->getFunctionName() + "$local$__" + std::to_string(Number);
}

namespace {
// Requirements for Push/Pop:
//  1) All the Variables have the same type;
//  2) All the variables have registers assigned to them.
void validatePushOrPopRegisterListOrDie(const VarList &RegList) {
  Type PreviousTy = IceType_void;
  for (Variable *Reg : RegList) {
    if (PreviousTy != IceType_void && Reg->getType() != PreviousTy) {
      llvm::report_fatal_error("Type mismatch when popping/pushing "
                               "registers.");
    }

    if (!Reg->hasReg()) {
      llvm::report_fatal_error("Push/pop operand does not have a register "
                               "assigned to it.");
    }

    PreviousTy = Reg->getType();
  }
}
} // end of anonymous namespace

InstARM32Pop::InstARM32Pop(Cfg *Func, const VarList &Dests)
    : InstARM32(Func, InstARM32::Pop, 0, nullptr), Dests(Dests) {
  // Track modifications to Dests separately via FakeDefs. Also, a pop
  // instruction affects the stack pointer and so it should not be allowed to
  // be automatically dead-code eliminated. This is automatic since we leave
  // the Dest as nullptr.
  validatePushOrPopRegisterListOrDie(Dests);
}

InstARM32Push::InstARM32Push(Cfg *Func, const VarList &Srcs)
    : InstARM32(Func, InstARM32::Push, Srcs.size(), nullptr) {
  validatePushOrPopRegisterListOrDie(Srcs);
  for (Variable *Source : Srcs) {
    addSource(Source);
  }
}

InstARM32Ret::InstARM32Ret(Cfg *Func, Variable *LR, Variable *Source)
    : InstARM32(Func, InstARM32::Ret, Source ? 2 : 1, nullptr) {
  addSource(LR);
  if (Source)
    addSource(Source);
}

InstARM32Str::InstARM32Str(Cfg *Func, Variable *Value, OperandARM32Mem *Mem,
                           CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Str, 2, nullptr, Predicate) {
  addSource(Value);
  addSource(Mem);
}

InstARM32Strex::InstARM32Strex(Cfg *Func, Variable *Dest, Variable *Value,
                               OperandARM32Mem *Mem, CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Strex, 2, Dest, Predicate) {
  addSource(Value);
  addSource(Mem);
}

InstARM32Trap::InstARM32Trap(Cfg *Func)
    : InstARM32(Func, InstARM32::Trap, 0, nullptr) {}

InstARM32Umull::InstARM32Umull(Cfg *Func, Variable *DestLo, Variable *DestHi,
                               Variable *Src0, Variable *Src1,
                               CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Umull, 2, DestLo, Predicate),
      // DestHi is expected to have a FakeDef inserted by the lowering code.
      DestHi(DestHi) {
  addSource(Src0);
  addSource(Src1);
}

InstARM32Vcvt::InstARM32Vcvt(Cfg *Func, Variable *Dest, Variable *Src,
                             VcvtVariant Variant, CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Vcvt, 1, Dest, Predicate),
      Variant(Variant) {
  addSource(Src);
}

InstARM32Mov::InstARM32Mov(Cfg *Func, Variable *Dest, Operand *Src,
                           CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Mov, 2, Dest, Predicate) {
  auto *Dest64 = llvm::dyn_cast<Variable64On32>(Dest);
  auto *Src64 = llvm::dyn_cast<Variable64On32>(Src);

  assert(Dest64 == nullptr || Src64 == nullptr);

  if (Dest64 != nullptr) {
    // this-> is needed below because there is a parameter named Dest.
    this->Dest = Dest64->getLo();
    DestHi = Dest64->getHi();
  }

  if (Src64 == nullptr) {
    addSource(Src);
  } else {
    addSource(Src64->getLo());
    addSource(Src64->getHi());
  }
}

template <InstARM32::InstKindARM32 K>
void InstARM32CmpLike<K>::emitIAS(const Cfg *Func) const {
  emitUsingTextFixup(Func);
}

template <> void InstARM32Cmn::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 2);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->cmn(getSrc(0), getSrc(1), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Cmp::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 2);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->cmp(getSrc(0), getSrc(1), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Tst::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 2);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->tst(getSrc(0), getSrc(1), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

InstARM32Vcmp::InstARM32Vcmp(Cfg *Func, Variable *Src0, Operand *Src1,
                             CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Vcmp, 2, nullptr, Predicate) {
  HasSideEffects = true;
  addSource(Src0);
  addSource(Src1);
}

InstARM32Vmrs::InstARM32Vmrs(Cfg *Func, CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Vmrs, 0, nullptr, Predicate) {
  HasSideEffects = true;
}

InstARM32Vabs::InstARM32Vabs(Cfg *Func, Variable *Dest, Variable *Src,
                             CondARM32::Cond Predicate)
    : InstARM32Pred(Func, InstARM32::Vabs, 1, Dest, Predicate) {
  addSource(Src);
}

InstARM32Dmb::InstARM32Dmb(Cfg *Func)
    : InstARM32Pred(Func, InstARM32::Dmb, 0, nullptr, CondARM32::AL) {}

// ======================== Dump routines ======================== //

// Two-addr ops
template <> const char *InstARM32Movt::Opcode = "movt";
// Unary ops
template <> const char *InstARM32Movw::Opcode = "movw";
template <> const char *InstARM32Clz::Opcode = "clz";
template <> const char *InstARM32Mvn::Opcode = "mvn";
template <> const char *InstARM32Rbit::Opcode = "rbit";
template <> const char *InstARM32Rev::Opcode = "rev";
template <> const char *InstARM32Sxt::Opcode = "sxt"; // still requires b/h
template <> const char *InstARM32Uxt::Opcode = "uxt"; // still requires b/h
// FP
template <> const char *InstARM32Vsqrt::Opcode = "vsqrt";
// Mov-like ops
template <> const char *InstARM32Ldr::Opcode = "ldr";
template <> const char *InstARM32Ldrex::Opcode = "ldrex";
// Three-addr ops
template <> const char *InstARM32Adc::Opcode = "adc";
template <> const char *InstARM32Add::Opcode = "add";
template <> const char *InstARM32And::Opcode = "and";
template <> const char *InstARM32Asr::Opcode = "asr";
template <> const char *InstARM32Bic::Opcode = "bic";
template <> const char *InstARM32Eor::Opcode = "eor";
template <> const char *InstARM32Lsl::Opcode = "lsl";
template <> const char *InstARM32Lsr::Opcode = "lsr";
template <> const char *InstARM32Mul::Opcode = "mul";
template <> const char *InstARM32Orr::Opcode = "orr";
template <> const char *InstARM32Rsb::Opcode = "rsb";
template <> const char *InstARM32Rsc::Opcode = "rsc";
template <> const char *InstARM32Sbc::Opcode = "sbc";
template <> const char *InstARM32Sdiv::Opcode = "sdiv";
template <> const char *InstARM32Sub::Opcode = "sub";
template <> const char *InstARM32Udiv::Opcode = "udiv";
// FP
template <> const char *InstARM32Vadd::Opcode = "vadd";
template <> const char *InstARM32Vdiv::Opcode = "vdiv";
template <> const char *InstARM32Veor::Opcode = "veor";
template <> const char *InstARM32Vmla::Opcode = "vmla";
template <> const char *InstARM32Vmls::Opcode = "vmls";
template <> const char *InstARM32Vmul::Opcode = "vmul";
template <> const char *InstARM32Vsub::Opcode = "vsub";
// Four-addr ops
template <> const char *InstARM32Mla::Opcode = "mla";
template <> const char *InstARM32Mls::Opcode = "mls";
// Cmp-like ops
template <> const char *InstARM32Cmn::Opcode = "cmn";
template <> const char *InstARM32Cmp::Opcode = "cmp";
template <> const char *InstARM32Tst::Opcode = "tst";

void InstARM32::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "[ARM32] ";
  Inst::dump(Func);
}

void InstARM32Mov::emitMultiDestSingleSource(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Variable *DestLo = getDest();
  Variable *DestHi = getDestHi();
  auto *Src = llvm::cast<Variable>(getSrc(0));

  assert(DestHi->hasReg());
  assert(DestLo->hasReg());
  assert(llvm::isa<Variable>(Src) && Src->hasReg());

  Str << "\t"
         "vmov" << getPredicate() << "\t";
  DestLo->emit(Func);
  Str << ", ";
  DestHi->emit(Func);
  Str << ", ";
  Src->emit(Func);
}

void InstARM32Mov::emitSingleDestMultiSource(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Variable *Dest = getDest();
  auto *SrcLo = llvm::cast<Variable>(getSrc(0));
  auto *SrcHi = llvm::cast<Variable>(getSrc(1));

  assert(SrcHi->hasReg());
  assert(SrcLo->hasReg());
  assert(Dest->hasReg());
  assert(getSrcSize() == 2);

  Str << "\t"
         "vmov" << getPredicate() << "\t";
  Dest->emit(Func);
  Str << ", ";
  SrcLo->emit(Func);
  Str << ", ";
  SrcHi->emit(Func);
}

namespace {

bool isVariableWithoutRegister(const Operand *Op) {
  if (const auto *OpV = llvm::dyn_cast<Variable>(Op)) {
    return !OpV->hasReg();
  }
  return false;
}
bool isMemoryAccess(Operand *Op) {
  return isVariableWithoutRegister(Op) || llvm::isa<OperandARM32Mem>(Op);
}

bool isMoveBetweenCoreAndVFPRegisters(Variable *Dest, Operand *Src) {
  const Type DestTy = Dest->getType();
  const Type SrcTy = Src->getType();
  return !isVectorType(DestTy) && !isVectorType(SrcTy) &&
         (isScalarIntegerType(DestTy) == isScalarFloatingType(SrcTy));
}

} // end of anonymous namespace

void InstARM32Mov::emitSingleDestSingleSource(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Variable *Dest = getDest();

  if (!Dest->hasReg()) {
    llvm::report_fatal_error("mov can't store.");
  }

  Operand *Src0 = getSrc(0);
  if (isMemoryAccess(Src0)) {
    llvm::report_fatal_error("mov can't load.");
  }

  Type Ty = Dest->getType();
  const bool IsVector = isVectorType(Ty);
  const bool IsScalarFP = isScalarFloatingType(Ty);
  const bool CoreVFPMove = isMoveBetweenCoreAndVFPRegisters(Dest, Src0);
  const bool IsVMove = (IsVector || IsScalarFP || CoreVFPMove);
  const char *Opcode = IsVMove ? "vmov" : "mov";
  // when vmov{c}'ing, we need to emit a width string. Otherwise, the
  // assembler might be tempted to assume we want a vector vmov{c}, and that
  // is disallowed because ARM.
  const char *WidthString = !CoreVFPMove ? getVecWidthString(Ty) : "";
  Str << "\t" << Opcode;
  if (IsVMove) {
    Str << getPredicate() << WidthString;
  } else {
    Str << WidthString << getPredicate();
  }
  Str << "\t";
  Dest->emit(Func);
  Str << ", ";
  Src0->emit(Func);
}

void InstARM32Mov::emitIASSingleDestSingleSource(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Variable *Dest = getDest();
  Operand *Src0 = getSrc(0);

  if (!Dest->hasReg()) {
    llvm::report_fatal_error("mov can't store.");
  }

  if (isMemoryAccess(Src0)) {
    llvm::report_fatal_error("mov can't load.");
  }

  const Type DestTy = Dest->getType();
  const bool DestIsVector = isVectorType(DestTy);
  const bool DestIsScalarFP = isScalarFloatingType(DestTy);
  const bool CoreVFPMove = isMoveBetweenCoreAndVFPRegisters(Dest, Src0);
  if (DestIsVector || DestIsScalarFP || CoreVFPMove)
    return Asm->setNeedsTextFixup();
  return Asm->mov(Dest, Src0, getPredicate());
}

void InstARM32Mov::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  assert(!(isMultiDest() && isMultiSource()) && "Invalid vmov type.");
  if (isMultiDest()) {
    emitMultiDestSingleSource(Func);
    return;
  }

  if (isMultiSource()) {
    emitSingleDestMultiSource(Func);
    return;
  }

  emitSingleDestSingleSource(Func);
}

void InstARM32Mov::emitIAS(const Cfg *Func) const {
  assert(!(isMultiDest() && isMultiSource()) && "Invalid vmov type.");
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  if (!(isMultiDest() || isMultiSource()))
    // Must be single source/dest.
    emitIASSingleDestSingleSource(Func);
  else
    Asm->setNeedsTextFixup();
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Mov::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  assert(getSrcSize() == 1 || getSrcSize() == 2);
  Ostream &Str = Func->getContext()->getStrDump();
  Variable *Dest = getDest();
  Variable *DestHi = getDestHi();
  Dest->dump(Func);
  if (DestHi) {
    Str << ", ";
    DestHi->dump(Func);
  }

  dumpOpcodePred(Str, " = mov", getDest()->getType());
  Str << " ";

  dumpSources(Func);
}

void InstARM32Br::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t"
         "b" << getPredicate() << "\t";
  if (Label) {
    Str << Label->getName(Func);
  } else {
    if (isUnconditionalBranch()) {
      Str << getTargetFalse()->getAsmName();
    } else {
      Str << getTargetTrue()->getAsmName();
      if (getTargetFalse()) {
        startNextInst(Func);
        Str << "\n\t"
            << "b"
            << "\t" << getTargetFalse()->getAsmName();
      }
    }
  }
}

void InstARM32Br::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  if (Label) {
    Asm->b(Asm->getOrCreateLocalLabel(Label->getNumber()), getPredicate());
  } else if (isUnconditionalBranch()) {
    Asm->b(Asm->getOrCreateCfgNodeLabel(getTargetFalse()->getIndex()),
           getPredicate());
  } else {
    Asm->b(Asm->getOrCreateCfgNodeLabel(getTargetTrue()->getIndex()),
           getPredicate());
    if (const CfgNode *False = getTargetFalse())
      Asm->b(Asm->getOrCreateCfgNodeLabel(False->getIndex()), CondARM32::AL);
  }
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Br::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "br ";

  if (getPredicate() == CondARM32::AL) {
    Str << "label %"
        << (Label ? Label->getName(Func) : getTargetFalse()->getName());
    return;
  }

  if (Label) {
    Str << getPredicate() << ", label %" << Label->getName(Func);
  } else {
    Str << getPredicate() << ", label %" << getTargetTrue()->getName();
    if (getTargetFalse()) {
      Str << ", label %" << getTargetFalse()->getName();
    }
  }
}

void InstARM32Call::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 1);
  if (llvm::isa<ConstantInteger32>(getCallTarget())) {
    // This shouldn't happen (typically have to copy the full 32-bits to a
    // register and do an indirect jump).
    llvm::report_fatal_error("ARM32Call to ConstantInteger32");
  } else if (const auto *CallTarget =
                 llvm::dyn_cast<ConstantRelocatable>(getCallTarget())) {
    // Calls only have 24-bits, but the linker should insert veneers to extend
    // the range if needed.
    Str << "\t"
           "bl"
           "\t";
    CallTarget->emitWithoutPrefix(Func->getTarget());
  } else {
    Str << "\t"
           "blx"
           "\t";
    getCallTarget()->emit(Func);
  }
}

void InstARM32Call::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  if (llvm::isa<ConstantInteger32>(getCallTarget())) {
    // This shouldn't happen (typically have to copy the full 32-bits to a
    // register and do an indirect jump).
    llvm::report_fatal_error("ARM32Call to ConstantInteger32");
  } else if (const auto *CallTarget =
                 llvm::dyn_cast<ConstantRelocatable>(getCallTarget())) {
    // Calls only have 24-bits, but the linker should insert veneers to extend
    // the range if needed.
    Asm->bl(CallTarget);
  } else {
    Asm->blx(getCallTarget());
  }
  if (Asm->needsTextFixup())
    return emitUsingTextFixup(Func);
}

void InstARM32Call::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  if (getDest()) {
    dumpDest(Func);
    Str << " = ";
  }
  Str << "call ";
  getCallTarget()->dump(Func);
}

void InstARM32Label::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  // A label is not really an instruction. Hence, we need to fix the
  // emitted text size.
  if (auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>())
    Asm->decEmitTextSize(InstSize);
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << getName(Func) << ":";
}

void InstARM32Label::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->bindLocalLabel(Func, this, Number);
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Label::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << getName(Func) << ":";
}

template <InstARM32::InstKindARM32 K>
void InstARM32LoadBase<K>::emitIAS(const Cfg *Func) const {
  emitUsingTextFixup(Func);
}

template <> void InstARM32Ldr::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 1);
  assert(getDest()->hasReg());
  Variable *Dest = getDest();
  Type Ty = Dest->getType();
  const bool IsVector = isVectorType(Ty);
  const bool IsScalarFloat = isScalarFloatingType(Ty);
  const char *ActualOpcode =
      IsVector ? "vld1" : (IsScalarFloat ? "vldr" : "ldr");
  const char *VectorMarker = IsVector ? ".64" : "";
  const char *WidthString = IsVector ? "" : getWidthString(Ty);
  Str << "\t" << ActualOpcode;
  const bool IsVInst = IsVector || IsScalarFloat;
  if (IsVInst) {
    Str << getPredicate() << WidthString;
  } else {
    Str << WidthString << getPredicate();
  }
  Str << VectorMarker << "\t";
  getDest()->emit(Func);
  Str << ", ";
  getSrc(0)->emit(Func);
}

template <> void InstARM32Ldr::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  Variable *Dest = getDest();
  Type DestTy = Dest->getType();
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  if (isVectorType(DestTy) || isScalarFloatingType(DestTy))
    // TODO(kschimpf) Handle case.
    Asm->setNeedsTextFixup();
  else
    Asm->ldr(Dest, getSrc(0), getPredicate(), Func->getTarget());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Ldrex::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 1);
  assert(getDest()->hasReg());
  Variable *Dest = getDest();
  Type DestTy = Dest->getType();
  assert(isScalarIntegerType(DestTy));
  const char *WidthString = getWidthString(DestTy);
  Str << "\t" << Opcode << WidthString << getPredicate() << "\t";
  getDest()->emit(Func);
  Str << ", ";
  getSrc(0)->emit(Func);
}

template <> void InstARM32Ldrex::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  assert(getDest()->hasReg());
  Variable *Dest = getDest();
  assert(isScalarIntegerType(Dest->getType()));
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->ldrex(Dest, getSrc(0), getPredicate(), Func->getTarget());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <InstARM32::InstKindARM32 K>
void InstARM32TwoAddrGPR<K>::emitIAS(const Cfg *Func) const {
  emitUsingTextFixup(Func);
}

template <InstARM32::InstKindARM32 K, bool Nws>
void InstARM32UnaryopGPR<K, Nws>::emitIAS(const Cfg *Func) const {
  emitUsingTextFixup(Func);
}

template <> void InstARM32Rbit::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->rbit(getDest(), getSrc(0), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Rev::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->rev(getDest(), getSrc(0), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Movw::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 1);
  Str << "\t" << Opcode << getPredicate() << "\t";
  getDest()->emit(Func);
  Str << ", ";
  auto *Src0 = llvm::cast<Constant>(getSrc(0));
  if (auto *CR = llvm::dyn_cast<ConstantRelocatable>(Src0)) {
    Str << "#:lower16:";
    CR->emitWithoutPrefix(Func->getTarget());
  } else {
    Src0->emit(Func);
  }
}

template <> void InstARM32Movw::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->movw(getDest(), getSrc(0), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Movt::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 2);
  Variable *Dest = getDest();
  auto *Src1 = llvm::cast<Constant>(getSrc(1));
  Str << "\t" << Opcode << getPredicate() << "\t";
  Dest->emit(Func);
  Str << ", ";
  if (auto *CR = llvm::dyn_cast<ConstantRelocatable>(Src1)) {
    Str << "#:upper16:";
    CR->emitWithoutPrefix(Func->getTarget());
  } else {
    Src1->emit(Func);
  }
}

template <> void InstARM32Movt::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 2);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->movt(getDest(), getSrc(1), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Clz::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->clz(getDest(), getSrc(0), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Mvn::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->mvn(getDest(), getSrc(0), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Sxt::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->sxt(getDest(), getSrc(0), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

template <> void InstARM32Uxt::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 1);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->uxt(getDest(), getSrc(0), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

namespace {

bool isAssignedConsecutiveRegisters(const Variable *Before,
                                    const Variable *After) {
  assert(Before->hasReg());
  assert(After->hasReg());
  return Before->getRegNum() + 1 == After->getRegNum();
}

} // end of anonymous namespace

void InstARM32Pop::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;

  const SizeT DestSize = Dests.size();
  if (DestSize == 0) {
    assert(false && "Empty pop list");
    return;
  }

  Ostream &Str = Func->getContext()->getStrEmit();

  Variable *Reg = Dests[0];
  if (isScalarIntegerType(Reg->getType())) {
    // GPR push.
    Str << "\t"
           "pop"
           "\t{";
    Reg->emit(Func);
    for (SizeT i = 1; i < DestSize; ++i) {
      Str << ", ";
      Reg = Dests[i];
      Reg->emit(Func);
    }
    Str << "}";
    return;
  }

  // VFP "s" reg push.
  SizeT End = DestSize - 1;
  SizeT Start = DestSize - 1;
  Reg = Dests[DestSize - 1];
  Str << "\t"
         "vpop"
         "\t{";
  for (SizeT i = 2; i <= DestSize; ++i) {
    Variable *PreviousReg = Dests[DestSize - i];
    if (!isAssignedConsecutiveRegisters(PreviousReg, Reg)) {
      Dests[Start]->emit(Func);
      for (SizeT j = Start + 1; j <= End; ++j) {
        Str << ", ";
        Dests[j]->emit(Func);
      }
      startNextInst(Func);
      Str << "}\n\t"
             "vpop"
             "\t{";
      End = DestSize - i;
    }
    Reg = PreviousReg;
    Start = DestSize - i;
  }
  Dests[Start]->emit(Func);
  for (SizeT j = Start + 1; j <= End; ++j) {
    Str << ", ";
    Dests[j]->emit(Func);
  }
  Str << "}";
}

void InstARM32Pop::emitIAS(const Cfg *Func) const {
  // Pop can't be emitted if there are no registers to load. This should never
  // happen, but if it does, we don't need to bring Subzero down -- we just skip
  // emitting the pop instruction (and maybe emit a nop?) The assert() is here
  // so that we can detect this error during development.
  const SizeT DestSize = Dests.size();
  if (DestSize == 0) {
    assert(false && "Empty pop list");
    return;
  }

  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  const auto *Reg = llvm::cast<Variable>(Dests[0]);
  if (isScalarIntegerType(Reg->getType())) {
    // Pop GPR registers.
    SizeT IntegerCount = 0;
    ARM32::IValueT GPRegisters = 0;
    const Variable *LastDest = nullptr;
    for (const Variable *Var : Dests) {
      assert(Var->hasReg() && "pop only applies to registers");
      int32_t Reg = RegARM32::getEncodedGPReg(Var->getRegNum());
      LastDest = Var;
      GPRegisters |= (1 << Reg);
      ++IntegerCount;
    }
    switch (IntegerCount) {
    case 0:
      return;
    case 1:
      // Note: Can only apply pop register if single register is not sp.
      assert((RegARM32::Encoded_Reg_sp != LastDest->getRegNum()) &&
             "Effects of pop register SP is undefined!");
      Asm->pop(LastDest, CondARM32::AL);
      break;
    default:
      Asm->popList(GPRegisters, CondARM32::AL);
      break;
    }
  } else {
    // Pop vector/floating point registers.
    const Variable *BaseReg = nullptr;
    SizeT RegCount = 0;
    for (const Variable *NextReg : Dests) {
      if (BaseReg == nullptr) {
        BaseReg = NextReg;
        RegCount = 1;
      } else if (RegCount < VpushVpopMaxConsecRegs &&
                 isAssignedConsecutiveRegisters(Reg, NextReg)) {
        ++RegCount;
      } else {
        Asm->vpop(BaseReg, RegCount, CondARM32::AL);
        BaseReg = NextReg;
        RegCount = 1;
      }
      Reg = NextReg;
    }
    if (RegCount)
      Asm->vpop(BaseReg, RegCount, CondARM32::AL);
  }
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Pop::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "pop"
      << " ";
  for (SizeT I = 0; I < Dests.size(); ++I) {
    if (I > 0)
      Str << ", ";
    Dests[I]->dump(Func);
  }
}

void InstARM32Push::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;

  // Push can't be emitted if there are no registers to save. This should never
  // happen, but if it does, we don't need to bring Subzero down -- we just skip
  // emitting the push instruction (and maybe emit a nop?) The assert() is here
  // so that we can detect this error during development.
  const SizeT SrcSize = getSrcSize();
  if (SrcSize == 0) {
    assert(false && "Empty push list");
    return;
  }

  Ostream &Str = Func->getContext()->getStrEmit();

  const auto *Reg = llvm::cast<Variable>(getSrc(0));
  if (isScalarIntegerType(Reg->getType())) {
    // GPR push.
    Str << "\t"
           "push"
           "\t{";
    Reg->emit(Func);
    for (SizeT i = 1; i < SrcSize; ++i) {
      Str << ", ";
      getSrc(i)->emit(Func);
    }
    Str << "}";
    return;
  }

  // VFP "s" reg push.
  Str << "\t"
         "vpush"
         "\t{";
  Reg->emit(Func);
  SizeT RegCount = 1;
  for (SizeT i = 1; i < SrcSize; ++i) {
    const auto *NextReg = llvm::cast<Variable>(getSrc(i));
    if (RegCount < VpushVpopMaxConsecRegs &&
        isAssignedConsecutiveRegisters(Reg, NextReg)) {
      ++RegCount;
      Str << ", ";
    } else {
      startNextInst(Func);
      RegCount = 1;
      Str << "}\n\t"
             "vpush"
             "\t{";
    }
    Reg = NextReg;
    Reg->emit(Func);
  }
  Str << "}";
}

void InstARM32Push::emitIAS(const Cfg *Func) const {
  // Push can't be emitted if there are no registers to save. This should never
  // happen, but if it does, we don't need to bring Subzero down -- we just skip
  // emitting the push instruction (and maybe emit a nop?) The assert() is here
  // so that we can detect this error during development.
  const SizeT SrcSize = getSrcSize();
  if (SrcSize == 0) {
    assert(false && "Empty push list");
    return;
  }

  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  const auto *Reg = llvm::cast<Variable>(getSrc(0));
  if (isScalarIntegerType(Reg->getType())) {
    // Push GPR registers.
    SizeT IntegerCount = 0;
    ARM32::IValueT GPRegisters = 0;
    const Variable *LastSrc = nullptr;
    for (SizeT Index = 0; Index < getSrcSize(); ++Index) {
      const auto *Var = llvm::cast<Variable>(getSrc(Index));
      int32_t Reg = RegARM32::getEncodedGPReg(Var->getRegNum());
      assert(Reg != RegARM32::Encoded_Not_GPR);
      LastSrc = Var;
      GPRegisters |= (1 << Reg);
      ++IntegerCount;
    }
    switch (IntegerCount) {
    case 0:
      return;
    case 1: {
      // Note: Can only apply push register if single register is not sp.
      assert((RegARM32::Encoded_Reg_sp != LastSrc->getRegNum()) &&
             "Effects of push register SP is undefined!");
      Asm->push(LastSrc, CondARM32::AL);
      break;
    }
    default:
      Asm->pushList(GPRegisters, CondARM32::AL);
      break;
    }
  } else {
    // Push vector/Floating point registers.
    const Variable *BaseReg = Reg;
    SizeT RegCount = 1;
    for (SizeT i = 1; i < SrcSize; ++i) {
      const auto *NextReg = llvm::cast<Variable>(getSrc(i));
      if (RegCount < VpushVpopMaxConsecRegs &&
          isAssignedConsecutiveRegisters(Reg, NextReg)) {
        ++RegCount;
      } else {
        Asm->vpush(BaseReg, RegCount, CondARM32::AL);
        BaseReg = NextReg;
        RegCount = 1;
      }
      Reg = NextReg;
    }
    Asm->vpush(BaseReg, RegCount, CondARM32::AL);
  }
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Push::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "push"
      << " ";
  dumpSources(Func);
}

void InstARM32Ret::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  assert(getSrcSize() > 0);
  auto *LR = llvm::cast<Variable>(getSrc(0));
  assert(LR->hasReg());
  assert(LR->getRegNum() == RegARM32::Reg_lr);
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "\t"
         "bx"
         "\t";
  LR->emit(Func);
}

void InstARM32Ret::emitIAS(const Cfg *Func) const {
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->bx(RegARM32::Encoded_Reg_lr);
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Ret::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Type Ty = (getSrcSize() == 1 ? IceType_void : getSrc(0)->getType());
  Str << "ret." << Ty << " ";
  dumpSources(Func);
}

void InstARM32Str::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 2);
  Type Ty = getSrc(0)->getType();
  const bool IsVectorStore = isVectorType(Ty);
  const bool IsScalarFloat = isScalarFloatingType(Ty);
  const char *Opcode =
      IsVectorStore ? "vst1" : (IsScalarFloat ? "vstr" : "str");
  const char *VecEltWidthString = IsVectorStore ? ".64" : "";
  Str << "\t" << Opcode;
  const bool IsVInst = IsVectorStore || IsScalarFloat;
  if (IsVInst) {
    Str << getPredicate() << getWidthString(Ty);
  } else {
    Str << getWidthString(Ty) << getPredicate();
  }
  Str << VecEltWidthString << "\t";
  getSrc(0)->emit(Func);
  Str << ", ";
  getSrc(1)->emit(Func);
}

void InstARM32Str::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 2);
  Type Ty = getSrc(0)->getType();
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  if (isVectorType(Ty) || isScalarFloatingType(Ty))
    // TODO(kschimpf) Handle case.
    Asm->setNeedsTextFixup();
  else
    Asm->str(getSrc(0), getSrc(1), getPredicate(), Func->getTarget());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Str::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Type Ty = getSrc(0)->getType();
  dumpOpcodePred(Str, "str", Ty);
  Str << " ";
  getSrc(1)->dump(Func);
  Str << ", ";
  getSrc(0)->dump(Func);
}

void InstARM32Strex::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  assert(getSrcSize() == 2);
  Type Ty = getSrc(0)->getType();
  assert(isScalarIntegerType(Ty));
  Variable *Dest = getDest();
  Ostream &Str = Func->getContext()->getStrEmit();
  static constexpr char Opcode[] = "strex";
  const char *WidthString = getWidthString(Ty);
  Str << "\t" << Opcode << WidthString << getPredicate() << "\t";
  Dest->emit(Func);
  Str << ", ";
  emitSources(Func);
}

void InstARM32Strex::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 2);
  const Operand *Src0 = getSrc(0);
  assert(isScalarIntegerType(Src0->getType()));
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->strex(Dest, Src0, getSrc(1), getPredicate(), Func->getTarget());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Strex::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Variable *Dest = getDest();
  Dest->dump(Func);
  Str << " = ";
  Type Ty = getSrc(0)->getType();
  dumpOpcodePred(Str, "strex", Ty);
  Str << " ";
  getSrc(1)->dump(Func);
  Str << ", ";
  getSrc(0)->dump(Func);
}

void InstARM32Trap::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 0);
  // There isn't a mnemonic for the special NaCl Trap encoding, so dump
  // the raw bytes.
  Str << "\t.long 0x";
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  for (uint8_t I : Asm->getNonExecBundlePadding()) {
    Str.write_hex(I);
  }
}

void InstARM32Trap::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "trap";
}

void InstARM32Umull::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 2);
  assert(getDest()->hasReg());
  Str << "\t"
         "umull" << getPredicate() << "\t";
  getDest()->emit(Func);
  Str << ", ";
  DestHi->emit(Func);
  Str << ", ";
  getSrc(0)->emit(Func);
  Str << ", ";
  getSrc(1)->emit(Func);
}

void InstARM32Umull::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 2);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  Asm->umull(getDest(), DestHi, getSrc(0), getSrc(1), getPredicate());
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Umull::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  dumpDest(Func);
  Str << " = ";
  dumpOpcodePred(Str, "umull", getDest()->getType());
  Str << " ";
  dumpSources(Func);
}

namespace {
const char *vcvtVariantSuffix(const InstARM32Vcvt::VcvtVariant Variant) {
  switch (Variant) {
  case InstARM32Vcvt::S2si:
    return ".s32.f32";
  case InstARM32Vcvt::S2ui:
    return ".u32.f32";
  case InstARM32Vcvt::Si2s:
    return ".f32.s32";
  case InstARM32Vcvt::Ui2s:
    return ".f32.u32";
  case InstARM32Vcvt::D2si:
    return ".s32.f64";
  case InstARM32Vcvt::D2ui:
    return ".u32.f64";
  case InstARM32Vcvt::Si2d:
    return ".f64.s32";
  case InstARM32Vcvt::Ui2d:
    return ".f64.u32";
  case InstARM32Vcvt::S2d:
    return ".f64.f32";
  case InstARM32Vcvt::D2s:
    return ".f32.f64";
  }
  llvm::report_fatal_error("Invalid VcvtVariant enum.");
}
} // end of anonymous namespace

void InstARM32Vcvt::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 1);
  assert(getDest()->hasReg());
  Str << "\t"
         "vcvt" << getPredicate() << vcvtVariantSuffix(Variant) << "\t";
  getDest()->emit(Func);
  Str << ", ";
  getSrc(0)->emit(Func);
}

void InstARM32Vcvt::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  dumpDest(Func);
  Str << " = "
      << "vcvt" << getPredicate() << vcvtVariantSuffix(Variant) << " ";
  dumpSources(Func);
}

void InstARM32Vcmp::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 2);
  Str << "\t"
         "vcmp" << getPredicate() << getVecWidthString(getSrc(0)->getType())
      << "\t";
  getSrc(0)->emit(Func);
  Str << ", ";
  getSrc(1)->emit(Func);
}

void InstARM32Vcmp::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "vcmp" << getPredicate() << getVecWidthString(getSrc(0)->getType());
  dumpSources(Func);
}

void InstARM32Vmrs::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 0);
  Str << "\t"
         "vmrs" << getPredicate() << "\t"
                                     "APSR_nzcv"
                                     ", "
                                     "FPSCR";
}

void InstARM32Vmrs::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  Str << "APSR{n,z,v,c} = vmrs" << getPredicate() << "\t"
                                                     "FPSCR{n,z,c,v}";
}

void InstARM32Vabs::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 1);
  Str << "\t"
         "vabs" << getPredicate() << getVecWidthString(getSrc(0)->getType())
      << "\t";
  getDest()->emit(Func);
  Str << ", ";
  getSrc(0)->emit(Func);
}

void InstARM32Vabs::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrDump();
  dumpDest(Func);
  Str << " = vabs" << getPredicate() << getVecWidthString(getSrc(0)->getType());
}

void InstARM32Dmb::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  assert(getSrcSize() == 0);
  Str << "\t"
         "dmb"
         "\t"
         "sy";
}

void InstARM32Dmb::emitIAS(const Cfg *Func) const {
  assert(getSrcSize() == 0);
  auto *Asm = Func->getAssembler<ARM32::AssemblerARM32>();
  constexpr ARM32::IValueT SyOption = 0xF; // i.e. 1111
  Asm->dmb(SyOption);
  if (Asm->needsTextFixup())
    emitUsingTextFixup(Func);
}

void InstARM32Dmb::dump(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Func->getContext()->getStrDump() << "dmb\t"
                                      "sy";
}

void OperandARM32Mem::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  Str << "[";
  getBase()->emit(Func);
  switch (getAddrMode()) {
  case PostIndex:
  case NegPostIndex:
    Str << "]";
    break;
  default:
    break;
  }
  if (isRegReg()) {
    Str << ", ";
    if (isNegAddrMode()) {
      Str << "-";
    }
    getIndex()->emit(Func);
    if (getShiftOp() != kNoShift) {
      Str << ", " << InstARM32ShiftAttributes[getShiftOp()].EmitString << " #"
          << getShiftAmt();
    }
  } else {
    ConstantInteger32 *Offset = getOffset();
    if (Offset && Offset->getValue() != 0) {
      Str << ", ";
      Offset->emit(Func);
    }
  }
  switch (getAddrMode()) {
  case Offset:
  case NegOffset:
    Str << "]";
    break;
  case PreIndex:
  case NegPreIndex:
    Str << "]!";
    break;
  case PostIndex:
  case NegPostIndex:
    // Brace is already closed off.
    break;
  }
}

void OperandARM32Mem::dump(const Cfg *Func, Ostream &Str) const {
  if (!BuildDefs::dump())
    return;
  Str << "[";
  if (Func)
    getBase()->dump(Func);
  else
    getBase()->dump(Str);
  Str << ", ";
  if (isRegReg()) {
    if (isNegAddrMode()) {
      Str << "-";
    }
    if (Func)
      getIndex()->dump(Func);
    else
      getIndex()->dump(Str);
    if (getShiftOp() != kNoShift) {
      Str << ", " << InstARM32ShiftAttributes[getShiftOp()].EmitString << " #"
          << getShiftAmt();
    }
  } else {
    getOffset()->dump(Func, Str);
  }
  Str << "] AddrMode==" << getAddrMode();
}

void OperandARM32ShAmtImm::emit(const Cfg *Func) const { ShAmt->emit(Func); }

void OperandARM32ShAmtImm::dump(const Cfg *, Ostream &Str) const {
  ShAmt->dump(Str);
}

OperandARM32FlexImm *OperandARM32FlexImm::create(Cfg *Func, Type Ty,
                                                 uint32_t Imm,
                                                 uint32_t RotateAmt) {
  // The assembler wants the smallest rotation. Rotate if needed. Note: Imm is
  // an 8-bit value.
  assert(Utils::IsUint(8, Imm) &&
         "Flex immediates can only be defined on 8-bit immediates");
  while ((Imm & 0x03) == 0 && RotateAmt > 0) {
    --RotateAmt;
    Imm = Imm >> 2;
  }
  return new (Func->allocate<OperandARM32FlexImm>())
      OperandARM32FlexImm(Func, Ty, Imm, RotateAmt);
}

void OperandARM32FlexImm::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  uint32_t Imm = getImm();
  uint32_t RotateAmt = getRotateAmt();
  Str << "#" << Utils::rotateRight32(Imm, 2 * RotateAmt);
}

void OperandARM32FlexImm::dump(const Cfg * /* Func */, Ostream &Str) const {
  if (!BuildDefs::dump())
    return;
  uint32_t Imm = getImm();
  uint32_t RotateAmt = getRotateAmt();
  Str << "#(" << Imm << " ror 2*" << RotateAmt << ")";
}

namespace {
static constexpr uint32_t a = 0x80;
static constexpr uint32_t b = 0x40;
static constexpr uint32_t cdefgh = 0x3F;
static constexpr uint32_t AllowedBits = a | b | cdefgh;
static_assert(AllowedBits == 0xFF,
              "Invalid mask for f32/f64 constant rematerialization.");

// There's no loss in always returning the modified immediate as float.
// TODO(jpp): returning a double causes problems when outputting the constants
// for filetype=asm. Why?
float materializeFloatImmediate(uint32_t ModifiedImm) {
  const uint32_t Ret = ((ModifiedImm & a) ? 0x80000000 : 0) |
                       ((ModifiedImm & b) ? 0x3E000000 : 0x40000000) |
                       ((ModifiedImm & cdefgh) << 19);
  return *reinterpret_cast<const float *>(&Ret);
}

} // end of anonymous namespace

void OperandARM32FlexFpImm::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  switch (Ty) {
  default:
    llvm::report_fatal_error("Invalid flex fp imm type.");
  case IceType_f64:
  case IceType_f32:
    Str << "#" << materializeFloatImmediate(ModifiedImm)
        << " @ Modified: " << ModifiedImm;
    break;
  }
}

void OperandARM32FlexFpImm::dump(const Cfg * /*Func*/, Ostream &Str) const {
  if (!BuildDefs::dump())
    return;
  Str << "#" << materializeFloatImmediate(ModifiedImm)
      << InstARM32::getVecWidthString(Ty);
}

void OperandARM32FlexFpZero::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  switch (Ty) {
  default:
    llvm::report_fatal_error("Invalid flex fp imm type.");
  case IceType_f64:
  case IceType_f32:
    Str << "#0.0";
  }
}

void OperandARM32FlexFpZero::dump(const Cfg * /*Func*/, Ostream &Str) const {
  if (!BuildDefs::dump())
    return;
  Str << "#0.0" << InstARM32::getVecWidthString(Ty);
}

void OperandARM32FlexReg::emit(const Cfg *Func) const {
  if (!BuildDefs::dump())
    return;
  Ostream &Str = Func->getContext()->getStrEmit();
  getReg()->emit(Func);
  if (getShiftOp() != kNoShift) {
    Str << ", " << InstARM32ShiftAttributes[getShiftOp()].EmitString << " ";
    getShiftAmt()->emit(Func);
  }
}

void OperandARM32FlexReg::dump(const Cfg *Func, Ostream &Str) const {
  if (!BuildDefs::dump())
    return;
  Variable *Reg = getReg();
  if (Func)
    Reg->dump(Func);
  else
    Reg->dump(Str);
  if (getShiftOp() != kNoShift) {
    Str << ", " << InstARM32ShiftAttributes[getShiftOp()].EmitString << " ";
    if (Func)
      getShiftAmt()->dump(Func);
    else
      getShiftAmt()->dump(Str);
  }
}

// Force instantition of template classes
template class InstARM32ThreeAddrGPR<InstARM32::Adc>;
template class InstARM32ThreeAddrGPR<InstARM32::Add>;
template class InstARM32ThreeAddrGPR<InstARM32::And>;
template class InstARM32ThreeAddrGPR<InstARM32::Asr>;
template class InstARM32ThreeAddrGPR<InstARM32::Bic>;
template class InstARM32ThreeAddrGPR<InstARM32::Eor>;
template class InstARM32ThreeAddrGPR<InstARM32::Lsl>;
template class InstARM32ThreeAddrGPR<InstARM32::Lsr>;
template class InstARM32ThreeAddrGPR<InstARM32::Mul>;
template class InstARM32ThreeAddrGPR<InstARM32::Orr>;
template class InstARM32ThreeAddrGPR<InstARM32::Rsb>;
template class InstARM32ThreeAddrGPR<InstARM32::Rsc>;
template class InstARM32ThreeAddrGPR<InstARM32::Sbc>;
template class InstARM32ThreeAddrGPR<InstARM32::Sdiv>;
template class InstARM32ThreeAddrGPR<InstARM32::Sub>;
template class InstARM32ThreeAddrGPR<InstARM32::Udiv>;

template class InstARM32ThreeAddrFP<InstARM32::Vadd>;
template class InstARM32ThreeAddrFP<InstARM32::Vdiv>;
template class InstARM32ThreeAddrFP<InstARM32::Veor>;
template class InstARM32ThreeAddrFP<InstARM32::Vmul>;
template class InstARM32ThreeAddrFP<InstARM32::Vmla>;
template class InstARM32ThreeAddrFP<InstARM32::Vmls>;
template class InstARM32ThreeAddrFP<InstARM32::Vsub>;

template class InstARM32LoadBase<InstARM32::Ldr>;
template class InstARM32LoadBase<InstARM32::Ldrex>;

template class InstARM32TwoAddrGPR<InstARM32::Movt>;

template class InstARM32UnaryopGPR<InstARM32::Movw, false>;
template class InstARM32UnaryopGPR<InstARM32::Clz, false>;
template class InstARM32UnaryopGPR<InstARM32::Mvn, false>;
template class InstARM32UnaryopGPR<InstARM32::Rbit, false>;
template class InstARM32UnaryopGPR<InstARM32::Rev, false>;
template class InstARM32UnaryopGPR<InstARM32::Sxt, true>;
template class InstARM32UnaryopGPR<InstARM32::Uxt, true>;
template class InstARM32UnaryopFP<InstARM32::Vsqrt>;

template class InstARM32FourAddrGPR<InstARM32::Mla>;
template class InstARM32FourAddrGPR<InstARM32::Mls>;

template class InstARM32CmpLike<InstARM32::Cmn>;
template class InstARM32CmpLike<InstARM32::Cmp>;
template class InstARM32CmpLike<InstARM32::Tst>;

} // end of namespace ARM32
} // end of namespace Ice
