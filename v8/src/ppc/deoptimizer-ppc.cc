// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/codegen.h"
#include "src/deoptimizer.h"
#include "src/full-codegen.h"
#include "src/safepoint-table.h"

namespace v8 {
namespace internal {

const int Deoptimizer::table_entry_size_ = 8;


int Deoptimizer::patch_size() {
#if V8_TARGET_ARCH_PPC64
  const int kCallInstructionSizeInWords = 7;
#else
  const int kCallInstructionSizeInWords = 4;
#endif
  return kCallInstructionSizeInWords * Assembler::kInstrSize;
}


void Deoptimizer::EnsureRelocSpaceForLazyDeoptimization(Handle<Code> code) {
  // Empty because there is no need for relocation information for the code
  // patching in Deoptimizer::PatchCodeForDeoptimization below.
}


void Deoptimizer::PatchCodeForDeoptimization(Isolate* isolate, Code* code) {
  Address code_start_address = code->instruction_start();

  // Invalidate the relocation information, as it will become invalid by the
  // code patching below, and is not needed any more.
  code->InvalidateRelocation();

  if (FLAG_zap_code_space) {
    // Fail hard and early if we enter this code object again.
    byte* pointer = code->FindCodeAgeSequence();
    if (pointer != NULL) {
      pointer += kNoCodeAgeSequenceLength;
    } else {
      pointer = code->instruction_start();
    }
    CodePatcher patcher(pointer, 1);
    patcher.masm()->bkpt(0);

    DeoptimizationInputData* data =
        DeoptimizationInputData::cast(code->deoptimization_data());
    int osr_offset = data->OsrPcOffset()->value();
    if (osr_offset > 0) {
      CodePatcher osr_patcher(code->instruction_start() + osr_offset, 1);
      osr_patcher.masm()->bkpt(0);
    }
  }

  DeoptimizationInputData* deopt_data =
      DeoptimizationInputData::cast(code->deoptimization_data());
#ifdef DEBUG
  Address prev_call_address = NULL;
#endif
  // For each LLazyBailout instruction insert a call to the corresponding
  // deoptimization entry.
  for (int i = 0; i < deopt_data->DeoptCount(); i++) {
    if (deopt_data->Pc(i)->value() == -1) continue;
    Address call_address = code_start_address + deopt_data->Pc(i)->value();
    Address deopt_entry = GetDeoptimizationEntry(isolate, i, LAZY);
    // We need calls to have a predictable size in the unoptimized code, but
    // this is optimized code, so we don't have to have a predictable size.
    int call_size_in_bytes = MacroAssembler::CallSizeNotPredictableCodeSize(
        deopt_entry, kRelocInfo_NONEPTR);
    int call_size_in_words = call_size_in_bytes / Assembler::kInstrSize;
    DCHECK(call_size_in_bytes % Assembler::kInstrSize == 0);
    DCHECK(call_size_in_bytes <= patch_size());
    CodePatcher patcher(call_address, call_size_in_words);
    patcher.masm()->Call(deopt_entry, kRelocInfo_NONEPTR);
    DCHECK(prev_call_address == NULL ||
           call_address >= prev_call_address + patch_size());
    DCHECK(call_address + patch_size() <= code->instruction_end());
#ifdef DEBUG
    prev_call_address = call_address;
#endif
  }
}


void Deoptimizer::FillInputFrame(Address tos, JavaScriptFrame* frame) {
  // Set the register values. The values are not important as there are no
  // callee saved registers in JavaScript frames, so all registers are
  // spilled. Registers fp and sp are set to the correct values though.
  // We ensure the values are Smis to avoid confusing the garbage
  // collector in the event that any values are retreived and stored
  // elsewhere.

  for (int i = 0; i < Register::kNumRegisters; i++) {
    input_->SetRegister(i, reinterpret_cast<intptr_t>(Smi::FromInt(i)));
  }
  input_->SetRegister(sp.code(), reinterpret_cast<intptr_t>(frame->sp()));
  input_->SetRegister(fp.code(), reinterpret_cast<intptr_t>(frame->fp()));
  for (int i = 0; i < DoubleRegister::NumAllocatableRegisters(); i++) {
    input_->SetDoubleRegister(i, 0.0);
  }

  // Fill the frame content from the actual data on the frame.
  for (unsigned i = 0; i < input_->GetFrameSize(); i += kPointerSize) {
    input_->SetFrameSlot(
        i, reinterpret_cast<intptr_t>(Memory::Address_at(tos + i)));
  }
}


void Deoptimizer::SetPlatformCompiledStubRegisters(
    FrameDescription* output_frame, CodeStubDescriptor* descriptor) {
  ApiFunction function(descriptor->deoptimization_handler());
  ExternalReference xref(&function, ExternalReference::BUILTIN_CALL, isolate_);
  intptr_t handler = reinterpret_cast<intptr_t>(xref.address());
  int params = descriptor->GetHandlerParameterCount();
  output_frame->SetRegister(r3.code(), params);
  output_frame->SetRegister(r4.code(), handler);
}


void Deoptimizer::CopyDoubleRegisters(FrameDescription* output_frame) {
  for (int i = 0; i < DoubleRegister::kMaxNumRegisters; ++i) {
    double double_value = input_->GetDoubleRegister(i);
    output_frame->SetDoubleRegister(i, double_value);
  }
}


bool Deoptimizer::HasAlignmentPadding(JSFunction* function) {
  // There is no dynamic alignment padding on PPC in the input frame.
  return false;
}


#define __ masm()->

// This code tries to be close to ia32 code so that any changes can be
// easily ported.
void Deoptimizer::EntryGenerator::Generate() {
  GeneratePrologue();

  // Unlike on ARM we don't save all the registers, just the useful ones.
  // For the rest, there are gaps on the stack, so the offsets remain the same.
  const int kNumberOfRegisters = Register::kNumRegisters;

  RegList restored_regs = kJSCallerSaved | kCalleeSaved;
  RegList saved_regs = restored_regs | sp.bit();

  const int kDoubleRegsSize =
      kDoubleSize * DoubleRegister::kMaxNumAllocatableRegisters;

  // Save all FPU registers before messing with them.
  __ subi(sp, sp, Operand(kDoubleRegsSize));
  for (int i = 0; i < DoubleRegister::kMaxNumAllocatableRegisters; ++i) {
    DoubleRegister fpu_reg = DoubleRegister::FromAllocationIndex(i);
    int offset = i * kDoubleSize;
    __ stfd(fpu_reg, MemOperand(sp, offset));
  }

  // Push saved_regs (needed to populate FrameDescription::registers_).
  // Leave gaps for other registers.
  __ subi(sp, sp, Operand(kNumberOfRegisters * kPointerSize));
  for (int16_t i = kNumberOfRegisters - 1; i >= 0; i--) {
    if ((saved_regs & (1 << i)) != 0) {
      __ StoreP(ToRegister(i), MemOperand(sp, kPointerSize * i));
    }
  }

  __ mov(ip, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  __ StoreP(fp, MemOperand(ip));

  const int kSavedRegistersAreaSize =
      (kNumberOfRegisters * kPointerSize) + kDoubleRegsSize;

  // Get the bailout id from the stack.
  __ LoadP(r5, MemOperand(sp, kSavedRegistersAreaSize));

  // Get the address of the location in the code object (r6) (return
  // address for lazy deoptimization) and compute the fp-to-sp delta in
  // register r7.
  __ mflr(r6);
  // Correct one word for bailout id.
  __ addi(r7, sp, Operand(kSavedRegistersAreaSize + (1 * kPointerSize)));
  __ sub(r7, fp, r7);

  // Allocate a new deoptimizer object.
  // Pass six arguments in r3 to r8.
  __ PrepareCallCFunction(6, r8);
  __ LoadP(r3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ li(r4, Operand(type()));  // bailout type,
  // r5: bailout id already loaded.
  // r6: code address or 0 already loaded.
  // r7: Fp-to-sp delta.
  __ mov(r8, Operand(ExternalReference::isolate_address(isolate())));
  // Call Deoptimizer::New().
  {
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(ExternalReference::new_deoptimizer_function(isolate()), 6);
  }

  // Preserve "deoptimizer" object in register r3 and get the input
  // frame descriptor pointer to r4 (deoptimizer->input_);
  __ LoadP(r4, MemOperand(r3, Deoptimizer::input_offset()));

  // Copy core registers into FrameDescription::registers_[kNumRegisters].
  DCHECK(Register::kNumRegisters == kNumberOfRegisters);
  for (int i = 0; i < kNumberOfRegisters; i++) {
    int offset = (i * kPointerSize) + FrameDescription::registers_offset();
    __ LoadP(r5, MemOperand(sp, i * kPointerSize));
    __ StoreP(r5, MemOperand(r4, offset));
  }

  int double_regs_offset = FrameDescription::double_registers_offset();
  // Copy VFP registers to
  // double_registers_[DoubleRegister::kNumAllocatableRegisters]
  for (int i = 0; i < DoubleRegister::NumAllocatableRegisters(); ++i) {
    int dst_offset = i * kDoubleSize + double_regs_offset;
    int src_offset = i * kDoubleSize + kNumberOfRegisters * kPointerSize;
    __ lfd(d0, MemOperand(sp, src_offset));
    __ stfd(d0, MemOperand(r4, dst_offset));
  }

  // Remove the bailout id and the saved registers from the stack.
  __ addi(sp, sp, Operand(kSavedRegistersAreaSize + (1 * kPointerSize)));

  // Compute a pointer to the unwinding limit in register r5; that is
  // the first stack slot not part of the input frame.
  __ LoadP(r5, MemOperand(r4, FrameDescription::frame_size_offset()));
  __ add(r5, r5, sp);

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ addi(r6, r4, Operand(FrameDescription::frame_content_offset()));
  Label pop_loop;
  Label pop_loop_header;
  __ b(&pop_loop_header);
  __ bind(&pop_loop);
  __ pop(r7);
  __ StoreP(r7, MemOperand(r6, 0));
  __ addi(r6, r6, Operand(kPointerSize));
  __ bind(&pop_loop_header);
  __ cmp(r5, sp);
  __ bne(&pop_loop);

  // Compute the output frame in the deoptimizer.
  __ push(r3);  // Preserve deoptimizer object across call.
  // r3: deoptimizer object; r4: scratch.
  __ PrepareCallCFunction(1, r4);
  // Call Deoptimizer::ComputeOutputFrames().
  {
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(
        ExternalReference::compute_output_frames_function(isolate()), 1);
  }
  __ pop(r3);  // Restore deoptimizer object (class Deoptimizer).

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, inner_push_loop, outer_loop_header, inner_loop_header;
  // Outer loop state: r7 = current "FrameDescription** output_",
  // r4 = one past the last FrameDescription**.
  __ lwz(r4, MemOperand(r3, Deoptimizer::output_count_offset()));
  __ LoadP(r7, MemOperand(r3, Deoptimizer::output_offset()));  // r7 is output_.
  __ ShiftLeftImm(r4, r4, Operand(kPointerSizeLog2));
  __ add(r4, r7, r4);
  __ b(&outer_loop_header);

  __ bind(&outer_push_loop);
  // Inner loop state: r5 = current FrameDescription*, r6 = loop index.
  __ LoadP(r5, MemOperand(r7, 0));  // output_[ix]
  __ LoadP(r6, MemOperand(r5, FrameDescription::frame_size_offset()));
  __ b(&inner_loop_header);

  __ bind(&inner_push_loop);
  __ addi(r6, r6, Operand(-sizeof(intptr_t)));
  __ add(r9, r5, r6);
  __ LoadP(r9, MemOperand(r9, FrameDescription::frame_content_offset()));
  __ push(r9);

  __ bind(&inner_loop_header);
  __ cmpi(r6, Operand::Zero());
  __ bne(&inner_push_loop);  // test for gt?

  __ addi(r7, r7, Operand(kPointerSize));
  __ bind(&outer_loop_header);
  __ cmp(r7, r4);
  __ blt(&outer_push_loop);

  __ LoadP(r4, MemOperand(r3, Deoptimizer::input_offset()));
  for (int i = 0; i < DoubleRegister::kMaxNumAllocatableRegisters; ++i) {
    const DoubleRegister dreg = DoubleRegister::FromAllocationIndex(i);
    int src_offset = i * kDoubleSize + double_regs_offset;
    __ lfd(dreg, MemOperand(r4, src_offset));
  }

  // Push state, pc, and continuation from the last output frame.
  __ LoadP(r9, MemOperand(r5, FrameDescription::state_offset()));
  __ push(r9);
  __ LoadP(r9, MemOperand(r5, FrameDescription::pc_offset()));
  __ push(r9);
  __ LoadP(r9, MemOperand(r5, FrameDescription::continuation_offset()));
  __ push(r9);

  // Restore the registers from the last output frame.
  DCHECK(!(ip.bit() & restored_regs));
  __ mr(ip, r5);
  for (int i = kNumberOfRegisters - 1; i >= 0; i--) {
    int offset = (i * kPointerSize) + FrameDescription::registers_offset();
    if ((restored_regs & (1 << i)) != 0) {
      __ LoadP(ToRegister(i), MemOperand(ip, offset));
    }
  }

  __ InitializeRootRegister();

  __ pop(ip);  // get continuation, leave pc on stack
  __ pop(r0);
  __ mtlr(r0);
  __ Jump(ip);
  __ stop("Unreachable.");
}


void Deoptimizer::TableEntryGenerator::GeneratePrologue() {
  Assembler::BlockTrampolinePoolScope block_trampoline_pool(masm());

  // Create a sequence of deoptimization entries.
  // Note that registers are still live when jumping to an entry.
  Label done;
  for (int i = 0; i < count(); i++) {
    int start = masm()->pc_offset();
    USE(start);
    __ li(ip, Operand(i));
    __ b(&done);
    DCHECK(masm()->pc_offset() - start == table_entry_size_);
  }
  __ bind(&done);
  __ push(ip);
}


void FrameDescription::SetCallerPc(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}


void FrameDescription::SetCallerFp(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}


void FrameDescription::SetCallerConstantPool(unsigned offset, intptr_t value) {
  // No out-of-line constant pool support.
  UNREACHABLE();
}


#undef __
}
}  // namespace v8::internal
