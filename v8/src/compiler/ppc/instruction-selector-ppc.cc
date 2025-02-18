// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/instruction-selector-impl.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

enum ImmediateMode {
  kInt16Imm,
  kInt16Imm_Unsigned,
  kInt16Imm_Negate,
  kInt16Imm_4ByteAligned,
  kShift32Imm,
  kShift64Imm,
  kNoImmediate
};


// Adds PPC-specific methods for generating operands.
class PPCOperandGenerator FINAL : public OperandGenerator {
 public:
  explicit PPCOperandGenerator(InstructionSelector* selector)
      : OperandGenerator(selector) {}

  InstructionOperand UseOperand(Node* node, ImmediateMode mode) {
    if (CanBeImmediate(node, mode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  bool CanBeImmediate(Node* node, ImmediateMode mode) {
    int64_t value;
    if (node->opcode() == IrOpcode::kInt32Constant)
      value = OpParameter<int32_t>(node);
    else if (node->opcode() == IrOpcode::kInt64Constant)
      value = OpParameter<int64_t>(node);
    else
      return false;
    return CanBeImmediate(value, mode);
  }

  bool CanBeImmediate(int64_t value, ImmediateMode mode) {
    switch (mode) {
      case kInt16Imm:
        return is_int16(value);
      case kInt16Imm_Unsigned:
        return is_uint16(value);
      case kInt16Imm_Negate:
        return is_int16(-value);
      case kInt16Imm_4ByteAligned:
        return is_int16(value) && !(value & 3);
      case kShift32Imm:
        return 0 <= value && value < 32;
      case kShift64Imm:
        return 0 <= value && value < 64;
      case kNoImmediate:
        return false;
    }
    return false;
  }
};


static void VisitRRFloat64(InstructionSelector* selector, ArchOpcode opcode,
                           Node* node) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)));
}


static void VisitRRR(InstructionSelector* selector, Node* node,
                     ArchOpcode opcode) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseRegister(node->InputAt(1)));
}


static void VisitRRRFloat64(InstructionSelector* selector, Node* node,
                            ArchOpcode opcode) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseRegister(node->InputAt(1)));
}


static void VisitRRO(InstructionSelector* selector, Node* node,
                     ArchOpcode opcode, ImmediateMode operand_mode) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseOperand(node->InputAt(1), operand_mode));
}


// Shared routine for multiple binary operations.
template <typename Matcher>
static void VisitBinop(InstructionSelector* selector, Node* node,
                       InstructionCode opcode, ImmediateMode operand_mode,
                       FlagsContinuation* cont) {
  PPCOperandGenerator g(selector);
  Matcher m(node);
  InstructionOperand inputs[4];
  size_t input_count = 0;
  InstructionOperand outputs[2];
  size_t output_count = 0;

  inputs[input_count++] = g.UseRegister(m.left().node());
  inputs[input_count++] = g.UseOperand(m.right().node(), operand_mode);

  if (cont->IsBranch()) {
    inputs[input_count++] = g.Label(cont->true_block());
    inputs[input_count++] = g.Label(cont->false_block());
  }

  outputs[output_count++] = g.DefineAsRegister(node);
  if (cont->IsSet()) {
    outputs[output_count++] = g.DefineAsRegister(cont->result());
  }

  DCHECK_NE(0u, input_count);
  DCHECK_NE(0u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  Instruction* instr = selector->Emit(cont->Encode(opcode), output_count,
                                      outputs, input_count, inputs);
  if (cont->IsBranch()) instr->MarkAsControl();
}


// Shared routine for multiple binary operations.
template <typename Matcher>
static void VisitBinop(InstructionSelector* selector, Node* node,
                       ArchOpcode opcode, ImmediateMode operand_mode) {
  FlagsContinuation cont;
  VisitBinop<Matcher>(selector, node, opcode, operand_mode, &cont);
}


void InstructionSelector::VisitLoad(Node* node) {
  MachineType rep = RepresentationOf(OpParameter<LoadRepresentation>(node));
  MachineType typ = TypeOf(OpParameter<LoadRepresentation>(node));
  PPCOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* offset = node->InputAt(1);

  ArchOpcode opcode;
  ImmediateMode mode = kInt16Imm;
  switch (rep) {
    case kRepFloat32:
      opcode = kPPC_LoadFloat32;
      break;
    case kRepFloat64:
      opcode = kPPC_LoadFloat64;
      break;
    case kRepBit:  // Fall through.
    case kRepWord8:
      opcode = (typ == kTypeInt32) ? kPPC_LoadWordS8 : kPPC_LoadWordU8;
      break;
    case kRepWord16:
      opcode = (typ == kTypeInt32) ? kPPC_LoadWordS16 : kPPC_LoadWordU16;
      break;
#if !V8_TARGET_ARCH_PPC64
    case kRepTagged:  // Fall through.
#endif
    case kRepWord32:
      opcode = kPPC_LoadWordS32;
#if V8_TARGET_ARCH_PPC64
      // TODO(mbrandy): this applies to signed loads only (lwa)
      mode = kInt16Imm_4ByteAligned;
#endif
      break;
#if V8_TARGET_ARCH_PPC64
    case kRepTagged:  // Fall through.
    case kRepWord64:
      opcode = kPPC_LoadWord64;
      mode = kInt16Imm_4ByteAligned;
      break;
#endif
    default:
      UNREACHABLE();
      return;
  }
  if (g.CanBeImmediate(offset, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI),
         g.DefineAsRegister(node), g.UseRegister(base), g.UseImmediate(offset));
  } else if (g.CanBeImmediate(base, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI),
         g.DefineAsRegister(node), g.UseRegister(offset), g.UseImmediate(base));
  } else {
    Emit(opcode | AddressingModeField::encode(kMode_MRR),
         g.DefineAsRegister(node), g.UseRegister(base), g.UseRegister(offset));
  }
}


void InstructionSelector::VisitStore(Node* node) {
  PPCOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* offset = node->InputAt(1);
  Node* value = node->InputAt(2);

  StoreRepresentation store_rep = OpParameter<StoreRepresentation>(node);
  MachineType rep = RepresentationOf(store_rep.machine_type());
  if (store_rep.write_barrier_kind() == kFullWriteBarrier) {
    DCHECK(rep == kRepTagged);
    // TODO(dcarney): refactor RecordWrite function to take temp registers
    //                and pass them here instead of using fixed regs
    // TODO(dcarney): handle immediate indices.
    InstructionOperand temps[] = {g.TempRegister(r8), g.TempRegister(r9)};
    Emit(kPPC_StoreWriteBarrier, g.NoOutput(), g.UseFixed(base, r7),
         g.UseFixed(offset, r8), g.UseFixed(value, r9), arraysize(temps),
         temps);
    return;
  }
  DCHECK_EQ(kNoWriteBarrier, store_rep.write_barrier_kind());
  ArchOpcode opcode;
  ImmediateMode mode = kInt16Imm;
  switch (rep) {
    case kRepFloat32:
      opcode = kPPC_StoreFloat32;
      break;
    case kRepFloat64:
      opcode = kPPC_StoreFloat64;
      break;
    case kRepBit:  // Fall through.
    case kRepWord8:
      opcode = kPPC_StoreWord8;
      break;
    case kRepWord16:
      opcode = kPPC_StoreWord16;
      break;
#if !V8_TARGET_ARCH_PPC64
    case kRepTagged:  // Fall through.
#endif
    case kRepWord32:
      opcode = kPPC_StoreWord32;
      break;
#if V8_TARGET_ARCH_PPC64
    case kRepTagged:  // Fall through.
    case kRepWord64:
      opcode = kPPC_StoreWord64;
      mode = kInt16Imm_4ByteAligned;
      break;
#endif
    default:
      UNREACHABLE();
      return;
  }
  if (g.CanBeImmediate(offset, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
         g.UseRegister(base), g.UseImmediate(offset), g.UseRegister(value));
  } else if (g.CanBeImmediate(base, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
         g.UseRegister(offset), g.UseImmediate(base), g.UseRegister(value));
  } else {
    Emit(opcode | AddressingModeField::encode(kMode_MRR), g.NoOutput(),
         g.UseRegister(base), g.UseRegister(offset), g.UseRegister(value));
  }
}


void InstructionSelector::VisitCheckedLoad(Node* node) {
  MachineType rep = RepresentationOf(OpParameter<MachineType>(node));
  MachineType typ = TypeOf(OpParameter<MachineType>(node));
  PPCOperandGenerator g(this);
  Node* const base = node->InputAt(0);
  Node* const offset = node->InputAt(1);
  Node* const length = node->InputAt(2);
  ArchOpcode opcode;
  switch (rep) {
    case kRepWord8:
      opcode = typ == kTypeInt32 ? kCheckedLoadInt8 : kCheckedLoadUint8;
      break;
    case kRepWord16:
      opcode = typ == kTypeInt32 ? kCheckedLoadInt16 : kCheckedLoadUint16;
      break;
    case kRepWord32:
      opcode = kCheckedLoadWord32;
      break;
    case kRepFloat32:
      opcode = kCheckedLoadFloat32;
      break;
    case kRepFloat64:
      opcode = kCheckedLoadFloat64;
      break;
    default:
      UNREACHABLE();
      return;
  }
  AddressingMode addressingMode = kMode_MRR;
  Emit(opcode | AddressingModeField::encode(addressingMode),
       g.DefineAsRegister(node), g.UseRegister(base), g.UseRegister(offset),
       g.UseOperand(length, kInt16Imm_Unsigned));
}


void InstructionSelector::VisitCheckedStore(Node* node) {
  MachineType rep = RepresentationOf(OpParameter<MachineType>(node));
  PPCOperandGenerator g(this);
  Node* const base = node->InputAt(0);
  Node* const offset = node->InputAt(1);
  Node* const length = node->InputAt(2);
  Node* const value = node->InputAt(3);
  ArchOpcode opcode;
  switch (rep) {
    case kRepWord8:
      opcode = kCheckedStoreWord8;
      break;
    case kRepWord16:
      opcode = kCheckedStoreWord16;
      break;
    case kRepWord32:
      opcode = kCheckedStoreWord32;
      break;
    case kRepFloat32:
      opcode = kCheckedStoreFloat32;
      break;
    case kRepFloat64:
      opcode = kCheckedStoreFloat64;
      break;
    default:
      UNREACHABLE();
      return;
  }
  AddressingMode addressingMode = kMode_MRR;
  Emit(opcode | AddressingModeField::encode(addressingMode), g.NoOutput(),
       g.UseRegister(base), g.UseRegister(offset),
       g.UseOperand(length, kInt16Imm_Unsigned), g.UseRegister(value));
}


template <typename Matcher>
static void VisitLogical(InstructionSelector* selector, Node* node, Matcher* m,
                         ArchOpcode opcode, bool left_can_cover,
                         bool right_can_cover, ImmediateMode imm_mode) {
  PPCOperandGenerator g(selector);

  // Map instruction to equivalent operation with inverted right input.
  ArchOpcode inv_opcode = opcode;
  switch (opcode) {
    case kPPC_And32:
      inv_opcode = kPPC_AndComplement32;
      break;
    case kPPC_And64:
      inv_opcode = kPPC_AndComplement64;
      break;
    case kPPC_Or32:
      inv_opcode = kPPC_OrComplement32;
      break;
    case kPPC_Or64:
      inv_opcode = kPPC_OrComplement64;
      break;
    default:
      UNREACHABLE();
  }

  // Select Logical(y, ~x) for Logical(Xor(x, -1), y).
  if ((m->left().IsWord32Xor() || m->left().IsWord64Xor()) && left_can_cover) {
    Matcher mleft(m->left().node());
    if (mleft.right().Is(-1)) {
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(m->right().node()),
                     g.UseRegister(mleft.left().node()));
      return;
    }
  }

  // Select Logical(x, ~y) for Logical(x, Xor(y, -1)).
  if ((m->right().IsWord32Xor() || m->right().IsWord64Xor()) &&
      right_can_cover) {
    Matcher mright(m->right().node());
    if (mright.right().Is(-1)) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(m->left().node()),
                     g.UseRegister(mright.left().node()));
      return;
    }
  }

  VisitBinop<Matcher>(selector, node, opcode, imm_mode);
}


static inline bool IsContiguousMask32(uint32_t value, int* mb, int* me) {
  int mask_width = base::bits::CountPopulation32(value);
  int mask_msb = base::bits::CountLeadingZeros32(value);
  int mask_lsb = base::bits::CountTrailingZeros32(value);
  if ((mask_width == 0) || (mask_msb + mask_width + mask_lsb != 32))
    return false;
  *mb = mask_lsb + mask_width - 1;
  *me = mask_lsb;
  return true;
}


#if V8_TARGET_ARCH_PPC64
static inline bool IsContiguousMask64(uint64_t value, int* mb, int* me) {
  int mask_width = base::bits::CountPopulation64(value);
  int mask_msb = base::bits::CountLeadingZeros64(value);
  int mask_lsb = base::bits::CountTrailingZeros64(value);
  if ((mask_width == 0) || (mask_msb + mask_width + mask_lsb != 64))
    return false;
  *mb = mask_lsb + mask_width - 1;
  *me = mask_lsb;
  return true;
}
#endif


// TODO(mbrandy): Absorb rotate-right into rlwinm?
void InstructionSelector::VisitWord32And(Node* node) {
  PPCOperandGenerator g(this);
  Int32BinopMatcher m(node);
  int mb;
  int me;
  if (m.right().HasValue() && IsContiguousMask32(m.right().Value(), &mb, &me)) {
    int sh = 0;
    Node* left = m.left().node();
    if ((m.left().IsWord32Shr() || m.left().IsWord32Shl()) &&
        CanCover(node, left)) {
      // Try to absorb left/right shift into rlwinm
      Int32BinopMatcher mleft(m.left().node());
      if (mleft.right().IsInRange(0, 31)) {
        left = mleft.left().node();
        sh = mleft.right().Value();
        if (m.left().IsWord32Shr()) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (mb > 31 - sh) mb = 31 - sh;
          sh = (32 - sh) & 0x1f;
        } else {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (me < sh) me = sh;
        }
      }
    }
    if (mb >= me) {
      Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node), g.UseRegister(left),
           g.TempImmediate(sh), g.TempImmediate(mb), g.TempImmediate(me));
      return;
    }
  }
  VisitLogical<Int32BinopMatcher>(
      this, node, &m, kPPC_And32, CanCover(node, m.left().node()),
      CanCover(node, m.right().node()), kInt16Imm_Unsigned);
}


#if V8_TARGET_ARCH_PPC64
// TODO(mbrandy): Absorb rotate-right into rldic?
void InstructionSelector::VisitWord64And(Node* node) {
  PPCOperandGenerator g(this);
  Int64BinopMatcher m(node);
  int mb;
  int me;
  if (m.right().HasValue() && IsContiguousMask64(m.right().Value(), &mb, &me)) {
    int sh = 0;
    Node* left = m.left().node();
    if ((m.left().IsWord64Shr() || m.left().IsWord64Shl()) &&
        CanCover(node, left)) {
      // Try to absorb left/right shift into rldic
      Int64BinopMatcher mleft(m.left().node());
      if (mleft.right().IsInRange(0, 63)) {
        left = mleft.left().node();
        sh = mleft.right().Value();
        if (m.left().IsWord64Shr()) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (mb > 63 - sh) mb = 63 - sh;
          sh = (64 - sh) & 0x3f;
        } else {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (me < sh) me = sh;
        }
      }
    }
    if (mb >= me) {
      bool match = false;
      ArchOpcode opcode;
      int mask;
      if (me == 0) {
        match = true;
        opcode = kPPC_RotLeftAndClearLeft64;
        mask = mb;
      } else if (mb == 63) {
        match = true;
        opcode = kPPC_RotLeftAndClearRight64;
        mask = me;
      } else if (sh && me <= sh && m.left().IsWord64Shl()) {
        match = true;
        opcode = kPPC_RotLeftAndClear64;
        mask = mb;
      }
      if (match) {
        Emit(opcode, g.DefineAsRegister(node), g.UseRegister(left),
             g.TempImmediate(sh), g.TempImmediate(mask));
        return;
      }
    }
  }
  VisitLogical<Int64BinopMatcher>(
      this, node, &m, kPPC_And64, CanCover(node, m.left().node()),
      CanCover(node, m.right().node()), kInt16Imm_Unsigned);
}
#endif


void InstructionSelector::VisitWord32Or(Node* node) {
  Int32BinopMatcher m(node);
  VisitLogical<Int32BinopMatcher>(
      this, node, &m, kPPC_Or32, CanCover(node, m.left().node()),
      CanCover(node, m.right().node()), kInt16Imm_Unsigned);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Or(Node* node) {
  Int64BinopMatcher m(node);
  VisitLogical<Int64BinopMatcher>(
      this, node, &m, kPPC_Or64, CanCover(node, m.left().node()),
      CanCover(node, m.right().node()), kInt16Imm_Unsigned);
}
#endif


void InstructionSelector::VisitWord32Xor(Node* node) {
  PPCOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().Is(-1)) {
    Emit(kPPC_Not32, g.DefineAsRegister(node), g.UseRegister(m.left().node()));
  } else {
    VisitBinop<Int32BinopMatcher>(this, node, kPPC_Xor32, kInt16Imm_Unsigned);
  }
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Xor(Node* node) {
  PPCOperandGenerator g(this);
  Int64BinopMatcher m(node);
  if (m.right().Is(-1)) {
    Emit(kPPC_Not64, g.DefineAsRegister(node), g.UseRegister(m.left().node()));
  } else {
    VisitBinop<Int64BinopMatcher>(this, node, kPPC_Xor64, kInt16Imm_Unsigned);
  }
}
#endif


void InstructionSelector::VisitWord32Shl(Node* node) {
  PPCOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.left().IsWord32And() && m.right().IsInRange(0, 31)) {
    // Try to absorb logical-and into rlwinm
    Int32BinopMatcher mleft(m.left().node());
    int sh = m.right().Value();
    int mb;
    int me;
    if (mleft.right().HasValue() &&
        IsContiguousMask32(mleft.right().Value() << sh, &mb, &me)) {
      // Adjust the mask such that it doesn't include any rotated bits.
      if (me < sh) me = sh;
      if (mb >= me) {
        Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
             g.TempImmediate(mb), g.TempImmediate(me));
        return;
      }
    }
  }
  VisitRRO(this, node, kPPC_ShiftLeft32, kShift32Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Shl(Node* node) {
  PPCOperandGenerator g(this);
  Int64BinopMatcher m(node);
  // TODO(mbrandy): eliminate left sign extension if right >= 32
  if (m.left().IsWord64And() && m.right().IsInRange(0, 63)) {
    // Try to absorb logical-and into rldic
    Int64BinopMatcher mleft(m.left().node());
    int sh = m.right().Value();
    int mb;
    int me;
    if (mleft.right().HasValue() &&
        IsContiguousMask64(mleft.right().Value() << sh, &mb, &me)) {
      // Adjust the mask such that it doesn't include any rotated bits.
      if (me < sh) me = sh;
      if (mb >= me) {
        bool match = false;
        ArchOpcode opcode;
        int mask;
        if (me == 0) {
          match = true;
          opcode = kPPC_RotLeftAndClearLeft64;
          mask = mb;
        } else if (mb == 63) {
          match = true;
          opcode = kPPC_RotLeftAndClearRight64;
          mask = me;
        } else if (sh && me <= sh) {
          match = true;
          opcode = kPPC_RotLeftAndClear64;
          mask = mb;
        }
        if (match) {
          Emit(opcode, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
               g.TempImmediate(mask));
          return;
        }
      }
    }
  }
  VisitRRO(this, node, kPPC_ShiftLeft64, kShift64Imm);
}
#endif


void InstructionSelector::VisitWord32Shr(Node* node) {
  PPCOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.left().IsWord32And() && m.right().IsInRange(0, 31)) {
    // Try to absorb logical-and into rlwinm
    Int32BinopMatcher mleft(m.left().node());
    int sh = m.right().Value();
    int mb;
    int me;
    if (mleft.right().HasValue() &&
        IsContiguousMask32((uint32_t)(mleft.right().Value()) >> sh, &mb, &me)) {
      // Adjust the mask such that it doesn't include any rotated bits.
      if (mb > 31 - sh) mb = 31 - sh;
      sh = (32 - sh) & 0x1f;
      if (mb >= me) {
        Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
             g.TempImmediate(mb), g.TempImmediate(me));
        return;
      }
    }
  }
  VisitRRO(this, node, kPPC_ShiftRight32, kShift32Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Shr(Node* node) {
  PPCOperandGenerator g(this);
  Int64BinopMatcher m(node);
  if (m.left().IsWord64And() && m.right().IsInRange(0, 63)) {
    // Try to absorb logical-and into rldic
    Int64BinopMatcher mleft(m.left().node());
    int sh = m.right().Value();
    int mb;
    int me;
    if (mleft.right().HasValue() &&
        IsContiguousMask64((uint64_t)(mleft.right().Value()) >> sh, &mb, &me)) {
      // Adjust the mask such that it doesn't include any rotated bits.
      if (mb > 63 - sh) mb = 63 - sh;
      sh = (64 - sh) & 0x3f;
      if (mb >= me) {
        bool match = false;
        ArchOpcode opcode;
        int mask;
        if (me == 0) {
          match = true;
          opcode = kPPC_RotLeftAndClearLeft64;
          mask = mb;
        } else if (mb == 63) {
          match = true;
          opcode = kPPC_RotLeftAndClearRight64;
          mask = me;
        }
        if (match) {
          Emit(opcode, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
               g.TempImmediate(mask));
          return;
        }
      }
    }
  }
  VisitRRO(this, node, kPPC_ShiftRight64, kShift64Imm);
}
#endif


void InstructionSelector::VisitWord32Sar(Node* node) {
  PPCOperandGenerator g(this);
  Int32BinopMatcher m(node);
  // Replace with sign extension for (x << K) >> K where K is 16 or 24.
  if (CanCover(node, m.left().node()) && m.left().IsWord32Shl()) {
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.right().Is(16) && m.right().Is(16)) {
      Emit(kPPC_ExtendSignWord16, g.DefineAsRegister(node),
           g.UseRegister(mleft.left().node()));
      return;
    } else if (mleft.right().Is(24) && m.right().Is(24)) {
      Emit(kPPC_ExtendSignWord8, g.DefineAsRegister(node),
           g.UseRegister(mleft.left().node()));
      return;
    }
  }
  VisitRRO(this, node, kPPC_ShiftRightAlg32, kShift32Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Sar(Node* node) {
  VisitRRO(this, node, kPPC_ShiftRightAlg64, kShift64Imm);
}
#endif


// TODO(mbrandy): Absorb logical-and into rlwinm?
void InstructionSelector::VisitWord32Ror(Node* node) {
  VisitRRO(this, node, kPPC_RotRight32, kShift32Imm);
}


#if V8_TARGET_ARCH_PPC64
// TODO(mbrandy): Absorb logical-and into rldic?
void InstructionSelector::VisitWord64Ror(Node* node) {
  VisitRRO(this, node, kPPC_RotRight64, kShift64Imm);
}
#endif


void InstructionSelector::VisitInt32Add(Node* node) {
  VisitBinop<Int32BinopMatcher>(this, node, kPPC_Add32, kInt16Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Add(Node* node) {
  VisitBinop<Int64BinopMatcher>(this, node, kPPC_Add64, kInt16Imm);
}
#endif


void InstructionSelector::VisitInt32Sub(Node* node) {
  PPCOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.left().Is(0)) {
    Emit(kPPC_Neg32, g.DefineAsRegister(node), g.UseRegister(m.right().node()));
  } else {
    VisitBinop<Int32BinopMatcher>(this, node, kPPC_Sub32, kInt16Imm_Negate);
  }
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Sub(Node* node) {
  PPCOperandGenerator g(this);
  Int64BinopMatcher m(node);
  if (m.left().Is(0)) {
    Emit(kPPC_Neg64, g.DefineAsRegister(node), g.UseRegister(m.right().node()));
  } else {
    VisitBinop<Int64BinopMatcher>(this, node, kPPC_Sub64, kInt16Imm_Negate);
  }
}
#endif


void InstructionSelector::VisitInt32Mul(Node* node) {
  VisitRRR(this, node, kPPC_Mul32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Mul(Node* node) {
  VisitRRR(this, node, kPPC_Mul64);
}
#endif


void InstructionSelector::VisitInt32MulHigh(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_MulHigh32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)), g.UseRegister(node->InputAt(1)));
}


void InstructionSelector::VisitUint32MulHigh(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_MulHighU32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)), g.UseRegister(node->InputAt(1)));
}


void InstructionSelector::VisitInt32Div(Node* node) {
  VisitRRR(this, node, kPPC_Div32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Div(Node* node) {
  VisitRRR(this, node, kPPC_Div64);
}
#endif


void InstructionSelector::VisitUint32Div(Node* node) {
  VisitRRR(this, node, kPPC_DivU32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitUint64Div(Node* node) {
  VisitRRR(this, node, kPPC_DivU64);
}
#endif


void InstructionSelector::VisitInt32Mod(Node* node) {
  VisitRRR(this, node, kPPC_Mod32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Mod(Node* node) {
  VisitRRR(this, node, kPPC_Mod64);
}
#endif


void InstructionSelector::VisitUint32Mod(Node* node) {
  VisitRRR(this, node, kPPC_ModU32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitUint64Mod(Node* node) {
  VisitRRR(this, node, kPPC_ModU64);
}
#endif


void InstructionSelector::VisitChangeFloat32ToFloat64(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float32ToFloat64, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeInt32ToFloat64(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Int32ToFloat64, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeUint32ToFloat64(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Uint32ToFloat64, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToInt32(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float64ToInt32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToUint32(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float64ToUint32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitChangeInt32ToInt64(Node* node) {
  // TODO(mbrandy): inspect input to see if nop is appropriate.
  PPCOperandGenerator g(this);
  Emit(kPPC_ExtendSignWord32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeUint32ToUint64(Node* node) {
  // TODO(mbrandy): inspect input to see if nop is appropriate.
  PPCOperandGenerator g(this);
  Emit(kPPC_Uint32ToUint64, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}
#endif


void InstructionSelector::VisitTruncateFloat64ToFloat32(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float64ToFloat32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitTruncateInt64ToInt32(Node* node) {
  PPCOperandGenerator g(this);
  // TODO(mbrandy): inspect input to see if nop is appropriate.
  Emit(kPPC_Int64ToInt32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}
#endif


void InstructionSelector::VisitFloat64Add(Node* node) {
  // TODO(mbrandy): detect multiply-add
  VisitRRRFloat64(this, node, kPPC_AddFloat64);
}


void InstructionSelector::VisitFloat64Sub(Node* node) {
  // TODO(mbrandy): detect multiply-subtract
  PPCOperandGenerator g(this);
  Float64BinopMatcher m(node);
  if (m.left().IsMinusZero() && m.right().IsFloat64RoundDown() &&
      CanCover(m.node(), m.right().node())) {
    if (m.right().InputAt(0)->opcode() == IrOpcode::kFloat64Sub &&
        CanCover(m.right().node(), m.right().InputAt(0))) {
      Float64BinopMatcher mright0(m.right().InputAt(0));
      if (mright0.left().IsMinusZero()) {
        // -floor(-x) = ceil(x)
        Emit(kPPC_CeilFloat64, g.DefineAsRegister(node),
             g.UseRegister(mright0.right().node()));
        return;
      }
    }
  }
  VisitRRRFloat64(this, node, kPPC_SubFloat64);
}


void InstructionSelector::VisitFloat64Mul(Node* node) {
  // TODO(mbrandy): detect negate
  VisitRRRFloat64(this, node, kPPC_MulFloat64);
}


void InstructionSelector::VisitFloat64Div(Node* node) {
  VisitRRRFloat64(this, node, kPPC_DivFloat64);
}


void InstructionSelector::VisitFloat64Mod(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_ModFloat64, g.DefineAsFixed(node, d1),
       g.UseFixed(node->InputAt(0), d1),
       g.UseFixed(node->InputAt(1), d2))->MarkAsCall();
}


void InstructionSelector::VisitFloat64Max(Node* node) { UNREACHABLE(); }


void InstructionSelector::VisitFloat64Min(Node* node) { UNREACHABLE(); }


void InstructionSelector::VisitFloat64Sqrt(Node* node) {
  VisitRRFloat64(this, kPPC_SqrtFloat64, node);
}


void InstructionSelector::VisitFloat64RoundDown(Node* node) {
  VisitRRFloat64(this, kPPC_FloorFloat64, node);
}


void InstructionSelector::VisitFloat64RoundTruncate(Node* node) {
  VisitRRFloat64(this, kPPC_TruncateFloat64, node);
}


void InstructionSelector::VisitFloat64RoundTiesAway(Node* node) {
  VisitRRFloat64(this, kPPC_RoundFloat64, node);
}


void InstructionSelector::VisitInt32AddWithOverflow(Node* node) {
  if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
    FlagsContinuation cont(kOverflow, ovf);
    return VisitBinop<Int32BinopMatcher>(this, node, kPPC_AddWithOverflow32,
                                         kInt16Imm, &cont);
  }
  FlagsContinuation cont;
  VisitBinop<Int32BinopMatcher>(this, node, kPPC_AddWithOverflow32, kInt16Imm,
                                &cont);
}


void InstructionSelector::VisitInt32SubWithOverflow(Node* node) {
  if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
    FlagsContinuation cont(kOverflow, ovf);
    return VisitBinop<Int32BinopMatcher>(this, node, kPPC_SubWithOverflow32,
                                         kInt16Imm_Negate, &cont);
  }
  FlagsContinuation cont;
  VisitBinop<Int32BinopMatcher>(this, node, kPPC_SubWithOverflow32,
                                kInt16Imm_Negate, &cont);
}


static bool CompareLogical(FlagsContinuation* cont) {
  switch (cont->condition()) {
    case kUnsignedLessThan:
    case kUnsignedGreaterThanOrEqual:
    case kUnsignedLessThanOrEqual:
    case kUnsignedGreaterThan:
      return true;
    default:
      return false;
  }
  UNREACHABLE();
  return false;
}


// Shared routine for multiple compare operations.
static void VisitCompare(InstructionSelector* selector, InstructionCode opcode,
                         InstructionOperand left, InstructionOperand right,
                         FlagsContinuation* cont) {
  PPCOperandGenerator g(selector);
  opcode = cont->Encode(opcode);
  if (cont->IsBranch()) {
    selector->Emit(opcode, g.NoOutput(), left, right,
                   g.Label(cont->true_block()),
                   g.Label(cont->false_block()))->MarkAsControl();
  } else {
    DCHECK(cont->IsSet());
    selector->Emit(opcode, g.DefineAsRegister(cont->result()), left, right);
  }
}


// Shared routine for multiple word compare operations.
static void VisitWordCompare(InstructionSelector* selector, Node* node,
                             InstructionCode opcode, FlagsContinuation* cont,
                             bool commutative, ImmediateMode immediate_mode) {
  PPCOperandGenerator g(selector);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);

  // Match immediates on left or right side of comparison.
  if (g.CanBeImmediate(right, immediate_mode)) {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseImmediate(right),
                 cont);
  } else if (g.CanBeImmediate(left, immediate_mode)) {
    if (!commutative) cont->Commute();
    VisitCompare(selector, opcode, g.UseRegister(right), g.UseImmediate(left),
                 cont);
  } else {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseRegister(right),
                 cont);
  }
}


static void VisitWord32Compare(InstructionSelector* selector, Node* node,
                               FlagsContinuation* cont) {
  ImmediateMode mode = (CompareLogical(cont) ? kInt16Imm_Unsigned : kInt16Imm);
  VisitWordCompare(selector, node, kPPC_Cmp32, cont, false, mode);
}


#if V8_TARGET_ARCH_PPC64
static void VisitWord64Compare(InstructionSelector* selector, Node* node,
                               FlagsContinuation* cont) {
  ImmediateMode mode = (CompareLogical(cont) ? kInt16Imm_Unsigned : kInt16Imm);
  VisitWordCompare(selector, node, kPPC_Cmp64, cont, false, mode);
}
#endif


// Shared routine for multiple float compare operations.
static void VisitFloat64Compare(InstructionSelector* selector, Node* node,
                                FlagsContinuation* cont) {
  PPCOperandGenerator g(selector);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  VisitCompare(selector, kPPC_CmpFloat64, g.UseRegister(left),
               g.UseRegister(right), cont);
}


// Shared routine for word comparisons against zero.
static void VisitWordCompareZero(InstructionSelector* selector, Node* user,
                                 Node* value, InstructionCode opcode,
                                 FlagsContinuation* cont) {
  while (selector->CanCover(user, value)) {
    switch (value->opcode()) {
      case IrOpcode::kWord32Equal: {
        // Combine with comparisons against 0 by simply inverting the
        // continuation.
        Int32BinopMatcher m(value);
        if (m.right().Is(0)) {
          user = value;
          value = m.left().node();
          cont->Negate();
          continue;
        }
        cont->OverwriteAndNegateIfEqual(kEqual);
        return VisitWord32Compare(selector, value, cont);
      }
      case IrOpcode::kInt32LessThan:
        cont->OverwriteAndNegateIfEqual(kSignedLessThan);
        return VisitWord32Compare(selector, value, cont);
      case IrOpcode::kInt32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
        return VisitWord32Compare(selector, value, cont);
      case IrOpcode::kUint32LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitWord32Compare(selector, value, cont);
      case IrOpcode::kUint32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitWord32Compare(selector, value, cont);
#if V8_TARGET_ARCH_PPC64
      case IrOpcode::kWord64Equal:
        cont->OverwriteAndNegateIfEqual(kEqual);
        return VisitWord64Compare(selector, value, cont);
      case IrOpcode::kInt64LessThan:
        cont->OverwriteAndNegateIfEqual(kSignedLessThan);
        return VisitWord64Compare(selector, value, cont);
      case IrOpcode::kInt64LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
        return VisitWord64Compare(selector, value, cont);
      case IrOpcode::kUint64LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitWord64Compare(selector, value, cont);
#endif
      case IrOpcode::kFloat64Equal:
        cont->OverwriteAndNegateIfEqual(kEqual);
        return VisitFloat64Compare(selector, value, cont);
      case IrOpcode::kFloat64LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitFloat64Compare(selector, value, cont);
      case IrOpcode::kFloat64LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitFloat64Compare(selector, value, cont);
      case IrOpcode::kProjection:
        // Check if this is the overflow output projection of an
        // <Operation>WithOverflow node.
        if (ProjectionIndexOf(value->op()) == 1u) {
          // We cannot combine the <Operation>WithOverflow with this branch
          // unless the 0th projection (the use of the actual value of the
          // <Operation> is either NULL, which means there's no use of the
          // actual value, or was already defined, which means it is scheduled
          // *AFTER* this branch).
          Node* const node = value->InputAt(0);
          Node* const result = NodeProperties::FindProjection(node, 0);
          if (result == NULL || selector->IsDefined(result)) {
            switch (node->opcode()) {
              case IrOpcode::kInt32AddWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<Int32BinopMatcher>(
                    selector, node, kPPC_AddWithOverflow32, kInt16Imm, cont);
              case IrOpcode::kInt32SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<Int32BinopMatcher>(selector, node,
                                                     kPPC_SubWithOverflow32,
                                                     kInt16Imm_Negate, cont);
              default:
                break;
            }
          }
        }
        break;
      case IrOpcode::kInt32Sub:
        return VisitWord32Compare(selector, value, cont);
      case IrOpcode::kWord32And:
        // TODO(mbandy): opportunity for rlwinm?
        return VisitWordCompare(selector, value, kPPC_Tst32, cont, true,
                                kInt16Imm_Unsigned);
// TODO(mbrandy): Handle?
// case IrOpcode::kInt32Add:
// case IrOpcode::kWord32Or:
// case IrOpcode::kWord32Xor:
// case IrOpcode::kWord32Sar:
// case IrOpcode::kWord32Shl:
// case IrOpcode::kWord32Shr:
// case IrOpcode::kWord32Ror:
#if V8_TARGET_ARCH_PPC64
      case IrOpcode::kInt64Sub:
        return VisitWord64Compare(selector, value, cont);
      case IrOpcode::kWord64And:
        // TODO(mbandy): opportunity for rldic?
        return VisitWordCompare(selector, value, kPPC_Tst64, cont, true,
                                kInt16Imm_Unsigned);
// TODO(mbrandy): Handle?
// case IrOpcode::kInt64Add:
// case IrOpcode::kWord64Or:
// case IrOpcode::kWord64Xor:
// case IrOpcode::kWord64Sar:
// case IrOpcode::kWord64Shl:
// case IrOpcode::kWord64Shr:
// case IrOpcode::kWord64Ror:
#endif
      default:
        break;
    }
    break;
  }

  // Branch could not be combined with a compare, emit compare against 0.
  PPCOperandGenerator g(selector);
  VisitCompare(selector, opcode, g.UseRegister(value), g.TempImmediate(0),
               cont);
}


static void VisitWord32CompareZero(InstructionSelector* selector, Node* user,
                                   Node* value, FlagsContinuation* cont) {
  VisitWordCompareZero(selector, user, value, kPPC_Cmp32, cont);
}


#if V8_TARGET_ARCH_PPC64
static void VisitWord64CompareZero(InstructionSelector* selector, Node* user,
                                   Node* value, FlagsContinuation* cont) {
  VisitWordCompareZero(selector, user, value, kPPC_Cmp64, cont);
}
#endif


void InstructionSelector::VisitBranch(Node* branch, BasicBlock* tbranch,
                                      BasicBlock* fbranch) {
  FlagsContinuation cont(kNotEqual, tbranch, fbranch);
  VisitWord32CompareZero(this, branch, branch->InputAt(0), &cont);
}


void InstructionSelector::VisitSwitch(Node* node, BasicBlock* default_branch,
                                      BasicBlock** case_branches,
                                      int32_t* case_values, size_t case_count,
                                      int32_t min_value, int32_t max_value) {
  PPCOperandGenerator g(this);
  InstructionOperand value_operand = g.UseRegister(node->InputAt(0));
  InstructionOperand default_operand = g.Label(default_branch);

  // Note that {value_range} can be 0 if {min_value} is -2^31 and {max_value}
  // is 2^31-1, so don't assume that it's non-zero below.
  size_t value_range =
      1u + bit_cast<uint32_t>(max_value) - bit_cast<uint32_t>(min_value);

  // Determine whether to issue an ArchTableSwitch or an ArchLookupSwitch
  // instruction.
  size_t table_space_cost = 4 + value_range;
  size_t table_time_cost = 3;
  size_t lookup_space_cost = 3 + 2 * case_count;
  size_t lookup_time_cost = case_count;
  if (case_count > 0 &&
      table_space_cost + 3 * table_time_cost <=
          lookup_space_cost + 3 * lookup_time_cost &&
      min_value > std::numeric_limits<int32_t>::min()) {
    InstructionOperand index_operand = value_operand;
    if (min_value) {
      index_operand = g.TempRegister();
      Emit(kPPC_Sub32, index_operand, value_operand,
           g.TempImmediate(min_value));
    }
    size_t input_count = 2 + value_range;
    auto* inputs = zone()->NewArray<InstructionOperand>(input_count);
    inputs[0] = index_operand;
    std::fill(&inputs[1], &inputs[input_count], default_operand);
    for (size_t index = 0; index < case_count; ++index) {
      size_t value = case_values[index] - min_value;
      BasicBlock* branch = case_branches[index];
      DCHECK_LE(0u, value);
      DCHECK_LT(value + 2, input_count);
      inputs[value + 2] = g.Label(branch);
    }
    Emit(kArchTableSwitch, 0, nullptr, input_count, inputs, 0, nullptr)
        ->MarkAsControl();
    return;
  }

  // Generate a sequence of conditional jumps.
  size_t input_count = 2 + case_count * 2;
  auto* inputs = zone()->NewArray<InstructionOperand>(input_count);
  inputs[0] = value_operand;
  inputs[1] = default_operand;
  for (size_t index = 0; index < case_count; ++index) {
    int32_t value = case_values[index];
    BasicBlock* branch = case_branches[index];
    inputs[index * 2 + 2 + 0] = g.TempImmediate(value);
    inputs[index * 2 + 2 + 1] = g.Label(branch);
  }
  Emit(kArchLookupSwitch, 0, nullptr, input_count, inputs, 0, nullptr)
      ->MarkAsControl();
}


void InstructionSelector::VisitWord32Equal(Node* const node) {
  FlagsContinuation cont(kEqual, node);
  Int32BinopMatcher m(node);
  if (m.right().Is(0)) {
    return VisitWord32CompareZero(this, m.node(), m.left().node(), &cont);
  }
  VisitWord32Compare(this, node, &cont);
}


void InstructionSelector::VisitInt32LessThan(Node* node) {
  FlagsContinuation cont(kSignedLessThan, node);
  VisitWord32Compare(this, node, &cont);
}


void InstructionSelector::VisitInt32LessThanOrEqual(Node* node) {
  FlagsContinuation cont(kSignedLessThanOrEqual, node);
  VisitWord32Compare(this, node, &cont);
}


void InstructionSelector::VisitUint32LessThan(Node* node) {
  FlagsContinuation cont(kUnsignedLessThan, node);
  VisitWord32Compare(this, node, &cont);
}


void InstructionSelector::VisitUint32LessThanOrEqual(Node* node) {
  FlagsContinuation cont(kUnsignedLessThanOrEqual, node);
  VisitWord32Compare(this, node, &cont);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Equal(Node* const node) {
  FlagsContinuation cont(kEqual, node);
  Int64BinopMatcher m(node);
  if (m.right().Is(0)) {
    return VisitWord64CompareZero(this, m.node(), m.left().node(), &cont);
  }
  VisitWord64Compare(this, node, &cont);
}


void InstructionSelector::VisitInt64LessThan(Node* node) {
  FlagsContinuation cont(kSignedLessThan, node);
  VisitWord64Compare(this, node, &cont);
}


void InstructionSelector::VisitInt64LessThanOrEqual(Node* node) {
  FlagsContinuation cont(kSignedLessThanOrEqual, node);
  VisitWord64Compare(this, node, &cont);
}


void InstructionSelector::VisitUint64LessThan(Node* node) {
  FlagsContinuation cont(kUnsignedLessThan, node);
  VisitWord64Compare(this, node, &cont);
}
#endif


void InstructionSelector::VisitFloat64Equal(Node* node) {
  FlagsContinuation cont(kEqual, node);
  VisitFloat64Compare(this, node, &cont);
}


void InstructionSelector::VisitFloat64LessThan(Node* node) {
  FlagsContinuation cont(kUnsignedLessThan, node);
  VisitFloat64Compare(this, node, &cont);
}


void InstructionSelector::VisitFloat64LessThanOrEqual(Node* node) {
  FlagsContinuation cont(kUnsignedLessThanOrEqual, node);
  VisitFloat64Compare(this, node, &cont);
}


void InstructionSelector::VisitCall(Node* node, BasicBlock* handler) {
  PPCOperandGenerator g(this);
  const CallDescriptor* descriptor = OpParameter<CallDescriptor*>(node);

  FrameStateDescriptor* frame_state_descriptor = NULL;
  if (descriptor->NeedsFrameState()) {
    frame_state_descriptor =
        GetFrameStateDescriptor(node->InputAt(descriptor->InputCount()));
  }

  CallBuffer buffer(zone(), descriptor, frame_state_descriptor);

  // Compute InstructionOperands for inputs and outputs.
  // TODO(turbofan): on PPC it's probably better to use the code object in a
  // register if there are multiple uses of it. Improve constant pool and the
  // heuristics in the register allocator for where to emit constants.
  InitializeCallBuffer(node, &buffer, true, false);

  // Push any stack arguments.
  // TODO(mbrandy): reverse order and use push only for first
  for (auto i = buffer.pushed_nodes.rbegin(); i != buffer.pushed_nodes.rend();
       i++) {
    Emit(kPPC_Push, g.NoOutput(), g.UseRegister(*i));
  }

  // Pass label of exception handler block.
  CallDescriptor::Flags flags = descriptor->flags();
  if (handler != nullptr) {
    flags |= CallDescriptor::kHasExceptionHandler;
    buffer.instruction_args.push_back(g.Label(handler));
  }

  // Select the appropriate opcode based on the call type.
  InstructionCode opcode;
  switch (descriptor->kind()) {
    case CallDescriptor::kCallCodeObject: {
      opcode = kArchCallCodeObject;
      break;
    }
    case CallDescriptor::kCallJSFunction:
      opcode = kArchCallJSFunction;
      break;
    default:
      UNREACHABLE();
      return;
  }
  opcode |= MiscField::encode(flags);

  // Emit the call instruction.
  InstructionOperand* first_output =
      buffer.outputs.size() > 0 ? &buffer.outputs.front() : NULL;
  Instruction* call_instr =
      Emit(opcode, buffer.outputs.size(), first_output,
           buffer.instruction_args.size(), &buffer.instruction_args.front());
  call_instr->MarkAsCall();
}


void InstructionSelector::VisitFloat64ExtractLowWord32(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float64ExtractLowWord32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64ExtractHighWord32(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float64ExtractHighWord32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64InsertLowWord32(Node* node) {
  PPCOperandGenerator g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  if (left->opcode() == IrOpcode::kFloat64InsertHighWord32 &&
      CanCover(node, left)) {
    left = left->InputAt(1);
    Emit(kPPC_Float64Construct, g.DefineAsRegister(node), g.UseRegister(left),
         g.UseRegister(right));
    return;
  }
  Emit(kPPC_Float64InsertLowWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right));
}


void InstructionSelector::VisitFloat64InsertHighWord32(Node* node) {
  PPCOperandGenerator g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  if (left->opcode() == IrOpcode::kFloat64InsertLowWord32 &&
      CanCover(node, left)) {
    left = left->InputAt(1);
    Emit(kPPC_Float64Construct, g.DefineAsRegister(node), g.UseRegister(right),
         g.UseRegister(left));
    return;
  }
  Emit(kPPC_Float64InsertHighWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right));
}


// static
MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  return MachineOperatorBuilder::kFloat64RoundDown |
         MachineOperatorBuilder::kFloat64RoundTruncate |
         MachineOperatorBuilder::kFloat64RoundTiesAway;
  // We omit kWord32ShiftIsSafe as s[rl]w use 0x3f as a mask rather than 0x1f.
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
