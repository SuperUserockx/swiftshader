//===- subzero/src/IceRegistersARM32.h - Register information ---*- C++ -*-===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Declares the registers and their encodings for ARM32.
///
//===----------------------------------------------------------------------===//

#ifndef SUBZERO_SRC_ICEREGISTERSARM32_H
#define SUBZERO_SRC_ICEREGISTERSARM32_H

#include "IceDefs.h"
#include "IceInstARM32.def"
#include "IceOperand.h" // RC_Target
#include "IceTypes.h"

namespace Ice {
namespace ARM32 {

/// SizeOf is used to obtain the size of an initializer list as a constexpr
/// expression. This is only needed until our C++ library is updated to
/// C++ 14 -- which defines constexpr members to std::initializer_list.
class SizeOf {
  SizeOf(const SizeOf &) = delete;
  SizeOf &operator=(const SizeOf &) = delete;

public:
  constexpr SizeOf() : Size(0) {}
  template <typename... T>
  explicit constexpr SizeOf(T...)
      : Size(__length<T...>::value) {}
  constexpr SizeT size() const { return Size; }

private:
  template <typename T, typename... U> struct __length {
    static constexpr std::size_t value = 1 + __length<U...>::value;
  };

  template <typename T> struct __length<T> {
    static constexpr std::size_t value = 1;
  };

  const std::size_t Size;
};

class RegARM32 {
private:
  RegARM32() = delete;
  RegARM32(const RegARM32 &) = delete;
  RegARM32 &operator=(const RegARM32 &) = delete;
  ~RegARM32() = delete;

public:
  /// An enum of every register. The enum value may not match the encoding used
  /// to binary encode register operands in instructions.
  enum AllRegisters {
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  val,
    REGARM32_TABLE
#undef X
        Reg_NUM,
#define X(val, init) val init,
    REGARM32_TABLE_BOUNDS
#undef X
  };

  /// An enum of GPR Registers. The enum value does match the encoding used to
  /// binary encode register operands in instructions.
  enum GPRRegister {
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  Encoded_##val = encode,
    REGARM32_GPR_TABLE
#undef X
        Encoded_Not_GPR = -1
  };

  /// An enum of FP32 S-Registers. The enum value does match the encoding used
  /// to binary encode register operands in instructions.
  enum SRegister {
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  Encoded_##val = encode,
    REGARM32_FP32_TABLE
#undef X
        Encoded_Not_SReg = -1
  };

  /// An enum of FP64 D-Registers. The enum value does match the encoding used
  /// to binary encode register operands in instructions.
  enum DRegister {
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  Encoded_##val = encode,
    REGARM32_FP64_TABLE
#undef X
        Encoded_Not_DReg = -1
  };

  /// An enum of 128-bit Q-Registers. The enum value does match the encoding
  /// used to binary encode register operands in instructions.
  enum QRegister {
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  Encoded_##val = encode,
    REGARM32_VEC128_TABLE
#undef X
        Encoded_Not_QReg = -1
  };

  static constexpr struct TableType {
    const char *Name;
    int32_t Encoding : 10;
    int32_t CCArg : 6;
    int32_t Scratch : 1;
    int32_t Preserved : 1;
    int32_t StackPtr : 1;
    int32_t FramePtr : 1;
    int32_t IsGPR : 1;
    int32_t IsInt : 1;
    int32_t IsI64Pair : 1;
    int32_t IsFP32 : 1;
    int32_t IsFP64 : 1;
    int32_t IsVec128 : 1;
#define NUM_ALIASES_BITS 3
    SizeT NumAliases : (NUM_ALIASES_BITS + 1);
    uint16_t Aliases[1 << NUM_ALIASES_BITS];
#undef NUM_ALIASES_BITS
  } Table[Reg_NUM] = {
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  {                                                                            \
    name, encode, cc_arg, scratch, preserved, stackptr, frameptr, isGPR,       \
        isInt, isI64Pair, isFP32, isFP64, isVec128,                            \
        (SizeOf alias_init).size(), alias_init                                 \
  }                                                                            \
  ,
      REGARM32_TABLE
#undef X
  };

  static inline void assertRegisterDefined(int32_t RegNum) {
    (void)RegNum;
    assert(RegNum >= 0);
    assert(RegNum < Reg_NUM);
  }

  static inline bool isGPRegister(int32_t RegNum) {
    assertRegisterDefined(RegNum);
    return Table[RegNum].IsGPR;
  }

  static constexpr inline SizeT getNumGPRegs() {
    return 0
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  +(isGPR)
        REGARM32_TABLE
#undef X
        ;
  }

  static inline GPRRegister getEncodedGPReg(int32_t RegNum) {
    assert(isGPRegister(RegNum));
    return GPRRegister(Table[RegNum].Encoding);
  }

  static inline GPRRegister getI64PairFirstGPRNum(int32_t RegNum) {
    assert(isI64RegisterPair(RegNum));
    return GPRRegister(Table[RegNum].Encoding);
  }

  static inline GPRRegister getI64PairSecondGPRNum(int32_t RegNum) {
    assert(isI64RegisterPair(RegNum));
    return GPRRegister(Table[RegNum].Encoding + 1);
  }

  static inline bool isI64RegisterPair(int32_t RegNum) {
    assertRegisterDefined(RegNum);
    return Table[RegNum].IsI64Pair;
  }

  static inline bool isEncodedSReg(int32_t RegNum) {
    assertRegisterDefined(RegNum);
    return Table[RegNum].IsFP32;
  }

  static constexpr inline SizeT getNumSRegs() {
    return 0
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  +(isFP32)
        REGARM32_TABLE
#undef X
        ;
  }

  static inline SRegister getEncodedSReg(int32_t RegNum) {
    assert(isEncodedSReg(RegNum));
    return SRegister(Table[RegNum].Encoding);
  }

  static inline bool isEncodedDReg(int32_t RegNum) {
    assertRegisterDefined(RegNum);
    return Table[RegNum].IsFP64;
  }

  static constexpr inline SizeT getNumDRegs() {
    return 0
#define X(val, encode, name, cc_arg, scratch, preserved, stackptr, frameptr,   \
          isGPR, isInt, isI64Pair, isFP32, isFP64, isVec128, alias_init)       \
  +(isFP64)
        REGARM32_TABLE
#undef X
        ;
  }

  static inline DRegister getEncodedDReg(int32_t RegNum) {
    assert(isEncodedDReg(RegNum));
    return DRegister(Table[RegNum].Encoding);
  }

  static inline bool isEncodedQReg(int32_t RegNum) {
    assertRegisterDefined(RegNum);
    return Table[RegNum].IsVec128;
  }

  static inline QRegister getEncodedQReg(int32_t RegNum) {
    assert(isEncodedQReg(RegNum));
    return QRegister(Table[RegNum].Encoding);
  }

  static inline IceString getRegName(int32_t RegNum) {
    assertRegisterDefined(RegNum);
    return Table[RegNum].Name;
  }
};

// Extend enum RegClass with ARM32-specific register classes (if any).
enum RegClassARM32 : uint8_t { RCARM32_NUM = RC_Target };

} // end of namespace ARM32
} // end of namespace Ice

#endif // SUBZERO_SRC_ICEREGISTERSARM32_H
