// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/code-generator.h"

#include "src/compiler/code-generator-impl.h"
#include "src/compiler/gap-resolver.h"
#include "src/compiler/node-matchers.h"
#include "src/ia32/assembler-ia32.h"
#include "src/ia32/macro-assembler-ia32.h"
#include "src/scopes.h"

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->


// Adds IA-32 specific methods for decoding operands.
class IA32OperandConverter : public InstructionOperandConverter {
 public:
  IA32OperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  Operand InputOperand(size_t index, int extra = 0) {
    return ToOperand(instr_->InputAt(index), extra);
  }

  Immediate InputImmediate(size_t index) {
    return ToImmediate(instr_->InputAt(index));
  }

  Operand OutputOperand() { return ToOperand(instr_->Output()); }

  Operand ToOperand(InstructionOperand* op, int extra = 0) {
    if (op->IsRegister()) {
      DCHECK(extra == 0);
      return Operand(ToRegister(op));
    } else if (op->IsDoubleRegister()) {
      DCHECK(extra == 0);
      return Operand(ToDoubleRegister(op));
    }
    DCHECK(op->IsStackSlot() || op->IsDoubleStackSlot());
    // The linkage computes where all spill slots are located.
    FrameOffset offset = linkage()->GetFrameOffset(op->index(), frame(), extra);
    return Operand(offset.from_stack_pointer() ? esp : ebp, offset.offset());
  }

  Operand HighOperand(InstructionOperand* op) {
    DCHECK(op->IsDoubleStackSlot());
    return ToOperand(op, kPointerSize);
  }

  Immediate ToImmediate(InstructionOperand* operand) {
    Constant constant = ToConstant(operand);
    switch (constant.type()) {
      case Constant::kInt32:
        return Immediate(constant.ToInt32());
      case Constant::kFloat32:
        return Immediate(
            isolate()->factory()->NewNumber(constant.ToFloat32(), TENURED));
      case Constant::kFloat64:
        return Immediate(
            isolate()->factory()->NewNumber(constant.ToFloat64(), TENURED));
      case Constant::kExternalReference:
        return Immediate(constant.ToExternalReference());
      case Constant::kHeapObject:
        return Immediate(constant.ToHeapObject());
      case Constant::kInt64:
        break;
      case Constant::kRpoNumber:
        return Immediate::CodeRelativeOffset(ToLabel(operand));
    }
    UNREACHABLE();
    return Immediate(-1);
  }

  static size_t NextOffset(size_t* offset) {
    size_t i = *offset;
    (*offset)++;
    return i;
  }

  static ScaleFactor ScaleFor(AddressingMode one, AddressingMode mode) {
    STATIC_ASSERT(0 == static_cast<int>(times_1));
    STATIC_ASSERT(1 == static_cast<int>(times_2));
    STATIC_ASSERT(2 == static_cast<int>(times_4));
    STATIC_ASSERT(3 == static_cast<int>(times_8));
    int scale = static_cast<int>(mode - one);
    DCHECK(scale >= 0 && scale < 4);
    return static_cast<ScaleFactor>(scale);
  }

  Operand MemoryOperand(size_t* offset) {
    AddressingMode mode = AddressingModeField::decode(instr_->opcode());
    switch (mode) {
      case kMode_MR: {
        Register base = InputRegister(NextOffset(offset));
        int32_t disp = 0;
        return Operand(base, disp);
      }
      case kMode_MRI: {
        Register base = InputRegister(NextOffset(offset));
        int32_t disp = InputInt32(NextOffset(offset));
        return Operand(base, disp);
      }
      case kMode_MR1:
      case kMode_MR2:
      case kMode_MR4:
      case kMode_MR8: {
        Register base = InputRegister(NextOffset(offset));
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_MR1, mode);
        int32_t disp = 0;
        return Operand(base, index, scale, disp);
      }
      case kMode_MR1I:
      case kMode_MR2I:
      case kMode_MR4I:
      case kMode_MR8I: {
        Register base = InputRegister(NextOffset(offset));
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_MR1I, mode);
        int32_t disp = InputInt32(NextOffset(offset));
        return Operand(base, index, scale, disp);
      }
      case kMode_M1:
      case kMode_M2:
      case kMode_M4:
      case kMode_M8: {
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_M1, mode);
        int32_t disp = 0;
        return Operand(index, scale, disp);
      }
      case kMode_M1I:
      case kMode_M2I:
      case kMode_M4I:
      case kMode_M8I: {
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_M1I, mode);
        int32_t disp = InputInt32(NextOffset(offset));
        return Operand(index, scale, disp);
      }
      case kMode_MI: {
        int32_t disp = InputInt32(NextOffset(offset));
        return Operand(Immediate(disp));
      }
      case kMode_None:
        UNREACHABLE();
        return Operand(no_reg, 0);
    }
    UNREACHABLE();
    return Operand(no_reg, 0);
  }

  Operand MemoryOperand(size_t first_input = 0) {
    return MemoryOperand(&first_input);
  }
};


namespace {

bool HasImmediateInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsImmediate();
}


class OutOfLineLoadInteger FINAL : public OutOfLineCode {
 public:
  OutOfLineLoadInteger(CodeGenerator* gen, Register result)
      : OutOfLineCode(gen), result_(result) {}

  void Generate() FINAL { __ xor_(result_, result_); }

 private:
  Register const result_;
};


class OutOfLineLoadFloat FINAL : public OutOfLineCode {
 public:
  OutOfLineLoadFloat(CodeGenerator* gen, XMMRegister result)
      : OutOfLineCode(gen), result_(result) {}

  void Generate() FINAL { __ pcmpeqd(result_, result_); }

 private:
  XMMRegister const result_;
};


class OutOfLineTruncateDoubleToI FINAL : public OutOfLineCode {
 public:
  OutOfLineTruncateDoubleToI(CodeGenerator* gen, Register result,
                             XMMRegister input)
      : OutOfLineCode(gen), result_(result), input_(input) {}

  void Generate() FINAL {
    __ sub(esp, Immediate(kDoubleSize));
    __ movsd(MemOperand(esp, 0), input_);
    __ SlowTruncateToI(result_, esp, 0);
    __ add(esp, Immediate(kDoubleSize));
  }

 private:
  Register const result_;
  XMMRegister const input_;
};

}  // namespace


#define ASSEMBLE_CHECKED_LOAD_FLOAT(asm_instr)                          \
  do {                                                                  \
    auto result = i.OutputDoubleRegister();                             \
    auto offset = i.InputRegister(0);                                   \
    if (instr->InputAt(1)->IsRegister()) {                              \
      __ cmp(offset, i.InputRegister(1));                               \
    } else {                                                            \
      __ cmp(offset, i.InputImmediate(1));                              \
    }                                                                   \
    OutOfLineCode* ool = new (zone()) OutOfLineLoadFloat(this, result); \
    __ j(above_equal, ool->entry());                                    \
    __ asm_instr(result, i.MemoryOperand(2));                           \
    __ bind(ool->exit());                                               \
  } while (false)


#define ASSEMBLE_CHECKED_LOAD_INTEGER(asm_instr)                          \
  do {                                                                    \
    auto result = i.OutputRegister();                                     \
    auto offset = i.InputRegister(0);                                     \
    if (instr->InputAt(1)->IsRegister()) {                                \
      __ cmp(offset, i.InputRegister(1));                                 \
    } else {                                                              \
      __ cmp(offset, i.InputImmediate(1));                                \
    }                                                                     \
    OutOfLineCode* ool = new (zone()) OutOfLineLoadInteger(this, result); \
    __ j(above_equal, ool->entry());                                      \
    __ asm_instr(result, i.MemoryOperand(2));                             \
    __ bind(ool->exit());                                                 \
  } while (false)


#define ASSEMBLE_CHECKED_STORE_FLOAT(asm_instr)                 \
  do {                                                          \
    auto offset = i.InputRegister(0);                           \
    if (instr->InputAt(1)->IsRegister()) {                      \
      __ cmp(offset, i.InputRegister(1));                       \
    } else {                                                    \
      __ cmp(offset, i.InputImmediate(1));                      \
    }                                                           \
    Label done;                                                 \
    __ j(above_equal, &done, Label::kNear);                     \
    __ asm_instr(i.MemoryOperand(3), i.InputDoubleRegister(2)); \
    __ bind(&done);                                             \
  } while (false)


#define ASSEMBLE_CHECKED_STORE_INTEGER(asm_instr)            \
  do {                                                       \
    auto offset = i.InputRegister(0);                        \
    if (instr->InputAt(1)->IsRegister()) {                   \
      __ cmp(offset, i.InputRegister(1));                    \
    } else {                                                 \
      __ cmp(offset, i.InputImmediate(1));                   \
    }                                                        \
    Label done;                                              \
    __ j(above_equal, &done, Label::kNear);                  \
    if (instr->InputAt(2)->IsRegister()) {                   \
      __ asm_instr(i.MemoryOperand(3), i.InputRegister(2));  \
    } else {                                                 \
      __ asm_instr(i.MemoryOperand(3), i.InputImmediate(2)); \
    }                                                        \
    __ bind(&done);                                          \
  } while (false)


// Assembles an instruction after register allocation, producing machine code.
void CodeGenerator::AssembleArchInstruction(Instruction* instr) {
  IA32OperandConverter i(this, instr);

  switch (ArchOpcodeField::decode(instr->opcode())) {
    case kArchCallCodeObject: {
      EnsureSpaceForLazyDeopt();
      if (HasImmediateInput(instr, 0)) {
        Handle<Code> code = Handle<Code>::cast(i.InputHeapObject(0));
        __ call(code, RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        __ call(Operand(reg, Code::kHeaderSize - kHeapObjectTag));
      }
      RecordCallPosition(instr);
      break;
    }
    case kArchCallJSFunction: {
      EnsureSpaceForLazyDeopt();
      Register func = i.InputRegister(0);
      if (FLAG_debug_code) {
        // Check the function's context matches the context argument.
        __ cmp(esi, FieldOperand(func, JSFunction::kContextOffset));
        __ Assert(equal, kWrongFunctionContext);
      }
      __ call(FieldOperand(func, JSFunction::kCodeEntryOffset));
      RecordCallPosition(instr);
      break;
    }
    case kArchJmp:
      AssembleArchJump(i.InputRpo(0));
      break;
    case kArchLookupSwitch:
      AssembleArchLookupSwitch(instr);
      break;
    case kArchTableSwitch:
      AssembleArchTableSwitch(instr);
      break;
    case kArchNop:
      // don't emit code for nops.
      break;
    case kArchDeoptimize: {
      int deopt_state_id =
          BuildTranslation(instr, -1, 0, OutputFrameStateCombine::Ignore());
      AssembleDeoptimizerCall(deopt_state_id, Deoptimizer::EAGER);
      break;
    }
    case kArchRet:
      AssembleReturn();
      break;
    case kArchStackPointer:
      __ mov(i.OutputRegister(), esp);
      break;
    case kArchTruncateDoubleToI: {
      auto result = i.OutputRegister();
      auto input = i.InputDoubleRegister(0);
      auto ool = new (zone()) OutOfLineTruncateDoubleToI(this, result, input);
      __ cvttsd2si(result, Operand(input));
      __ cmp(result, 1);
      __ j(overflow, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kIA32Add:
      if (HasImmediateInput(instr, 1)) {
        __ add(i.InputOperand(0), i.InputImmediate(1));
      } else {
        __ add(i.InputRegister(0), i.InputOperand(1));
      }
      break;
    case kIA32And:
      if (HasImmediateInput(instr, 1)) {
        __ and_(i.InputOperand(0), i.InputImmediate(1));
      } else {
        __ and_(i.InputRegister(0), i.InputOperand(1));
      }
      break;
    case kIA32Cmp:
      if (HasImmediateInput(instr, 1)) {
        __ cmp(i.InputOperand(0), i.InputImmediate(1));
      } else {
        __ cmp(i.InputRegister(0), i.InputOperand(1));
      }
      break;
    case kIA32Test:
      if (HasImmediateInput(instr, 1)) {
        __ test(i.InputOperand(0), i.InputImmediate(1));
      } else {
        __ test(i.InputRegister(0), i.InputOperand(1));
      }
      break;
    case kIA32Imul:
      if (HasImmediateInput(instr, 1)) {
        __ imul(i.OutputRegister(), i.InputOperand(0), i.InputInt32(1));
      } else {
        __ imul(i.OutputRegister(), i.InputOperand(1));
      }
      break;
    case kIA32ImulHigh:
      __ imul(i.InputRegister(1));
      break;
    case kIA32UmulHigh:
      __ mul(i.InputRegister(1));
      break;
    case kIA32Idiv:
      __ cdq();
      __ idiv(i.InputOperand(1));
      break;
    case kIA32Udiv:
      __ Move(edx, Immediate(0));
      __ div(i.InputOperand(1));
      break;
    case kIA32Not:
      __ not_(i.OutputOperand());
      break;
    case kIA32Neg:
      __ neg(i.OutputOperand());
      break;
    case kIA32Or:
      if (HasImmediateInput(instr, 1)) {
        __ or_(i.InputOperand(0), i.InputImmediate(1));
      } else {
        __ or_(i.InputRegister(0), i.InputOperand(1));
      }
      break;
    case kIA32Xor:
      if (HasImmediateInput(instr, 1)) {
        __ xor_(i.InputOperand(0), i.InputImmediate(1));
      } else {
        __ xor_(i.InputRegister(0), i.InputOperand(1));
      }
      break;
    case kIA32Sub:
      if (HasImmediateInput(instr, 1)) {
        __ sub(i.InputOperand(0), i.InputImmediate(1));
      } else {
        __ sub(i.InputRegister(0), i.InputOperand(1));
      }
      break;
    case kIA32Shl:
      if (HasImmediateInput(instr, 1)) {
        __ shl(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ shl_cl(i.OutputOperand());
      }
      break;
    case kIA32Shr:
      if (HasImmediateInput(instr, 1)) {
        __ shr(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ shr_cl(i.OutputOperand());
      }
      break;
    case kIA32Sar:
      if (HasImmediateInput(instr, 1)) {
        __ sar(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ sar_cl(i.OutputOperand());
      }
      break;
    case kIA32Ror:
      if (HasImmediateInput(instr, 1)) {
        __ ror(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ ror_cl(i.OutputOperand());
      }
      break;
    case kSSEFloat64Cmp:
      __ ucomisd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kSSEFloat64Add:
      __ addsd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kSSEFloat64Sub:
      __ subsd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kSSEFloat64Mul:
      __ mulsd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kSSEFloat64Div:
      __ divsd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kSSEFloat64Max:
      __ maxsd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kSSEFloat64Min:
      __ minsd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kSSEFloat64Mod: {
      // TODO(dcarney): alignment is wrong.
      __ sub(esp, Immediate(kDoubleSize));
      // Move values to st(0) and st(1).
      __ movsd(Operand(esp, 0), i.InputDoubleRegister(1));
      __ fld_d(Operand(esp, 0));
      __ movsd(Operand(esp, 0), i.InputDoubleRegister(0));
      __ fld_d(Operand(esp, 0));
      // Loop while fprem isn't done.
      Label mod_loop;
      __ bind(&mod_loop);
      // This instructions traps on all kinds inputs, but we are assuming the
      // floating point control word is set to ignore them all.
      __ fprem();
      // The following 2 instruction implicitly use eax.
      __ fnstsw_ax();
      __ sahf();
      __ j(parity_even, &mod_loop);
      // Move output to stack and clean up.
      __ fstp(1);
      __ fstp_d(Operand(esp, 0));
      __ movsd(i.OutputDoubleRegister(), Operand(esp, 0));
      __ add(esp, Immediate(kDoubleSize));
      break;
    }
    case kSSEFloat64Sqrt:
      __ sqrtsd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kSSEFloat64Round: {
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      RoundingMode const mode =
          static_cast<RoundingMode>(MiscField::decode(instr->opcode()));
      __ roundsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0), mode);
      break;
    }
    case kSSECvtss2sd:
      __ cvtss2sd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kSSECvtsd2ss:
      __ cvtsd2ss(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kSSEFloat64ToInt32:
      __ cvttsd2si(i.OutputRegister(), i.InputOperand(0));
      break;
    case kSSEFloat64ToUint32: {
      XMMRegister scratch = xmm0;
      __ Move(scratch, -2147483648.0);
      __ addsd(scratch, i.InputOperand(0));
      __ cvttsd2si(i.OutputRegister(), scratch);
      __ add(i.OutputRegister(), Immediate(0x80000000));
      break;
    }
    case kSSEInt32ToFloat64:
      __ cvtsi2sd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kSSEUint32ToFloat64:
      __ LoadUint32(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kSSEFloat64ExtractLowWord32:
      if (instr->InputAt(0)->IsDoubleStackSlot()) {
        __ mov(i.OutputRegister(), i.InputOperand(0));
      } else {
        __ movd(i.OutputRegister(), i.InputDoubleRegister(0));
      }
      break;
    case kSSEFloat64ExtractHighWord32:
      if (instr->InputAt(0)->IsDoubleStackSlot()) {
        __ mov(i.OutputRegister(), i.InputOperand(0, kDoubleSize / 2));
      } else {
        __ Pextrd(i.OutputRegister(), i.InputDoubleRegister(0), 1);
      }
      break;
    case kSSEFloat64InsertLowWord32:
      __ Pinsrd(i.OutputDoubleRegister(), i.InputOperand(1), 0);
      break;
    case kSSEFloat64InsertHighWord32:
      __ Pinsrd(i.OutputDoubleRegister(), i.InputOperand(1), 1);
      break;
    case kSSEFloat64LoadLowWord32:
      __ movd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kAVXFloat64Add: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vaddsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputOperand(1));
      break;
    }
    case kAVXFloat64Sub: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vsubsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputOperand(1));
      break;
    }
    case kAVXFloat64Mul: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vmulsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputOperand(1));
      break;
    }
    case kAVXFloat64Div: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vdivsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputOperand(1));
      break;
    }
    case kAVXFloat64Max: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vmaxsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputOperand(1));
      break;
    }
    case kAVXFloat64Min: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vminsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputOperand(1));
      break;
    }
    case kIA32Movsxbl:
      __ movsx_b(i.OutputRegister(), i.MemoryOperand());
      break;
    case kIA32Movzxbl:
      __ movzx_b(i.OutputRegister(), i.MemoryOperand());
      break;
    case kIA32Movb: {
      size_t index = 0;
      Operand operand = i.MemoryOperand(&index);
      if (HasImmediateInput(instr, index)) {
        __ mov_b(operand, i.InputInt8(index));
      } else {
        __ mov_b(operand, i.InputRegister(index));
      }
      break;
    }
    case kIA32Movsxwl:
      __ movsx_w(i.OutputRegister(), i.MemoryOperand());
      break;
    case kIA32Movzxwl:
      __ movzx_w(i.OutputRegister(), i.MemoryOperand());
      break;
    case kIA32Movw: {
      size_t index = 0;
      Operand operand = i.MemoryOperand(&index);
      if (HasImmediateInput(instr, index)) {
        __ mov_w(operand, i.InputInt16(index));
      } else {
        __ mov_w(operand, i.InputRegister(index));
      }
      break;
    }
    case kIA32Movl:
      if (instr->HasOutput()) {
        __ mov(i.OutputRegister(), i.MemoryOperand());
      } else {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        if (HasImmediateInput(instr, index)) {
          __ mov(operand, i.InputImmediate(index));
        } else {
          __ mov(operand, i.InputRegister(index));
        }
      }
      break;
    case kIA32Movsd:
      if (instr->HasOutput()) {
        __ movsd(i.OutputDoubleRegister(), i.MemoryOperand());
      } else {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        __ movsd(operand, i.InputDoubleRegister(index));
      }
      break;
    case kIA32Movss:
      if (instr->HasOutput()) {
        __ movss(i.OutputDoubleRegister(), i.MemoryOperand());
      } else {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        __ movss(operand, i.InputDoubleRegister(index));
      }
      break;
    case kIA32Lea: {
      AddressingMode mode = AddressingModeField::decode(instr->opcode());
      // Shorten "leal" to "addl", "subl" or "shll" if the register allocation
      // and addressing mode just happens to work out. The "addl"/"subl" forms
      // in these cases are faster based on measurements.
      if (mode == kMode_MI) {
        __ Move(i.OutputRegister(), Immediate(i.InputInt32(0)));
      } else if (i.InputRegister(0).is(i.OutputRegister())) {
        if (mode == kMode_MRI) {
          int32_t constant_summand = i.InputInt32(1);
          if (constant_summand > 0) {
            __ add(i.OutputRegister(), Immediate(constant_summand));
          } else if (constant_summand < 0) {
            __ sub(i.OutputRegister(), Immediate(-constant_summand));
          }
        } else if (mode == kMode_MR1) {
          if (i.InputRegister(1).is(i.OutputRegister())) {
            __ shl(i.OutputRegister(), 1);
          } else {
            __ lea(i.OutputRegister(), i.MemoryOperand());
          }
        } else if (mode == kMode_M2) {
          __ shl(i.OutputRegister(), 1);
        } else if (mode == kMode_M4) {
          __ shl(i.OutputRegister(), 2);
        } else if (mode == kMode_M8) {
          __ shl(i.OutputRegister(), 3);
        } else {
          __ lea(i.OutputRegister(), i.MemoryOperand());
        }
      } else {
        __ lea(i.OutputRegister(), i.MemoryOperand());
      }
      break;
    }
    case kIA32Push:
      if (HasImmediateInput(instr, 0)) {
        __ push(i.InputImmediate(0));
      } else {
        __ push(i.InputOperand(0));
      }
      break;
    case kIA32StoreWriteBarrier: {
      Register object = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      __ mov(Operand(object, index, times_1, 0), value);
      __ lea(index, Operand(object, index, times_1, 0));
      SaveFPRegsMode mode =
          frame()->DidAllocateDoubleRegisters() ? kSaveFPRegs : kDontSaveFPRegs;
      __ RecordWrite(object, index, value, mode);
      break;
    }
    case kCheckedLoadInt8:
      ASSEMBLE_CHECKED_LOAD_INTEGER(movsx_b);
      break;
    case kCheckedLoadUint8:
      ASSEMBLE_CHECKED_LOAD_INTEGER(movzx_b);
      break;
    case kCheckedLoadInt16:
      ASSEMBLE_CHECKED_LOAD_INTEGER(movsx_w);
      break;
    case kCheckedLoadUint16:
      ASSEMBLE_CHECKED_LOAD_INTEGER(movzx_w);
      break;
    case kCheckedLoadWord32:
      ASSEMBLE_CHECKED_LOAD_INTEGER(mov);
      break;
    case kCheckedLoadFloat32:
      ASSEMBLE_CHECKED_LOAD_FLOAT(movss);
      break;
    case kCheckedLoadFloat64:
      ASSEMBLE_CHECKED_LOAD_FLOAT(movsd);
      break;
    case kCheckedStoreWord8:
      ASSEMBLE_CHECKED_STORE_INTEGER(mov_b);
      break;
    case kCheckedStoreWord16:
      ASSEMBLE_CHECKED_STORE_INTEGER(mov_w);
      break;
    case kCheckedStoreWord32:
      ASSEMBLE_CHECKED_STORE_INTEGER(mov);
      break;
    case kCheckedStoreFloat32:
      ASSEMBLE_CHECKED_STORE_FLOAT(movss);
      break;
    case kCheckedStoreFloat64:
      ASSEMBLE_CHECKED_STORE_FLOAT(movsd);
      break;
    case kIA32StackCheck: {
      ExternalReference const stack_limit =
          ExternalReference::address_of_stack_limit(isolate());
      __ cmp(esp, Operand::StaticVariable(stack_limit));
      break;
    }
  }
}


// Assembles a branch after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  IA32OperandConverter i(this, instr);
  Label::Distance flabel_distance =
      branch->fallthru ? Label::kNear : Label::kFar;
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;
  switch (branch->condition) {
    case kUnorderedEqual:
      __ j(parity_even, flabel, flabel_distance);
    // Fall through.
    case kEqual:
      __ j(equal, tlabel);
      break;
    case kUnorderedNotEqual:
      __ j(parity_even, tlabel);
    // Fall through.
    case kNotEqual:
      __ j(not_equal, tlabel);
      break;
    case kSignedLessThan:
      __ j(less, tlabel);
      break;
    case kSignedGreaterThanOrEqual:
      __ j(greater_equal, tlabel);
      break;
    case kSignedLessThanOrEqual:
      __ j(less_equal, tlabel);
      break;
    case kSignedGreaterThan:
      __ j(greater, tlabel);
      break;
    case kUnsignedLessThan:
      __ j(below, tlabel);
      break;
    case kUnsignedGreaterThanOrEqual:
      __ j(above_equal, tlabel);
      break;
    case kUnsignedLessThanOrEqual:
      __ j(below_equal, tlabel);
      break;
    case kUnsignedGreaterThan:
      __ j(above, tlabel);
      break;
    case kOverflow:
      __ j(overflow, tlabel);
      break;
    case kNotOverflow:
      __ j(no_overflow, tlabel);
      break;
  }
  // Add a jump if not falling through to the next block.
  if (!branch->fallthru) __ jmp(flabel);
}


void CodeGenerator::AssembleArchJump(RpoNumber target) {
  if (!IsNextInAssemblyOrder(target)) __ jmp(GetLabel(target));
}


// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  IA32OperandConverter i(this, instr);
  Label done;

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  Label check;
  DCHECK_NE(0u, instr->OutputCount());
  Register reg = i.OutputRegister(instr->OutputCount() - 1);
  Condition cc = no_condition;
  switch (condition) {
    case kUnorderedEqual:
      __ j(parity_odd, &check, Label::kNear);
      __ Move(reg, Immediate(0));
      __ jmp(&done, Label::kNear);
    // Fall through.
    case kEqual:
      cc = equal;
      break;
    case kUnorderedNotEqual:
      __ j(parity_odd, &check, Label::kNear);
      __ mov(reg, Immediate(1));
      __ jmp(&done, Label::kNear);
    // Fall through.
    case kNotEqual:
      cc = not_equal;
      break;
    case kSignedLessThan:
      cc = less;
      break;
    case kSignedGreaterThanOrEqual:
      cc = greater_equal;
      break;
    case kSignedLessThanOrEqual:
      cc = less_equal;
      break;
    case kSignedGreaterThan:
      cc = greater;
      break;
    case kUnsignedLessThan:
      cc = below;
      break;
    case kUnsignedGreaterThanOrEqual:
      cc = above_equal;
      break;
    case kUnsignedLessThanOrEqual:
      cc = below_equal;
      break;
    case kUnsignedGreaterThan:
      cc = above;
      break;
    case kOverflow:
      cc = overflow;
      break;
    case kNotOverflow:
      cc = no_overflow;
      break;
  }
  __ bind(&check);
  if (reg.is_byte_register()) {
    // setcc for byte registers (al, bl, cl, dl).
    __ setcc(cc, reg);
    __ movzx_b(reg, reg);
  } else {
    // Emit a branch to set a register to either 1 or 0.
    Label set;
    __ j(cc, &set, Label::kNear);
    __ Move(reg, Immediate(0));
    __ jmp(&done, Label::kNear);
    __ bind(&set);
    __ mov(reg, Immediate(1));
  }
  __ bind(&done);
}


void CodeGenerator::AssembleArchLookupSwitch(Instruction* instr) {
  IA32OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    __ cmp(input, Immediate(i.InputInt32(index + 0)));
    __ j(equal, GetLabel(i.InputRpo(index + 1)));
  }
  AssembleArchJump(i.InputRpo(1));
}


void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  IA32OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  size_t const case_count = instr->InputCount() - 2;
  Label** cases = zone()->NewArray<Label*>(case_count);
  for (size_t index = 0; index < case_count; ++index) {
    cases[index] = GetLabel(i.InputRpo(index + 2));
  }
  Label* const table = AddJumpTable(cases, case_count);
  __ cmp(input, Immediate(case_count));
  __ j(above_equal, GetLabel(i.InputRpo(1)));
  __ jmp(Operand::JumpTable(input, times_4, table));
}


void CodeGenerator::AssembleDeoptimizerCall(
    int deoptimization_id, Deoptimizer::BailoutType bailout_type) {
  Address deopt_entry = Deoptimizer::GetDeoptimizationEntry(
      isolate(), deoptimization_id, bailout_type);
  __ call(deopt_entry, RelocInfo::RUNTIME_ENTRY);
}


// The calling convention for JSFunctions on IA32 passes arguments on the
// stack and the JSFunction and context in EDI and ESI, respectively, thus
// the steps of the call look as follows:

// --{ before the call instruction }--------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// --{ push arguments and setup ESI, EDI }--------------------------------------
//                                       | args + receiver |  caller frame |
//                                       ^ esp                             ^ ebp
//                 [edi = JSFunction, esi = context]

// --{ call [edi + kCodeEntryOffset] }------------------------------------------
//                                 | RET | args + receiver |  caller frame |
//                                 ^ esp                                   ^ ebp

// =={ prologue of called function }============================================
// --{ push ebp }---------------------------------------------------------------
//                            | FP | RET | args + receiver |  caller frame |
//                            ^ esp                                        ^ ebp

// --{ mov ebp, esp }-----------------------------------------------------------
//                            | FP | RET | args + receiver |  caller frame |
//                            ^ ebp,esp

// --{ push esi }---------------------------------------------------------------
//                      | CTX | FP | RET | args + receiver |  caller frame |
//                      ^esp  ^ ebp

// --{ push edi }---------------------------------------------------------------
//                | FNC | CTX | FP | RET | args + receiver |  caller frame |
//                ^esp        ^ ebp

// --{ subi esp, #N }-----------------------------------------------------------
// | callee frame | FNC | CTX | FP | RET | args + receiver |  caller frame |
// ^esp                       ^ ebp

// =={ body of called function }================================================

// =={ epilogue of called function }============================================
// --{ mov esp, ebp }-----------------------------------------------------------
//                            | FP | RET | args + receiver |  caller frame |
//                            ^ esp,ebp

// --{ pop ebp }-----------------------------------------------------------
// |                               | RET | args + receiver |  caller frame |
//                                 ^ esp                                   ^ ebp

// --{ ret #A+1 }-----------------------------------------------------------
// |                                                       |  caller frame |
//                                                         ^ esp           ^ ebp


// Runtime function calls are accomplished by doing a stub call to the
// CEntryStub (a real code object). On IA32 passes arguments on the
// stack, the number of arguments in EAX, the address of the runtime function
// in EBX, and the context in ESI.

// --{ before the call instruction }--------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// --{ push arguments and setup EAX, EBX, and ESI }-----------------------------
//                                       | args + receiver |  caller frame |
//                                       ^ esp                             ^ ebp
//              [eax = #args, ebx = runtime function, esi = context]

// --{ call #CEntryStub }-------------------------------------------------------
//                                 | RET | args + receiver |  caller frame |
//                                 ^ esp                                   ^ ebp

// =={ body of runtime function }===============================================

// --{ runtime returns }--------------------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// Other custom linkages (e.g. for calling directly into and out of C++) may
// need to save callee-saved registers on the stack, which is done in the
// function prologue of generated code.

// --{ before the call instruction }--------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// --{ set up arguments in registers on stack }---------------------------------
//                                                  | args |  caller frame |
//                                                  ^ esp                  ^ ebp
//                  [r0 = arg0, r1 = arg1, ...]

// --{ call code }--------------------------------------------------------------
//                                            | RET | args |  caller frame |
//                                            ^ esp                        ^ ebp

// =={ prologue of called function }============================================
// --{ push ebp }---------------------------------------------------------------
//                                       | FP | RET | args |  caller frame |
//                                       ^ esp                             ^ ebp

// --{ mov ebp, esp }-----------------------------------------------------------
//                                       | FP | RET | args |  caller frame |
//                                       ^ ebp,esp

// --{ save registers }---------------------------------------------------------
//                                | regs | FP | RET | args |  caller frame |
//                                ^ esp  ^ ebp

// --{ subi esp, #N }-----------------------------------------------------------
//                 | callee frame | regs | FP | RET | args |  caller frame |
//                 ^esp                  ^ ebp

// =={ body of called function }================================================

// =={ epilogue of called function }============================================
// --{ restore registers }------------------------------------------------------
//                                | regs | FP | RET | args |  caller frame |
//                                ^ esp  ^ ebp

// --{ mov esp, ebp }-----------------------------------------------------------
//                                       | FP | RET | args |  caller frame |
//                                       ^ esp,ebp

// --{ pop ebp }----------------------------------------------------------------
//                                            | RET | args |  caller frame |
//                                            ^ esp                        ^ ebp


void CodeGenerator::AssemblePrologue() {
  CallDescriptor* descriptor = linkage()->GetIncomingDescriptor();
  int stack_slots = frame()->GetSpillSlotCount();
  if (descriptor->kind() == CallDescriptor::kCallAddress) {
    // Assemble a prologue similar the to cdecl calling convention.
    __ push(ebp);
    __ mov(ebp, esp);
    const RegList saves = descriptor->CalleeSavedRegisters();
    if (saves != 0) {  // Save callee-saved registers.
      int register_save_area_size = 0;
      for (int i = Register::kNumRegisters - 1; i >= 0; i--) {
        if (!((1 << i) & saves)) continue;
        __ push(Register::from_code(i));
        register_save_area_size += kPointerSize;
      }
      frame()->SetRegisterSaveAreaSize(register_save_area_size);
    }
  } else if (descriptor->IsJSFunctionCall()) {
    // TODO(turbofan): this prologue is redundant with OSR, but needed for
    // code aging.
    CompilationInfo* info = this->info();
    __ Prologue(info->IsCodePreAgingActive());
    frame()->SetRegisterSaveAreaSize(
        StandardFrameConstants::kFixedFrameSizeFromFp);
  } else if (stack_slots > 0) {
    __ StubPrologue();
    frame()->SetRegisterSaveAreaSize(
        StandardFrameConstants::kFixedFrameSizeFromFp);
  }

  if (info()->is_osr()) {
    // TurboFan OSR-compiled functions cannot be entered directly.
    __ Abort(kShouldNotDirectlyEnterOsrFunction);

    // Unoptimized code jumps directly to this entrypoint while the unoptimized
    // frame is still on the stack. Optimized code uses OSR values directly from
    // the unoptimized frame. Thus, all that needs to be done is to allocate the
    // remaining stack slots.
    if (FLAG_code_comments) __ RecordComment("-- OSR entrypoint --");
    osr_pc_offset_ = __ pc_offset();
    DCHECK(stack_slots >= frame()->GetOsrStackSlotCount());
    stack_slots -= frame()->GetOsrStackSlotCount();
  }

  if (stack_slots > 0) {
    // Allocate the stack slots used by this frame.
    __ sub(esp, Immediate(stack_slots * kPointerSize));
  }
}


void CodeGenerator::AssembleReturn() {
  CallDescriptor* descriptor = linkage()->GetIncomingDescriptor();
  int stack_slots = frame()->GetSpillSlotCount();
  if (descriptor->kind() == CallDescriptor::kCallAddress) {
    const RegList saves = descriptor->CalleeSavedRegisters();
    if (frame()->GetRegisterSaveAreaSize() > 0) {
      // Remove this frame's spill slots first.
      if (stack_slots > 0) {
        __ add(esp, Immediate(stack_slots * kPointerSize));
      }
      // Restore registers.
      if (saves != 0) {
        for (int i = 0; i < Register::kNumRegisters; i++) {
          if (!((1 << i) & saves)) continue;
          __ pop(Register::from_code(i));
        }
      }
      __ pop(ebp);  // Pop caller's frame pointer.
      __ ret(0);
    } else {
      // No saved registers.
      __ mov(esp, ebp);  // Move stack pointer back to frame pointer.
      __ pop(ebp);       // Pop caller's frame pointer.
      __ ret(0);
    }
  } else if (descriptor->IsJSFunctionCall() || stack_slots > 0) {
    __ mov(esp, ebp);  // Move stack pointer back to frame pointer.
    __ pop(ebp);       // Pop caller's frame pointer.
    int pop_count = descriptor->IsJSFunctionCall()
                        ? static_cast<int>(descriptor->JSParameterCount())
                        : 0;
    __ ret(pop_count * kPointerSize);
  } else {
    __ ret(0);
  }
}


void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  IA32OperandConverter g(this, NULL);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Register src = g.ToRegister(source);
    Operand dst = g.ToOperand(destination);
    __ mov(dst, src);
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Operand src = g.ToOperand(source);
    if (destination->IsRegister()) {
      Register dst = g.ToRegister(destination);
      __ mov(dst, src);
    } else {
      Operand dst = g.ToOperand(destination);
      __ push(src);
      __ pop(dst);
    }
  } else if (source->IsConstant()) {
    Constant src_constant = g.ToConstant(source);
    if (src_constant.type() == Constant::kHeapObject) {
      Handle<HeapObject> src = src_constant.ToHeapObject();
      if (info()->IsOptimizing() && src.is_identical_to(info()->context())) {
        // Loading the context from the frame is way cheaper than materializing
        // the actual context heap object address.
        if (destination->IsRegister()) {
          Register dst = g.ToRegister(destination);
          __ mov(dst, Operand(ebp, StandardFrameConstants::kContextOffset));
        } else {
          DCHECK(destination->IsStackSlot());
          Operand dst = g.ToOperand(destination);
          __ push(Operand(ebp, StandardFrameConstants::kContextOffset));
          __ pop(dst);
        }
      } else if (destination->IsRegister()) {
        Register dst = g.ToRegister(destination);
        __ LoadHeapObject(dst, src);
      } else {
        DCHECK(destination->IsStackSlot());
        Operand dst = g.ToOperand(destination);
        AllowDeferredHandleDereference embedding_raw_address;
        if (isolate()->heap()->InNewSpace(*src)) {
          __ PushHeapObject(src);
          __ pop(dst);
        } else {
          __ mov(dst, src);
        }
      }
    } else if (destination->IsRegister()) {
      Register dst = g.ToRegister(destination);
      __ Move(dst, g.ToImmediate(source));
    } else if (destination->IsStackSlot()) {
      Operand dst = g.ToOperand(destination);
      __ Move(dst, g.ToImmediate(source));
    } else if (src_constant.type() == Constant::kFloat32) {
      // TODO(turbofan): Can we do better here?
      uint32_t src = bit_cast<uint32_t>(src_constant.ToFloat32());
      if (destination->IsDoubleRegister()) {
        XMMRegister dst = g.ToDoubleRegister(destination);
        __ Move(dst, src);
      } else {
        DCHECK(destination->IsDoubleStackSlot());
        Operand dst = g.ToOperand(destination);
        __ Move(dst, Immediate(src));
      }
    } else {
      DCHECK_EQ(Constant::kFloat64, src_constant.type());
      uint64_t src = bit_cast<uint64_t>(src_constant.ToFloat64());
      uint32_t lower = static_cast<uint32_t>(src);
      uint32_t upper = static_cast<uint32_t>(src >> 32);
      if (destination->IsDoubleRegister()) {
        XMMRegister dst = g.ToDoubleRegister(destination);
        __ Move(dst, src);
      } else {
        DCHECK(destination->IsDoubleStackSlot());
        Operand dst0 = g.ToOperand(destination);
        Operand dst1 = g.HighOperand(destination);
        __ Move(dst0, Immediate(lower));
        __ Move(dst1, Immediate(upper));
      }
    }
  } else if (source->IsDoubleRegister()) {
    XMMRegister src = g.ToDoubleRegister(source);
    if (destination->IsDoubleRegister()) {
      XMMRegister dst = g.ToDoubleRegister(destination);
      __ movaps(dst, src);
    } else {
      DCHECK(destination->IsDoubleStackSlot());
      Operand dst = g.ToOperand(destination);
      __ movsd(dst, src);
    }
  } else if (source->IsDoubleStackSlot()) {
    DCHECK(destination->IsDoubleRegister() || destination->IsDoubleStackSlot());
    Operand src = g.ToOperand(source);
    if (destination->IsDoubleRegister()) {
      XMMRegister dst = g.ToDoubleRegister(destination);
      __ movsd(dst, src);
    } else {
      // We rely on having xmm0 available as a fixed scratch register.
      Operand dst = g.ToOperand(destination);
      __ movsd(xmm0, src);
      __ movsd(dst, xmm0);
    }
  } else {
    UNREACHABLE();
  }
}


void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  IA32OperandConverter g(this, NULL);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister() && destination->IsRegister()) {
    // Register-register.
    Register src = g.ToRegister(source);
    Register dst = g.ToRegister(destination);
    __ xchg(dst, src);
  } else if (source->IsRegister() && destination->IsStackSlot()) {
    // Register-memory.
    __ xchg(g.ToRegister(source), g.ToOperand(destination));
  } else if (source->IsStackSlot() && destination->IsStackSlot()) {
    // Memory-memory.
    Operand src = g.ToOperand(source);
    Operand dst = g.ToOperand(destination);
    __ push(dst);
    __ push(src);
    __ pop(dst);
    __ pop(src);
  } else if (source->IsDoubleRegister() && destination->IsDoubleRegister()) {
    // XMM register-register swap. We rely on having xmm0
    // available as a fixed scratch register.
    XMMRegister src = g.ToDoubleRegister(source);
    XMMRegister dst = g.ToDoubleRegister(destination);
    __ movaps(xmm0, src);
    __ movaps(src, dst);
    __ movaps(dst, xmm0);
  } else if (source->IsDoubleRegister() && destination->IsDoubleStackSlot()) {
    // XMM register-memory swap.  We rely on having xmm0
    // available as a fixed scratch register.
    XMMRegister reg = g.ToDoubleRegister(source);
    Operand other = g.ToOperand(destination);
    __ movsd(xmm0, other);
    __ movsd(other, reg);
    __ movaps(reg, xmm0);
  } else if (source->IsDoubleStackSlot() && destination->IsDoubleStackSlot()) {
    // Double-width memory-to-memory.
    Operand src0 = g.ToOperand(source);
    Operand src1 = g.HighOperand(source);
    Operand dst0 = g.ToOperand(destination);
    Operand dst1 = g.HighOperand(destination);
    __ movsd(xmm0, dst0);  // Save destination in xmm0.
    __ push(src0);         // Then use stack to copy source to destination.
    __ pop(dst0);
    __ push(src1);
    __ pop(dst1);
    __ movsd(src0, xmm0);
  } else {
    // No other combinations are possible.
    UNREACHABLE();
  }
}


void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  for (size_t index = 0; index < target_count; ++index) {
    __ dd(targets[index]);
  }
}


void CodeGenerator::AddNopForSmiCodeInlining() { __ nop(); }


void CodeGenerator::EnsureSpaceForLazyDeopt() {
  int space_needed = Deoptimizer::patch_size();
  if (!info()->IsStub()) {
    // Ensure that we have enough space after the previous lazy-bailout
    // instruction for patching the code here.
    int current_pc = masm()->pc_offset();
    if (current_pc < last_lazy_deopt_pc_ + space_needed) {
      int padding_size = last_lazy_deopt_pc_ + space_needed - current_pc;
      __ Nop(padding_size);
    }
  }
  MarkLazyDeoptSite();
}

#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
