// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bits.h"
#include "src/compiler/instruction-selector-impl.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE_UNIMPL() \
  PrintF("UNIMPLEMENTED instr_sel: %s at line %d\n", __FUNCTION__, __LINE__)

#define TRACE() PrintF("instr_sel: %s at line %d\n", __FUNCTION__, __LINE__)


// Adds Mips-specific methods for generating InstructionOperands.
class MipsOperandGenerator FINAL : public OperandGenerator {
 public:
  explicit MipsOperandGenerator(InstructionSelector* selector)
      : OperandGenerator(selector) {}

  InstructionOperand UseOperand(Node* node, InstructionCode opcode) {
    if (CanBeImmediate(node, opcode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  bool CanBeImmediate(Node* node, InstructionCode opcode) {
    Int32Matcher m(node);
    if (!m.HasValue()) return false;
    int32_t value = m.Value();
    switch (ArchOpcodeField::decode(opcode)) {
      case kMipsShl:
      case kMipsSar:
      case kMipsShr:
        return is_uint5(value);
      case kMipsXor:
        return is_uint16(value);
      case kMipsLdc1:
      case kMipsSdc1:
      case kCheckedLoadFloat32:
      case kCheckedLoadFloat64:
      case kCheckedStoreFloat32:
      case kCheckedStoreFloat64:
        return is_int16(value + kIntSize);
      default:
        return is_int16(value);
    }
  }

 private:
  bool ImmediateFitsAddrMode1Instruction(int32_t imm) const {
    TRACE_UNIMPL();
    return false;
  }
};


static void VisitRRR(InstructionSelector* selector, ArchOpcode opcode,
                     Node* node) {
  MipsOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseRegister(node->InputAt(1)));
}


static void VisitRR(InstructionSelector* selector, ArchOpcode opcode,
                    Node* node) {
  MipsOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)));
}


static void VisitRRO(InstructionSelector* selector, ArchOpcode opcode,
                     Node* node) {
  MipsOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseOperand(node->InputAt(1), opcode));
}


static void VisitBinop(InstructionSelector* selector, Node* node,
                       InstructionCode opcode, FlagsContinuation* cont) {
  MipsOperandGenerator g(selector);
  Int32BinopMatcher m(node);
  InstructionOperand inputs[4];
  size_t input_count = 0;
  InstructionOperand outputs[2];
  size_t output_count = 0;

  inputs[input_count++] = g.UseRegister(m.left().node());
  inputs[input_count++] = g.UseOperand(m.right().node(), opcode);

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


static void VisitBinop(InstructionSelector* selector, Node* node,
                       InstructionCode opcode) {
  FlagsContinuation cont;
  VisitBinop(selector, node, opcode, &cont);
}


void InstructionSelector::VisitLoad(Node* node) {
  MachineType rep = RepresentationOf(OpParameter<LoadRepresentation>(node));
  MachineType typ = TypeOf(OpParameter<LoadRepresentation>(node));
  MipsOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);

  ArchOpcode opcode;
  switch (rep) {
    case kRepFloat32:
      opcode = kMipsLwc1;
      break;
    case kRepFloat64:
      opcode = kMipsLdc1;
      break;
    case kRepBit:  // Fall through.
    case kRepWord8:
      opcode = typ == kTypeUint32 ? kMipsLbu : kMipsLb;
      break;
    case kRepWord16:
      opcode = typ == kTypeUint32 ? kMipsLhu : kMipsLh;
      break;
    case kRepTagged:  // Fall through.
    case kRepWord32:
      opcode = kMipsLw;
      break;
    default:
      UNREACHABLE();
      return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI),
         g.DefineAsRegister(node), g.UseRegister(base), g.UseImmediate(index));
  } else {
    InstructionOperand addr_reg = g.TempRegister();
    Emit(kMipsAdd | AddressingModeField::encode(kMode_None), addr_reg,
         g.UseRegister(index), g.UseRegister(base));
    // Emit desired load opcode, using temp addr_reg.
    Emit(opcode | AddressingModeField::encode(kMode_MRI),
         g.DefineAsRegister(node), addr_reg, g.TempImmediate(0));
  }
}


void InstructionSelector::VisitStore(Node* node) {
  MipsOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);

  StoreRepresentation store_rep = OpParameter<StoreRepresentation>(node);
  MachineType rep = RepresentationOf(store_rep.machine_type());
  if (store_rep.write_barrier_kind() == kFullWriteBarrier) {
    DCHECK(rep == kRepTagged);
    // TODO(dcarney): refactor RecordWrite function to take temp registers
    //                and pass them here instead of using fixed regs
    // TODO(dcarney): handle immediate indices.
    InstructionOperand temps[] = {g.TempRegister(t1), g.TempRegister(t2)};
    Emit(kMipsStoreWriteBarrier, g.NoOutput(), g.UseFixed(base, t0),
         g.UseFixed(index, t1), g.UseFixed(value, t2), arraysize(temps), temps);
    return;
  }
  DCHECK_EQ(kNoWriteBarrier, store_rep.write_barrier_kind());

  ArchOpcode opcode;
  switch (rep) {
    case kRepFloat32:
      opcode = kMipsSwc1;
      break;
    case kRepFloat64:
      opcode = kMipsSdc1;
      break;
    case kRepBit:  // Fall through.
    case kRepWord8:
      opcode = kMipsSb;
      break;
    case kRepWord16:
      opcode = kMipsSh;
      break;
    case kRepTagged:  // Fall through.
    case kRepWord32:
      opcode = kMipsSw;
      break;
    default:
      UNREACHABLE();
      return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
         g.UseRegister(base), g.UseImmediate(index), g.UseRegister(value));
  } else {
    InstructionOperand addr_reg = g.TempRegister();
    Emit(kMipsAdd | AddressingModeField::encode(kMode_None), addr_reg,
         g.UseRegister(index), g.UseRegister(base));
    // Emit desired store opcode, using temp addr_reg.
    Emit(opcode | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
         addr_reg, g.TempImmediate(0), g.UseRegister(value));
  }
}


void InstructionSelector::VisitWord32And(Node* node) {
  VisitBinop(this, node, kMipsAnd);
}


void InstructionSelector::VisitWord32Or(Node* node) {
  VisitBinop(this, node, kMipsOr);
}


void InstructionSelector::VisitWord32Xor(Node* node) {
  VisitBinop(this, node, kMipsXor);
}


void InstructionSelector::VisitWord32Shl(Node* node) {
  VisitRRO(this, kMipsShl, node);
}


void InstructionSelector::VisitWord32Shr(Node* node) {
  VisitRRO(this, kMipsShr, node);
}


void InstructionSelector::VisitWord32Sar(Node* node) {
  VisitRRO(this, kMipsSar, node);
}


void InstructionSelector::VisitWord32Ror(Node* node) {
  VisitRRO(this, kMipsRor, node);
}


void InstructionSelector::VisitInt32Add(Node* node) {
  MipsOperandGenerator g(this);

  // TODO(plind): Consider multiply & add optimization from arm port.
  VisitBinop(this, node, kMipsAdd);
}


void InstructionSelector::VisitInt32Sub(Node* node) {
  VisitBinop(this, node, kMipsSub);
}


void InstructionSelector::VisitInt32Mul(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().HasValue() && m.right().Value() > 0) {
    int32_t value = m.right().Value();
    if (base::bits::IsPowerOfTwo32(value)) {
      Emit(kMipsShl | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value)));
      return;
    }
    if (base::bits::IsPowerOfTwo32(value - 1)) {
      InstructionOperand temp = g.TempRegister();
      Emit(kMipsShl | AddressingModeField::encode(kMode_None), temp,
           g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value - 1)));
      Emit(kMipsAdd | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()), temp);
      return;
    }
    if (base::bits::IsPowerOfTwo32(value + 1)) {
      InstructionOperand temp = g.TempRegister();
      Emit(kMipsShl | AddressingModeField::encode(kMode_None), temp,
           g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value + 1)));
      Emit(kMipsSub | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), temp, g.UseRegister(m.left().node()));
      return;
    }
  }
  Emit(kMipsMul, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitInt32MulHigh(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsMulHigh, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)),
       g.UseRegister(node->InputAt(1)));
}


void InstructionSelector::VisitUint32MulHigh(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsMulHighU, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)),
       g.UseRegister(node->InputAt(1)));
}


void InstructionSelector::VisitInt32Div(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsDiv, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitUint32Div(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsDivU, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitInt32Mod(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsMod, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitUint32Mod(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsModU, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitChangeFloat32ToFloat64(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtDS, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeInt32ToFloat64(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtDW, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeUint32ToFloat64(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtDUw, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToInt32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsTruncWD, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToUint32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsTruncUwD, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitTruncateFloat64ToFloat32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtSD, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64Add(Node* node) {
  VisitRRR(this, kMipsAddD, node);
}


void InstructionSelector::VisitFloat64Sub(Node* node) {
  MipsOperandGenerator g(this);
  Float64BinopMatcher m(node);
  if (m.left().IsMinusZero() && m.right().IsFloat64RoundDown() &&
      CanCover(m.node(), m.right().node())) {
    if (m.right().InputAt(0)->opcode() == IrOpcode::kFloat64Sub &&
        CanCover(m.right().node(), m.right().InputAt(0))) {
      Float64BinopMatcher mright0(m.right().InputAt(0));
      if (mright0.left().IsMinusZero()) {
        Emit(kMipsFloat64RoundUp, g.DefineAsRegister(node),
             g.UseRegister(mright0.right().node()));
        return;
      }
    }
  }
  VisitRRR(this, kMipsSubD, node);
}


void InstructionSelector::VisitFloat64Mul(Node* node) {
  VisitRRR(this, kMipsMulD, node);
}


void InstructionSelector::VisitFloat64Div(Node* node) {
  VisitRRR(this, kMipsDivD, node);
}


void InstructionSelector::VisitFloat64Mod(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsModD, g.DefineAsFixed(node, f0), g.UseFixed(node->InputAt(0), f12),
       g.UseFixed(node->InputAt(1), f14))->MarkAsCall();
}


void InstructionSelector::VisitFloat64Max(Node* node) { UNREACHABLE(); }


void InstructionSelector::VisitFloat64Min(Node* node) { UNREACHABLE(); }


void InstructionSelector::VisitFloat64Sqrt(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsSqrtD, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64RoundDown(Node* node) {
  VisitRR(this, kMipsFloat64RoundDown, node);
}


void InstructionSelector::VisitFloat64RoundTruncate(Node* node) {
  VisitRR(this, kMipsFloat64RoundTruncate, node);
}


void InstructionSelector::VisitFloat64RoundTiesAway(Node* node) {
  UNREACHABLE();
}


void InstructionSelector::VisitCall(Node* node, BasicBlock* handler) {
  MipsOperandGenerator g(this);
  const CallDescriptor* descriptor = OpParameter<const CallDescriptor*>(node);

  FrameStateDescriptor* frame_state_descriptor = NULL;
  if (descriptor->NeedsFrameState()) {
    frame_state_descriptor =
        GetFrameStateDescriptor(node->InputAt(descriptor->InputCount()));
  }

  CallBuffer buffer(zone(), descriptor, frame_state_descriptor);

  // Compute InstructionOperands for inputs and outputs.
  InitializeCallBuffer(node, &buffer, true, false);
  // Possibly align stack here for functions.
  int push_count = buffer.pushed_nodes.size();
  if (push_count > 0) {
    Emit(kMipsStackClaim | MiscField::encode(push_count), g.NoOutput());
  }
  int slot = buffer.pushed_nodes.size() - 1;
  for (auto i = buffer.pushed_nodes.rbegin(); i != buffer.pushed_nodes.rend();
       ++i) {
    Emit(kMipsStoreToStackSlot | MiscField::encode(slot), g.NoOutput(),
         g.UseRegister(*i));
    slot--;
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


void InstructionSelector::VisitCheckedLoad(Node* node) {
  MachineType rep = RepresentationOf(OpParameter<MachineType>(node));
  MachineType typ = TypeOf(OpParameter<MachineType>(node));
  MipsOperandGenerator g(this);
  Node* const buffer = node->InputAt(0);
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
  InstructionOperand offset_operand = g.CanBeImmediate(offset, opcode)
                                          ? g.UseImmediate(offset)
                                          : g.UseRegister(offset);

  InstructionOperand length_operand = (!g.CanBeImmediate(offset, opcode))
                                          ? g.CanBeImmediate(length, opcode)
                                                ? g.UseImmediate(length)
                                                : g.UseRegister(length)
                                          : g.UseRegister(length);

  Emit(opcode | AddressingModeField::encode(kMode_MRI),
       g.DefineAsRegister(node), offset_operand, length_operand,
       g.UseRegister(buffer));
}


void InstructionSelector::VisitCheckedStore(Node* node) {
  MachineType rep = RepresentationOf(OpParameter<MachineType>(node));
  MipsOperandGenerator g(this);
  Node* const buffer = node->InputAt(0);
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
  InstructionOperand offset_operand = g.CanBeImmediate(offset, opcode)
                                          ? g.UseImmediate(offset)
                                          : g.UseRegister(offset);

  InstructionOperand length_operand = (!g.CanBeImmediate(offset, opcode))
                                          ? g.CanBeImmediate(length, opcode)
                                                ? g.UseImmediate(length)
                                                : g.UseRegister(length)
                                          : g.UseRegister(length);

  Emit(opcode | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
       offset_operand, length_operand, g.UseRegister(value),
       g.UseRegister(buffer));
}


namespace {

// Shared routine for multiple compare operations.
static void VisitCompare(InstructionSelector* selector, InstructionCode opcode,
                         InstructionOperand left, InstructionOperand right,
                         FlagsContinuation* cont) {
  MipsOperandGenerator g(selector);
  opcode = cont->Encode(opcode);
  if (cont->IsBranch()) {
    selector->Emit(opcode, g.NoOutput(), left, right,
                   g.Label(cont->true_block()),
                   g.Label(cont->false_block()))->MarkAsControl();
  } else {
    DCHECK(cont->IsSet());
    // TODO(plind): Revisit and test this path.
    selector->Emit(opcode, g.DefineAsRegister(cont->result()), left, right);
  }
}


// Shared routine for multiple float compare operations.
void VisitFloat64Compare(InstructionSelector* selector, Node* node,
                         FlagsContinuation* cont) {
  MipsOperandGenerator g(selector);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  VisitCompare(selector, kMipsCmpD, g.UseRegister(left), g.UseRegister(right),
               cont);
}


// Shared routine for multiple word compare operations.
void VisitWordCompare(InstructionSelector* selector, Node* node,
                      InstructionCode opcode, FlagsContinuation* cont,
                      bool commutative) {
  MipsOperandGenerator g(selector);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);

  // Match immediates on left or right side of comparison.
  if (g.CanBeImmediate(right, opcode)) {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseImmediate(right),
                 cont);
  } else if (g.CanBeImmediate(left, opcode)) {
    if (!commutative) cont->Commute();
    VisitCompare(selector, opcode, g.UseRegister(right), g.UseImmediate(left),
                 cont);
  } else {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseRegister(right),
                 cont);
  }
}


void VisitWordCompare(InstructionSelector* selector, Node* node,
                      FlagsContinuation* cont) {
  VisitWordCompare(selector, node, kMipsCmp, cont, false);
}

}  // namespace


// Shared routine for word comparisons against zero.
void VisitWordCompareZero(InstructionSelector* selector, Node* user,
                          Node* value, FlagsContinuation* cont) {
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
        return VisitWordCompare(selector, value, cont);
      }
      case IrOpcode::kInt32LessThan:
        cont->OverwriteAndNegateIfEqual(kSignedLessThan);
        return VisitWordCompare(selector, value, cont);
      case IrOpcode::kInt32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
        return VisitWordCompare(selector, value, cont);
      case IrOpcode::kUint32LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitWordCompare(selector, value, cont);
      case IrOpcode::kUint32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitWordCompare(selector, value, cont);
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
          if (!result || selector->IsDefined(result)) {
            switch (node->opcode()) {
              case IrOpcode::kInt32AddWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(selector, node, kMipsAddOvf, cont);
              case IrOpcode::kInt32SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(selector, node, kMipsSubOvf, cont);
              default:
                break;
            }
          }
        }
        break;
      case IrOpcode::kWord32And:
        return VisitWordCompare(selector, value, kMipsTst, cont, true);
      default:
        break;
    }
    break;
  }

  // Continuation could not be combined with a compare, emit compare against 0.
  MipsOperandGenerator g(selector);
  InstructionCode const opcode = cont->Encode(kMipsCmp);
  InstructionOperand const value_operand = g.UseRegister(value);
  if (cont->IsBranch()) {
    selector->Emit(opcode, g.NoOutput(), value_operand, g.TempImmediate(0),
                   g.Label(cont->true_block()),
                   g.Label(cont->false_block()))->MarkAsControl();
  } else {
    selector->Emit(opcode, g.DefineAsRegister(cont->result()), value_operand,
                   g.TempImmediate(0));
  }
}


void InstructionSelector::VisitBranch(Node* branch, BasicBlock* tbranch,
                                      BasicBlock* fbranch) {
  FlagsContinuation cont(kNotEqual, tbranch, fbranch);
  VisitWordCompareZero(this, branch, branch->InputAt(0), &cont);
}


void InstructionSelector::VisitSwitch(Node* node, BasicBlock* default_branch,
                                      BasicBlock** case_branches,
                                      int32_t* case_values, size_t case_count,
                                      int32_t min_value, int32_t max_value) {
  MipsOperandGenerator g(this);
  InstructionOperand value_operand = g.UseRegister(node->InputAt(0));
  InstructionOperand default_operand = g.Label(default_branch);

  // Note that {value_range} can be 0 if {min_value} is -2^31 and {max_value}
  // is 2^31-1, so don't assume that it's non-zero below.
  size_t value_range =
      1u + bit_cast<uint32_t>(max_value) - bit_cast<uint32_t>(min_value);

  // Determine whether to issue an ArchTableSwitch or an ArchLookupSwitch
  // instruction.
  size_t table_space_cost = 9 + value_range;
  size_t table_time_cost = 9;
  size_t lookup_space_cost = 2 + 2 * case_count;
  size_t lookup_time_cost = case_count;
  if (case_count > 0 &&
      table_space_cost + 3 * table_time_cost <=
          lookup_space_cost + 3 * lookup_time_cost &&
      min_value > std::numeric_limits<int32_t>::min()) {
    InstructionOperand index_operand = value_operand;
    if (min_value) {
      index_operand = g.TempRegister();
      Emit(kMipsSub, index_operand, value_operand, g.TempImmediate(min_value));
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
    return VisitWordCompareZero(this, m.node(), m.left().node(), &cont);
  }
  VisitWordCompare(this, node, &cont);
}


void InstructionSelector::VisitInt32LessThan(Node* node) {
  FlagsContinuation cont(kSignedLessThan, node);
  VisitWordCompare(this, node, &cont);
}


void InstructionSelector::VisitInt32LessThanOrEqual(Node* node) {
  FlagsContinuation cont(kSignedLessThanOrEqual, node);
  VisitWordCompare(this, node, &cont);
}


void InstructionSelector::VisitUint32LessThan(Node* node) {
  FlagsContinuation cont(kUnsignedLessThan, node);
  VisitWordCompare(this, node, &cont);
}


void InstructionSelector::VisitUint32LessThanOrEqual(Node* node) {
  FlagsContinuation cont(kUnsignedLessThanOrEqual, node);
  VisitWordCompare(this, node, &cont);
}


void InstructionSelector::VisitInt32AddWithOverflow(Node* node) {
  if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
    FlagsContinuation cont(kOverflow, ovf);
    return VisitBinop(this, node, kMipsAddOvf, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kMipsAddOvf, &cont);
}


void InstructionSelector::VisitInt32SubWithOverflow(Node* node) {
  if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
    FlagsContinuation cont(kOverflow, ovf);
    return VisitBinop(this, node, kMipsSubOvf, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kMipsSubOvf, &cont);
}


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


void InstructionSelector::VisitFloat64ExtractLowWord32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsFloat64ExtractLowWord32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64ExtractHighWord32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsFloat64ExtractHighWord32, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64InsertLowWord32(Node* node) {
  MipsOperandGenerator g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  Emit(kMipsFloat64InsertLowWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right));
}


void InstructionSelector::VisitFloat64InsertHighWord32(Node* node) {
  MipsOperandGenerator g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  Emit(kMipsFloat64InsertHighWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right));
}


// static
MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  if (IsMipsArchVariant(kMips32r2) || IsMipsArchVariant(kMips32r6)) {
    return MachineOperatorBuilder::kFloat64RoundDown |
           MachineOperatorBuilder::kFloat64RoundTruncate;
  }
  return MachineOperatorBuilder::kNoFlags;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
