//===- subzero/crosstest/test_vector_ops.h - Test prototypes ----*- C++ -*-===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the function prototypes for crosstesting insertelement
// and extractelement operations.
//
//===----------------------------------------------------------------------===//

#ifndef TEST_VECTOR_OPS_H
#define TEST_VECTOR_OPS_H

#include "vectors.h"

// The VectorOps<> class acts like Vectors<> but also has insertelement,
// Subzero_insertelement, extractelement, and Subzero_extractelement
// fields.

template <typename T> struct VectorOps;
#define FIELD(TYNAME, FIELDNAME) VectorOps<TYNAME>::FIELDNAME
#define TY(TYNAME) FIELD(TYNAME, Ty)
#define CASTTY(TYNAME) FIELD(TYNAME, CastTy)
#define DECLARE_VECTOR_OPS(NAME)                                               \
  template <> struct VectorOps<NAME> : public Vectors<NAME> {                  \
    static Ty (*insertelement)(Ty, CastTy, int32_t);                           \
    static CastTy (*extractelement)(Ty, int32_t);                              \
    static Ty (*Subzero_insertelement)(Ty, CastTy, int32_t);                   \
    static CastTy (*Subzero_extractelement)(Ty, int32_t);                      \
  };                                                                           \
  extern "C" {                                                                 \
  TY(NAME) insertelement_##NAME(TY(NAME), CASTTY(NAME), int32_t);              \
  TY(NAME) Subzero_insertelement_##NAME(TY(NAME), CASTTY(NAME), int32_t);      \
  CASTTY(NAME) extractelement_##NAME(TY(NAME), int32_t);                       \
  CASTTY(NAME) Subzero_extractelement_##NAME(TY(NAME), int32_t);               \
  }                                                                            \
  TY(NAME) (*FIELD(NAME, insertelement))(TY(NAME), CASTTY(NAME), int32_t) =    \
      &insertelement_##NAME;                                                   \
  TY(NAME) (*FIELD(NAME, Subzero_insertelement))(                              \
      TY(NAME), CASTTY(NAME), int32_t) = &Subzero_insertelement_##NAME;        \
  CASTTY(NAME) (*FIELD(NAME, extractelement))(TY(NAME), int32_t) =             \
      &extractelement_##NAME;                                                  \
  CASTTY(NAME) (*FIELD(NAME, Subzero_extractelement))(TY(NAME), int32_t) =     \
      &Subzero_extractelement_##NAME;

#define X(ty, eltty, castty) DECLARE_VECTOR_OPS(ty)
VECTOR_TYPE_TABLE
#undef X

#define X(ty, eltty, numelements) DECLARE_VECTOR_OPS(ty)
I1_VECTOR_TYPE_TABLE
#undef X

#endif // TEST_VECTOR_OPS_H
