// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/accessors.h"
#include "src/api.h"
#include "src/base/platform/platform.h"
#include "src/bootstrapper.h"
#include "src/code-stubs.h"
#include "src/cpu-profiler.h"
#include "src/deoptimizer.h"
#include "src/execution.h"
#include "src/global-handles.h"
#include "src/ic/ic.h"
#include "src/ic/stub-cache.h"
#include "src/natives.h"
#include "src/objects.h"
#include "src/parser.h"
#include "src/runtime/runtime.h"
#include "src/serialize.h"
#include "src/snapshot.h"
#include "src/snapshot-source-sink.h"
#include "src/v8threads.h"
#include "src/version.h"

namespace v8 {
namespace internal {


// -----------------------------------------------------------------------------
// Coding of external references.


ExternalReferenceTable* ExternalReferenceTable::instance(Isolate* isolate) {
  ExternalReferenceTable* external_reference_table =
      isolate->external_reference_table();
  if (external_reference_table == NULL) {
    external_reference_table = new ExternalReferenceTable(isolate);
    isolate->set_external_reference_table(external_reference_table);
  }
  return external_reference_table;
}


ExternalReferenceTable::ExternalReferenceTable(Isolate* isolate) {
  // Miscellaneous
  Add(ExternalReference::roots_array_start(isolate).address(),
      "Heap::roots_array_start()");
  Add(ExternalReference::address_of_stack_limit(isolate).address(),
      "StackGuard::address_of_jslimit()");
  Add(ExternalReference::address_of_real_stack_limit(isolate).address(),
      "StackGuard::address_of_real_jslimit()");
  Add(ExternalReference::new_space_start(isolate).address(),
      "Heap::NewSpaceStart()");
  Add(ExternalReference::new_space_mask(isolate).address(),
      "Heap::NewSpaceMask()");
  Add(ExternalReference::new_space_allocation_limit_address(isolate).address(),
      "Heap::NewSpaceAllocationLimitAddress()");
  Add(ExternalReference::new_space_allocation_top_address(isolate).address(),
      "Heap::NewSpaceAllocationTopAddress()");
  Add(ExternalReference::debug_break(isolate).address(), "Debug::Break()");
  Add(ExternalReference::debug_step_in_fp_address(isolate).address(),
      "Debug::step_in_fp_addr()");
  Add(ExternalReference::mod_two_doubles_operation(isolate).address(),
      "mod_two_doubles");
  // Keyed lookup cache.
  Add(ExternalReference::keyed_lookup_cache_keys(isolate).address(),
      "KeyedLookupCache::keys()");
  Add(ExternalReference::keyed_lookup_cache_field_offsets(isolate).address(),
      "KeyedLookupCache::field_offsets()");
  Add(ExternalReference::handle_scope_next_address(isolate).address(),
      "HandleScope::next");
  Add(ExternalReference::handle_scope_limit_address(isolate).address(),
      "HandleScope::limit");
  Add(ExternalReference::handle_scope_level_address(isolate).address(),
      "HandleScope::level");
  Add(ExternalReference::new_deoptimizer_function(isolate).address(),
      "Deoptimizer::New()");
  Add(ExternalReference::compute_output_frames_function(isolate).address(),
      "Deoptimizer::ComputeOutputFrames()");
  Add(ExternalReference::address_of_min_int().address(),
      "LDoubleConstant::min_int");
  Add(ExternalReference::address_of_one_half().address(),
      "LDoubleConstant::one_half");
  Add(ExternalReference::isolate_address(isolate).address(), "isolate");
  Add(ExternalReference::address_of_negative_infinity().address(),
      "LDoubleConstant::negative_infinity");
  Add(ExternalReference::power_double_double_function(isolate).address(),
      "power_double_double_function");
  Add(ExternalReference::power_double_int_function(isolate).address(),
      "power_double_int_function");
  Add(ExternalReference::math_log_double_function(isolate).address(),
      "std::log");
  Add(ExternalReference::store_buffer_top(isolate).address(),
      "store_buffer_top");
  Add(ExternalReference::address_of_the_hole_nan().address(), "the_hole_nan");
  Add(ExternalReference::get_date_field_function(isolate).address(),
      "JSDate::GetField");
  Add(ExternalReference::date_cache_stamp(isolate).address(),
      "date_cache_stamp");
  Add(ExternalReference::address_of_pending_message_obj(isolate).address(),
      "address_of_pending_message_obj");
  Add(ExternalReference::get_make_code_young_function(isolate).address(),
      "Code::MakeCodeYoung");
  Add(ExternalReference::cpu_features().address(), "cpu_features");
  Add(ExternalReference::old_pointer_space_allocation_top_address(isolate)
          .address(),
      "Heap::OldPointerSpaceAllocationTopAddress");
  Add(ExternalReference::old_pointer_space_allocation_limit_address(isolate)
          .address(),
      "Heap::OldPointerSpaceAllocationLimitAddress");
  Add(ExternalReference::old_data_space_allocation_top_address(isolate)
          .address(),
      "Heap::OldDataSpaceAllocationTopAddress");
  Add(ExternalReference::old_data_space_allocation_limit_address(isolate)
          .address(),
      "Heap::OldDataSpaceAllocationLimitAddress");
  Add(ExternalReference::allocation_sites_list_address(isolate).address(),
      "Heap::allocation_sites_list_address()");
  Add(ExternalReference::address_of_uint32_bias().address(), "uint32_bias");
  Add(ExternalReference::get_mark_code_as_executed_function(isolate).address(),
      "Code::MarkCodeAsExecuted");
  Add(ExternalReference::is_profiling_address(isolate).address(),
      "CpuProfiler::is_profiling");
  Add(ExternalReference::scheduled_exception_address(isolate).address(),
      "Isolate::scheduled_exception");
  Add(ExternalReference::invoke_function_callback(isolate).address(),
      "InvokeFunctionCallback");
  Add(ExternalReference::invoke_accessor_getter_callback(isolate).address(),
      "InvokeAccessorGetterCallback");
  Add(ExternalReference::flush_icache_function(isolate).address(),
      "CpuFeatures::FlushICache");
  Add(ExternalReference::log_enter_external_function(isolate).address(),
      "Logger::EnterExternal");
  Add(ExternalReference::log_leave_external_function(isolate).address(),
      "Logger::LeaveExternal");
  Add(ExternalReference::address_of_minus_one_half().address(),
      "double_constants.minus_one_half");
  Add(ExternalReference::stress_deopt_count(isolate).address(),
      "Isolate::stress_deopt_count_address()");

  // Debug addresses
  Add(ExternalReference::debug_after_break_target_address(isolate).address(),
      "Debug::after_break_target_address()");
  Add(ExternalReference::debug_restarter_frame_function_pointer_address(isolate)
          .address(),
      "Debug::restarter_frame_function_pointer_address()");
  Add(ExternalReference::debug_is_active_address(isolate).address(),
      "Debug::is_active_address()");

#ifndef V8_INTERPRETED_REGEXP
  Add(ExternalReference::re_case_insensitive_compare_uc16(isolate).address(),
      "NativeRegExpMacroAssembler::CaseInsensitiveCompareUC16()");
  Add(ExternalReference::re_check_stack_guard_state(isolate).address(),
      "RegExpMacroAssembler*::CheckStackGuardState()");
  Add(ExternalReference::re_grow_stack(isolate).address(),
      "NativeRegExpMacroAssembler::GrowStack()");
  Add(ExternalReference::re_word_character_map().address(),
      "NativeRegExpMacroAssembler::word_character_map");
  Add(ExternalReference::address_of_regexp_stack_limit(isolate).address(),
      "RegExpStack::limit_address()");
  Add(ExternalReference::address_of_regexp_stack_memory_address(isolate)
          .address(),
      "RegExpStack::memory_address()");
  Add(ExternalReference::address_of_regexp_stack_memory_size(isolate).address(),
      "RegExpStack::memory_size()");
  Add(ExternalReference::address_of_static_offsets_vector(isolate).address(),
      "OffsetsVector::static_offsets_vector");
#endif  // V8_INTERPRETED_REGEXP

  // The following populates all of the different type of external references
  // into the ExternalReferenceTable.
  //
  // NOTE: This function was originally 100k of code.  It has since been
  // rewritten to be mostly table driven, as the callback macro style tends to
  // very easily cause code bloat.  Please be careful in the future when adding
  // new references.

  struct RefTableEntry {
    uint16_t id;
    const char* name;
  };

  static const RefTableEntry c_builtins[] = {
#define DEF_ENTRY_C(name, ignored)           \
  { Builtins::c_##name, "Builtins::" #name } \
  ,
      BUILTIN_LIST_C(DEF_ENTRY_C)
#undef DEF_ENTRY_C
  };

  for (unsigned i = 0; i < arraysize(c_builtins); ++i) {
    ExternalReference ref(static_cast<Builtins::CFunctionId>(c_builtins[i].id),
                          isolate);
    Add(ref.address(), c_builtins[i].name);
  }

  static const RefTableEntry builtins[] = {
#define DEF_ENTRY_C(name, ignored)          \
  { Builtins::k##name, "Builtins::" #name } \
  ,
#define DEF_ENTRY_A(name, i1, i2, i3)       \
  { Builtins::k##name, "Builtins::" #name } \
  ,
      BUILTIN_LIST_C(DEF_ENTRY_C) BUILTIN_LIST_A(DEF_ENTRY_A)
          BUILTIN_LIST_DEBUG_A(DEF_ENTRY_A)
#undef DEF_ENTRY_C
#undef DEF_ENTRY_A
  };

  for (unsigned i = 0; i < arraysize(builtins); ++i) {
    ExternalReference ref(static_cast<Builtins::Name>(builtins[i].id), isolate);
    Add(ref.address(), builtins[i].name);
  }

  static const RefTableEntry runtime_functions[] = {
#define RUNTIME_ENTRY(name, i1, i2)       \
  { Runtime::k##name, "Runtime::" #name } \
  ,
      FOR_EACH_INTRINSIC(RUNTIME_ENTRY)
#undef RUNTIME_ENTRY
  };

  for (unsigned i = 0; i < arraysize(runtime_functions); ++i) {
    ExternalReference ref(
        static_cast<Runtime::FunctionId>(runtime_functions[i].id), isolate);
    Add(ref.address(), runtime_functions[i].name);
  }

  static const RefTableEntry inline_caches[] = {
#define IC_ENTRY(name)          \
  { IC::k##name, "IC::" #name } \
  ,
      IC_UTIL_LIST(IC_ENTRY)
#undef IC_ENTRY
  };

  for (unsigned i = 0; i < arraysize(inline_caches); ++i) {
    ExternalReference ref(
        IC_Utility(static_cast<IC::UtilityId>(inline_caches[i].id)), isolate);
    Add(ref.address(), runtime_functions[i].name);
  }

  // Stat counters
  struct StatsRefTableEntry {
    StatsCounter* (Counters::*counter)();
    const char* name;
  };

  static const StatsRefTableEntry stats_ref_table[] = {
#define COUNTER_ENTRY(name, caption)      \
  { &Counters::name, "Counters::" #name } \
  ,
      STATS_COUNTER_LIST_1(COUNTER_ENTRY) STATS_COUNTER_LIST_2(COUNTER_ENTRY)
#undef COUNTER_ENTRY
  };

  Counters* counters = isolate->counters();
  for (unsigned i = 0; i < arraysize(stats_ref_table); ++i) {
    // To make sure the indices are not dependent on whether counters are
    // enabled, use a dummy address as filler.
    Address address = NotAvailable();
    StatsCounter* counter = (counters->*(stats_ref_table[i].counter))();
    if (counter->Enabled()) {
      address = reinterpret_cast<Address>(counter->GetInternalPointer());
    }
    Add(address, stats_ref_table[i].name);
  }

  // Top addresses
  static const char* address_names[] = {
#define BUILD_NAME_LITERAL(Name, name) "Isolate::" #name "_address",
      FOR_EACH_ISOLATE_ADDRESS_NAME(BUILD_NAME_LITERAL) NULL
#undef BUILD_NAME_LITERAL
  };

  for (int i = 0; i < Isolate::kIsolateAddressCount; ++i) {
    Add(isolate->get_address_from_id(static_cast<Isolate::AddressId>(i)),
        address_names[i]);
  }

  // Accessors
  struct AccessorRefTable {
    Address address;
    const char* name;
  };

  static const AccessorRefTable accessors[] = {
#define ACCESSOR_INFO_DECLARATION(name)                                     \
  { FUNCTION_ADDR(&Accessors::name##Getter), "Accessors::" #name "Getter" } \
  , {FUNCTION_ADDR(&Accessors::name##Setter), "Accessors::" #name "Setter"},
      ACCESSOR_INFO_LIST(ACCESSOR_INFO_DECLARATION)
#undef ACCESSOR_INFO_DECLARATION
  };

  for (unsigned i = 0; i < arraysize(accessors); ++i) {
    Add(accessors[i].address, accessors[i].name);
  }

  StubCache* stub_cache = isolate->stub_cache();

  // Stub cache tables
  Add(stub_cache->key_reference(StubCache::kPrimary).address(),
      "StubCache::primary_->key");
  Add(stub_cache->value_reference(StubCache::kPrimary).address(),
      "StubCache::primary_->value");
  Add(stub_cache->map_reference(StubCache::kPrimary).address(),
      "StubCache::primary_->map");
  Add(stub_cache->key_reference(StubCache::kSecondary).address(),
      "StubCache::secondary_->key");
  Add(stub_cache->value_reference(StubCache::kSecondary).address(),
      "StubCache::secondary_->value");
  Add(stub_cache->map_reference(StubCache::kSecondary).address(),
      "StubCache::secondary_->map");

  // Runtime entries
  Add(ExternalReference::delete_handle_scope_extensions(isolate).address(),
      "HandleScope::DeleteExtensions");
  Add(ExternalReference::incremental_marking_record_write_function(isolate)
          .address(),
      "IncrementalMarking::RecordWrite");
  Add(ExternalReference::store_buffer_overflow_function(isolate).address(),
      "StoreBuffer::StoreBufferOverflow");

  // Add a small set of deopt entry addresses to encoder without generating the
  // deopt table code, which isn't possible at deserialization time.
  HandleScope scope(isolate);
  for (int entry = 0; entry < kDeoptTableSerializeEntryCount; ++entry) {
    Address address = Deoptimizer::GetDeoptimizationEntry(
        isolate,
        entry,
        Deoptimizer::LAZY,
        Deoptimizer::CALCULATE_ENTRY_ADDRESS);
    Add(address, "lazy_deopt");
  }
}


ExternalReferenceEncoder::ExternalReferenceEncoder(Isolate* isolate)
    : map_(HashMap::PointersMatch) {
  ExternalReferenceTable* table = ExternalReferenceTable::instance(isolate);
  for (int i = 0; i < table->size(); ++i) {
    Address addr = table->address(i);
    if (addr == ExternalReferenceTable::NotAvailable()) continue;
    // We expect no duplicate external references entries in the table.
    DCHECK_NULL(map_.Lookup(addr, Hash(addr), false));
    map_.Lookup(addr, Hash(addr), true)->value = reinterpret_cast<void*>(i);
  }
}


uint32_t ExternalReferenceEncoder::Encode(Address address) const {
  DCHECK_NOT_NULL(address);
  HashMap::Entry* entry =
      const_cast<HashMap&>(map_).Lookup(address, Hash(address), false);
  DCHECK_NOT_NULL(entry);
  return static_cast<uint32_t>(reinterpret_cast<intptr_t>(entry->value));
}


const char* ExternalReferenceEncoder::NameOfAddress(Isolate* isolate,
                                                    Address address) const {
  HashMap::Entry* entry =
      const_cast<HashMap&>(map_).Lookup(address, Hash(address), false);
  if (entry == NULL) return "<unknown>";
  uint32_t i = static_cast<uint32_t>(reinterpret_cast<intptr_t>(entry->value));
  return ExternalReferenceTable::instance(isolate)->name(i);
}


RootIndexMap::RootIndexMap(Isolate* isolate) : map_(HashMap::PointersMatch) {
  Object** root_array = isolate->heap()->roots_array_start();
  for (uint32_t i = 0; i < Heap::kStrongRootListLength; i++) {
    Heap::RootListIndex root_index = static_cast<Heap::RootListIndex>(i);
    Object* root = root_array[root_index];
    // Omit root entries that can be written after initialization. They must
    // not be referenced through the root list in the snapshot.
    if (root->IsHeapObject() &&
        isolate->heap()->RootCanBeTreatedAsConstant(root_index)) {
      HeapObject* heap_object = HeapObject::cast(root);
      HashMap::Entry* entry = LookupEntry(&map_, heap_object, false);
      if (entry != NULL) {
        // Some are initialized to a previous value in the root list.
        DCHECK_LT(GetValue(entry), i);
      } else {
        SetValue(LookupEntry(&map_, heap_object, true), i);
      }
    }
  }
}


class CodeAddressMap: public CodeEventLogger {
 public:
  explicit CodeAddressMap(Isolate* isolate)
      : isolate_(isolate) {
    isolate->logger()->addCodeEventListener(this);
  }

  virtual ~CodeAddressMap() {
    isolate_->logger()->removeCodeEventListener(this);
  }

  virtual void CodeMoveEvent(Address from, Address to) {
    address_to_name_map_.Move(from, to);
  }

  virtual void CodeDisableOptEvent(Code* code, SharedFunctionInfo* shared) {
  }

  virtual void CodeDeleteEvent(Address from) {
    address_to_name_map_.Remove(from);
  }

  const char* Lookup(Address address) {
    return address_to_name_map_.Lookup(address);
  }

 private:
  class NameMap {
   public:
    NameMap() : impl_(HashMap::PointersMatch) {}

    ~NameMap() {
      for (HashMap::Entry* p = impl_.Start(); p != NULL; p = impl_.Next(p)) {
        DeleteArray(static_cast<const char*>(p->value));
      }
    }

    void Insert(Address code_address, const char* name, int name_size) {
      HashMap::Entry* entry = FindOrCreateEntry(code_address);
      if (entry->value == NULL) {
        entry->value = CopyName(name, name_size);
      }
    }

    const char* Lookup(Address code_address) {
      HashMap::Entry* entry = FindEntry(code_address);
      return (entry != NULL) ? static_cast<const char*>(entry->value) : NULL;
    }

    void Remove(Address code_address) {
      HashMap::Entry* entry = FindEntry(code_address);
      if (entry != NULL) {
        DeleteArray(static_cast<char*>(entry->value));
        RemoveEntry(entry);
      }
    }

    void Move(Address from, Address to) {
      if (from == to) return;
      HashMap::Entry* from_entry = FindEntry(from);
      DCHECK(from_entry != NULL);
      void* value = from_entry->value;
      RemoveEntry(from_entry);
      HashMap::Entry* to_entry = FindOrCreateEntry(to);
      DCHECK(to_entry->value == NULL);
      to_entry->value = value;
    }

   private:
    static char* CopyName(const char* name, int name_size) {
      char* result = NewArray<char>(name_size + 1);
      for (int i = 0; i < name_size; ++i) {
        char c = name[i];
        if (c == '\0') c = ' ';
        result[i] = c;
      }
      result[name_size] = '\0';
      return result;
    }

    HashMap::Entry* FindOrCreateEntry(Address code_address) {
      return impl_.Lookup(code_address, ComputePointerHash(code_address), true);
    }

    HashMap::Entry* FindEntry(Address code_address) {
      return impl_.Lookup(code_address,
                          ComputePointerHash(code_address),
                          false);
    }

    void RemoveEntry(HashMap::Entry* entry) {
      impl_.Remove(entry->key, entry->hash);
    }

    HashMap impl_;

    DISALLOW_COPY_AND_ASSIGN(NameMap);
  };

  virtual void LogRecordedBuffer(Code* code,
                                 SharedFunctionInfo*,
                                 const char* name,
                                 int length) {
    address_to_name_map_.Insert(code->address(), name, length);
  }

  NameMap address_to_name_map_;
  Isolate* isolate_;
};


void Deserializer::DecodeReservation(
    Vector<const SerializedData::Reservation> res) {
  DCHECK_EQ(0, reservations_[NEW_SPACE].length());
  STATIC_ASSERT(NEW_SPACE == 0);
  int current_space = NEW_SPACE;
  for (int i = 0; i < res.length(); i++) {
    SerializedData::Reservation r(0);
    memcpy(&r, res.start() + i, sizeof(r));
    reservations_[current_space].Add({r.chunk_size(), NULL, NULL});
    if (r.is_last()) current_space++;
  }
  DCHECK_EQ(kNumberOfSpaces, current_space);
  for (int i = 0; i < kNumberOfPreallocatedSpaces; i++) current_chunk_[i] = 0;
}


void Deserializer::FlushICacheForNewCodeObjects() {
  PageIterator it(isolate_->heap()->code_space());
  while (it.has_next()) {
    Page* p = it.next();
    CpuFeatures::FlushICache(p->area_start(), p->area_end() - p->area_start());
  }
}


bool Deserializer::ReserveSpace() {
#ifdef DEBUG
  for (int i = NEW_SPACE; i < kNumberOfSpaces; ++i) {
    CHECK(reservations_[i].length() > 0);
  }
#endif  // DEBUG
  if (!isolate_->heap()->ReserveSpace(reservations_)) return false;
  for (int i = 0; i < kNumberOfPreallocatedSpaces; i++) {
    high_water_[i] = reservations_[i][0].start;
  }
  return true;
}


void Deserializer::Initialize(Isolate* isolate) {
  DCHECK_NULL(isolate_);
  DCHECK_NOT_NULL(isolate);
  isolate_ = isolate;
  DCHECK_NULL(external_reference_table_);
  external_reference_table_ = ExternalReferenceTable::instance(isolate);
  CHECK_EQ(magic_number_,
           SerializedData::ComputeMagicNumber(external_reference_table_));
}


void Deserializer::Deserialize(Isolate* isolate) {
  Initialize(isolate);
  if (!ReserveSpace()) V8::FatalProcessOutOfMemory("deserializing context");
  // No active threads.
  DCHECK_NULL(isolate_->thread_manager()->FirstThreadStateInUse());
  // No active handles.
  DCHECK(isolate_->handle_scope_implementer()->blocks()->is_empty());
  isolate_->heap()->IterateSmiRoots(this);
  isolate_->heap()->IterateStrongRoots(this, VISIT_ONLY_STRONG);
  isolate_->heap()->RepairFreeListsAfterDeserialization();
  isolate_->heap()->IterateWeakRoots(this, VISIT_ALL);

  isolate_->heap()->set_native_contexts_list(
      isolate_->heap()->undefined_value());
  isolate_->heap()->set_array_buffers_list(
      isolate_->heap()->undefined_value());
  isolate->heap()->set_new_array_buffer_views_list(
      isolate_->heap()->undefined_value());

  // The allocation site list is build during root iteration, but if no sites
  // were encountered then it needs to be initialized to undefined.
  if (isolate_->heap()->allocation_sites_list() == Smi::FromInt(0)) {
    isolate_->heap()->set_allocation_sites_list(
        isolate_->heap()->undefined_value());
  }

  // Update data pointers to the external strings containing natives sources.
  for (int i = 0; i < Natives::GetBuiltinsCount(); i++) {
    Object* source = isolate_->heap()->natives_source_cache()->get(i);
    if (!source->IsUndefined()) {
      ExternalOneByteString::cast(source)->update_data_cache();
    }
  }

  FlushICacheForNewCodeObjects();

  // Issue code events for newly deserialized code objects.
  LOG_CODE_EVENT(isolate_, LogCodeObjects());
  LOG_CODE_EVENT(isolate_, LogCompiledFunctions());
}


MaybeHandle<Object> Deserializer::DeserializePartial(
    Isolate* isolate, Handle<JSGlobalProxy> global_proxy,
    Handle<FixedArray>* outdated_contexts_out) {
  Initialize(isolate);
  if (!ReserveSpace()) {
    V8::FatalProcessOutOfMemory("deserialize context");
    return MaybeHandle<Object>();
  }

  Vector<Handle<Object> > attached_objects = Vector<Handle<Object> >::New(1);
  attached_objects[kGlobalProxyReference] = global_proxy;
  SetAttachedObjects(attached_objects);

  DisallowHeapAllocation no_gc;
  // Keep track of the code space start and end pointers in case new
  // code objects were unserialized
  OldSpace* code_space = isolate_->heap()->code_space();
  Address start_address = code_space->top();
  Object* root;
  Object* outdated_contexts;
  VisitPointer(&root);
  VisitPointer(&outdated_contexts);

  // There's no code deserialized here. If this assert fires
  // then that's changed and logging should be added to notify
  // the profiler et al of the new code.
  CHECK_EQ(start_address, code_space->top());
  CHECK(outdated_contexts->IsFixedArray());
  *outdated_contexts_out =
      Handle<FixedArray>(FixedArray::cast(outdated_contexts), isolate);
  return Handle<Object>(root, isolate);
}


MaybeHandle<SharedFunctionInfo> Deserializer::DeserializeCode(
    Isolate* isolate) {
  Initialize(isolate);
  if (!ReserveSpace()) {
    return Handle<SharedFunctionInfo>();
  } else {
    deserializing_user_code_ = true;
    DisallowHeapAllocation no_gc;
    Object* root;
    VisitPointer(&root);
    return Handle<SharedFunctionInfo>(SharedFunctionInfo::cast(root));
  }
}


Deserializer::~Deserializer() {
  // TODO(svenpanne) Re-enable this assertion when v8 initialization is fixed.
  // DCHECK(source_.AtEOF());
  attached_objects_.Dispose();
}


// This is called on the roots.  It is the driver of the deserialization
// process.  It is also called on the body of each function.
void Deserializer::VisitPointers(Object** start, Object** end) {
  // The space must be new space.  Any other space would cause ReadChunk to try
  // to update the remembered using NULL as the address.
  ReadData(start, end, NEW_SPACE, NULL);
}


void Deserializer::RelinkAllocationSite(AllocationSite* site) {
  if (isolate_->heap()->allocation_sites_list() == Smi::FromInt(0)) {
    site->set_weak_next(isolate_->heap()->undefined_value());
  } else {
    site->set_weak_next(isolate_->heap()->allocation_sites_list());
  }
  isolate_->heap()->set_allocation_sites_list(site);
}


// Used to insert a deserialized internalized string into the string table.
class StringTableInsertionKey : public HashTableKey {
 public:
  explicit StringTableInsertionKey(String* string)
      : string_(string), hash_(HashForObject(string)) {
    DCHECK(string->IsInternalizedString());
  }

  bool IsMatch(Object* string) OVERRIDE {
    // We know that all entries in a hash table had their hash keys created.
    // Use that knowledge to have fast failure.
    if (hash_ != HashForObject(string)) return false;
    // We want to compare the content of two internalized strings here.
    return string_->SlowEquals(String::cast(string));
  }

  uint32_t Hash() OVERRIDE { return hash_; }

  uint32_t HashForObject(Object* key) OVERRIDE {
    return String::cast(key)->Hash();
  }

  MUST_USE_RESULT virtual Handle<Object> AsHandle(Isolate* isolate)
      OVERRIDE {
    return handle(string_, isolate);
  }

  String* string_;
  uint32_t hash_;
};


HeapObject* Deserializer::ProcessNewObjectFromSerializedCode(HeapObject* obj) {
  if (obj->IsString()) {
    String* string = String::cast(obj);
    // Uninitialize hash field as the hash seed may have changed.
    string->set_hash_field(String::kEmptyHashField);
    if (string->IsInternalizedString()) {
      DisallowHeapAllocation no_gc;
      HandleScope scope(isolate_);
      StringTableInsertionKey key(string);
      String* canonical = *StringTable::LookupKey(isolate_, &key);
      string->SetForwardedInternalizedString(canonical);
      return canonical;
    }
  }
  return obj;
}


HeapObject* Deserializer::GetBackReferencedObject(int space) {
  HeapObject* obj;
  BackReference back_reference(source_.GetInt());
  if (space == LO_SPACE) {
    CHECK(back_reference.chunk_index() == 0);
    uint32_t index = back_reference.large_object_index();
    obj = deserialized_large_objects_[index];
  } else {
    DCHECK(space < kNumberOfPreallocatedSpaces);
    uint32_t chunk_index = back_reference.chunk_index();
    DCHECK_LE(chunk_index, current_chunk_[space]);
    uint32_t chunk_offset = back_reference.chunk_offset();
    obj = HeapObject::FromAddress(reservations_[space][chunk_index].start +
                                  chunk_offset);
  }
  if (deserializing_user_code() && obj->IsInternalizedString()) {
    obj = String::cast(obj)->GetForwardedInternalizedString();
  }
  hot_objects_.Add(obj);
  return obj;
}


// This routine writes the new object into the pointer provided and then
// returns true if the new object was in young space and false otherwise.
// The reason for this strange interface is that otherwise the object is
// written very late, which means the FreeSpace map is not set up by the
// time we need to use it to mark the space at the end of a page free.
void Deserializer::ReadObject(int space_number, Object** write_back) {
  Address address;
  HeapObject* obj;
  int next_int = source_.GetInt();

  bool double_align = false;
#ifndef V8_HOST_ARCH_64_BIT
  double_align = next_int == kDoubleAlignmentSentinel;
  if (double_align) next_int = source_.GetInt();
#endif

  DCHECK_NE(kDoubleAlignmentSentinel, next_int);
  int size = next_int << kObjectAlignmentBits;
  int reserved_size = size + (double_align ? kPointerSize : 0);
  address = Allocate(space_number, reserved_size);
  obj = HeapObject::FromAddress(address);
  if (double_align) {
    obj = isolate_->heap()->DoubleAlignForDeserialization(obj, reserved_size);
    address = obj->address();
  }

  isolate_->heap()->OnAllocationEvent(obj, size);
  Object** current = reinterpret_cast<Object**>(address);
  Object** limit = current + (size >> kPointerSizeLog2);
  if (FLAG_log_snapshot_positions) {
    LOG(isolate_, SnapshotPositionEvent(address, source_.position()));
  }
  ReadData(current, limit, space_number, address);

  // TODO(mvstanton): consider treating the heap()->allocation_sites_list()
  // as a (weak) root. If this root is relocated correctly,
  // RelinkAllocationSite() isn't necessary.
  if (obj->IsAllocationSite()) RelinkAllocationSite(AllocationSite::cast(obj));

  // Fix up strings from serialized user code.
  if (deserializing_user_code()) obj = ProcessNewObjectFromSerializedCode(obj);

  Object* write_back_obj = obj;
  UnalignedCopy(write_back, &write_back_obj);
#ifdef DEBUG
  if (obj->IsCode()) {
    DCHECK(space_number == CODE_SPACE || space_number == LO_SPACE);
  } else {
    DCHECK(space_number != CODE_SPACE);
  }
#endif

  if (obj->IsCode()) {
    // Turn internal references encoded as offsets back to absolute addresses.
    Code* code = Code::cast(obj);
    Address entry = code->entry();
    int mode_mask = RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
                    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE_ENCODED);
    for (RelocIterator it(code, mode_mask); !it.done(); it.next()) {
      RelocInfo* rinfo = it.rinfo();
      intptr_t offset =
          reinterpret_cast<intptr_t>(rinfo->target_internal_reference());
      DCHECK(0 <= offset && offset <= code->instruction_size());
      rinfo->set_target_internal_reference(entry + offset);
    }
  }
}


// We know the space requirements before deserialization and can
// pre-allocate that reserved space. During deserialization, all we need
// to do is to bump up the pointer for each space in the reserved
// space. This is also used for fixing back references.
// We may have to split up the pre-allocation into several chunks
// because it would not fit onto a single page. We do not have to keep
// track of when to move to the next chunk. An opcode will signal this.
// Since multiple large objects cannot be folded into one large object
// space allocation, we have to do an actual allocation when deserializing
// each large object. Instead of tracking offset for back references, we
// reference large objects by index.
Address Deserializer::Allocate(int space_index, int size) {
  if (space_index == LO_SPACE) {
    AlwaysAllocateScope scope(isolate_);
    LargeObjectSpace* lo_space = isolate_->heap()->lo_space();
    Executability exec = static_cast<Executability>(source_.Get());
    AllocationResult result = lo_space->AllocateRaw(size, exec);
    HeapObject* obj = HeapObject::cast(result.ToObjectChecked());
    deserialized_large_objects_.Add(obj);
    return obj->address();
  } else {
    DCHECK(space_index < kNumberOfPreallocatedSpaces);
    Address address = high_water_[space_index];
    DCHECK_NOT_NULL(address);
    high_water_[space_index] += size;
#ifdef DEBUG
    // Assert that the current reserved chunk is still big enough.
    const Heap::Reservation& reservation = reservations_[space_index];
    int chunk_index = current_chunk_[space_index];
    CHECK_LE(high_water_[space_index], reservation[chunk_index].end);
#endif
    return address;
  }
}


void Deserializer::ReadData(Object** current, Object** limit, int source_space,
                            Address current_object_address) {
  Isolate* const isolate = isolate_;
  // Write barrier support costs around 1% in startup time.  In fact there
  // are no new space objects in current boot snapshots, so it's not needed,
  // but that may change.
  bool write_barrier_needed = (current_object_address != NULL &&
                               source_space != NEW_SPACE &&
                               source_space != CELL_SPACE &&
                               source_space != PROPERTY_CELL_SPACE &&
                               source_space != CODE_SPACE &&
                               source_space != OLD_DATA_SPACE);
  while (current < limit) {
    byte data = source_.Get();
    switch (data) {
#define CASE_STATEMENT(where, how, within, space_number) \
  case where + how + within + space_number:              \
    STATIC_ASSERT((where & ~kPointedToMask) == 0);       \
    STATIC_ASSERT((how & ~kHowToCodeMask) == 0);         \
    STATIC_ASSERT((within & ~kWhereToPointMask) == 0);   \
    STATIC_ASSERT((space_number & ~kSpaceMask) == 0);

#define CASE_BODY(where, how, within, space_number_if_any)                     \
  {                                                                            \
    bool emit_write_barrier = false;                                           \
    bool current_was_incremented = false;                                      \
    int space_number = space_number_if_any == kAnyOldSpace                     \
                           ? (data & kSpaceMask)                               \
                           : space_number_if_any;                              \
    if (where == kNewObject && how == kPlain && within == kStartOfObject) {    \
      ReadObject(space_number, current);                                       \
      emit_write_barrier = (space_number == NEW_SPACE);                        \
    } else {                                                                   \
      Object* new_object = NULL; /* May not be a real Object pointer. */       \
      if (where == kNewObject) {                                               \
        ReadObject(space_number, &new_object);                                 \
      } else if (where == kRootArray) {                                        \
        int root_id = source_.GetInt();                                        \
        new_object = isolate->heap()->roots_array_start()[root_id];            \
        emit_write_barrier = isolate->heap()->InNewSpace(new_object);          \
      } else if (where == kPartialSnapshotCache) {                             \
        int cache_index = source_.GetInt();                                    \
        new_object = isolate->partial_snapshot_cache()->at(cache_index);       \
        emit_write_barrier = isolate->heap()->InNewSpace(new_object);          \
      } else if (where == kExternalReference) {                                \
        int skip = source_.GetInt();                                           \
        current = reinterpret_cast<Object**>(                                  \
            reinterpret_cast<Address>(current) + skip);                        \
        int reference_id = source_.GetInt();                                   \
        Address address = external_reference_table_->address(reference_id);    \
        new_object = reinterpret_cast<Object*>(address);                       \
      } else if (where == kBackref) {                                          \
        emit_write_barrier = (space_number == NEW_SPACE);                      \
        new_object = GetBackReferencedObject(data & kSpaceMask);               \
      } else if (where == kBuiltin) {                                          \
        DCHECK(deserializing_user_code());                                     \
        int builtin_id = source_.GetInt();                                     \
        DCHECK_LE(0, builtin_id);                                              \
        DCHECK_LT(builtin_id, Builtins::builtin_count);                        \
        Builtins::Name name = static_cast<Builtins::Name>(builtin_id);         \
        new_object = isolate->builtins()->builtin(name);                       \
        emit_write_barrier = false;                                            \
      } else if (where == kAttachedReference) {                                \
        int index = source_.GetInt();                                          \
        DCHECK(deserializing_user_code() || index == kGlobalProxyReference);   \
        new_object = *attached_objects_[index];                                \
        emit_write_barrier = isolate->heap()->InNewSpace(new_object);          \
      } else {                                                                 \
        DCHECK(where == kBackrefWithSkip);                                     \
        int skip = source_.GetInt();                                           \
        current = reinterpret_cast<Object**>(                                  \
            reinterpret_cast<Address>(current) + skip);                        \
        emit_write_barrier = (space_number == NEW_SPACE);                      \
        new_object = GetBackReferencedObject(data & kSpaceMask);               \
      }                                                                        \
      if (within == kInnerPointer) {                                           \
        if (space_number != CODE_SPACE || new_object->IsCode()) {              \
          Code* new_code_object = reinterpret_cast<Code*>(new_object);         \
          new_object =                                                         \
              reinterpret_cast<Object*>(new_code_object->instruction_start()); \
        } else {                                                               \
          DCHECK(space_number == CODE_SPACE);                                  \
          Cell* cell = Cell::cast(new_object);                                 \
          new_object = reinterpret_cast<Object*>(cell->ValueAddress());        \
        }                                                                      \
      }                                                                        \
      if (how == kFromCode) {                                                  \
        Address location_of_branch_data = reinterpret_cast<Address>(current);  \
        Assembler::deserialization_set_special_target_at(                      \
            location_of_branch_data,                                           \
            Code::cast(HeapObject::FromAddress(current_object_address)),       \
            reinterpret_cast<Address>(new_object));                            \
        location_of_branch_data += Assembler::kSpecialTargetSize;              \
        current = reinterpret_cast<Object**>(location_of_branch_data);         \
        current_was_incremented = true;                                        \
      } else {                                                                 \
        UnalignedCopy(current, &new_object);                                   \
      }                                                                        \
    }                                                                          \
    if (emit_write_barrier && write_barrier_needed) {                          \
      Address current_address = reinterpret_cast<Address>(current);            \
      isolate->heap()->RecordWrite(                                            \
          current_object_address,                                              \
          static_cast<int>(current_address - current_object_address));         \
    }                                                                          \
    if (!current_was_incremented) {                                            \
      current++;                                                               \
    }                                                                          \
    break;                                                                     \
  }

// This generates a case and a body for the new space (which has to do extra
// write barrier handling) and handles the other spaces with 8 fall-through
// cases and one body.
#define ALL_SPACES(where, how, within)                    \
  CASE_STATEMENT(where, how, within, NEW_SPACE)           \
  CASE_BODY(where, how, within, NEW_SPACE)                \
  CASE_STATEMENT(where, how, within, OLD_DATA_SPACE)      \
  CASE_STATEMENT(where, how, within, OLD_POINTER_SPACE)   \
  CASE_STATEMENT(where, how, within, CODE_SPACE)          \
  CASE_STATEMENT(where, how, within, MAP_SPACE)           \
  CASE_STATEMENT(where, how, within, CELL_SPACE)          \
  CASE_STATEMENT(where, how, within, PROPERTY_CELL_SPACE) \
  CASE_STATEMENT(where, how, within, LO_SPACE)            \
  CASE_BODY(where, how, within, kAnyOldSpace)

#define FOUR_CASES(byte_code)             \
  case byte_code:                         \
  case byte_code + 1:                     \
  case byte_code + 2:                     \
  case byte_code + 3:

#define SIXTEEN_CASES(byte_code)          \
  FOUR_CASES(byte_code)                   \
  FOUR_CASES(byte_code + 4)               \
  FOUR_CASES(byte_code + 8)               \
  FOUR_CASES(byte_code + 12)

#define COMMON_RAW_LENGTHS(f)        \
  f(1)  \
  f(2)  \
  f(3)  \
  f(4)  \
  f(5)  \
  f(6)  \
  f(7)  \
  f(8)  \
  f(9)  \
  f(10) \
  f(11) \
  f(12) \
  f(13) \
  f(14) \
  f(15) \
  f(16) \
  f(17) \
  f(18) \
  f(19) \
  f(20) \
  f(21) \
  f(22) \
  f(23) \
  f(24) \
  f(25) \
  f(26) \
  f(27) \
  f(28) \
  f(29) \
  f(30) \
  f(31)

      // We generate 15 cases and bodies that process special tags that combine
      // the raw data tag and the length into one byte.
#define RAW_CASE(index)                                                        \
  case kRawData + index: {                                                     \
    byte* raw_data_out = reinterpret_cast<byte*>(current);                     \
    source_.CopyRaw(raw_data_out, index* kPointerSize);                        \
    current = reinterpret_cast<Object**>(raw_data_out + index * kPointerSize); \
    break;                                                                     \
  }
      COMMON_RAW_LENGTHS(RAW_CASE)
#undef RAW_CASE

      // Deserialize a chunk of raw data that doesn't have one of the popular
      // lengths.
      case kRawData: {
        int size = source_.GetInt();
        byte* raw_data_out = reinterpret_cast<byte*>(current);
        source_.CopyRaw(raw_data_out, size);
        break;
      }

      SIXTEEN_CASES(kRootArrayConstants + kNoSkipDistance)
      SIXTEEN_CASES(kRootArrayConstants + kNoSkipDistance + 16) {
        int root_id = RootArrayConstantFromByteCode(data);
        Object* object = isolate->heap()->roots_array_start()[root_id];
        DCHECK(!isolate->heap()->InNewSpace(object));
        UnalignedCopy(current++, &object);
        break;
      }

      SIXTEEN_CASES(kRootArrayConstants + kHasSkipDistance)
      SIXTEEN_CASES(kRootArrayConstants + kHasSkipDistance + 16) {
        int root_id = RootArrayConstantFromByteCode(data);
        int skip = source_.GetInt();
        current = reinterpret_cast<Object**>(
            reinterpret_cast<intptr_t>(current) + skip);
        Object* object = isolate->heap()->roots_array_start()[root_id];
        DCHECK(!isolate->heap()->InNewSpace(object));
        UnalignedCopy(current++, &object);
        break;
      }

      case kVariableRepeat: {
        int repeats = source_.GetInt();
        Object* object = current[-1];
        DCHECK(!isolate->heap()->InNewSpace(object));
        for (int i = 0; i < repeats; i++) UnalignedCopy(current++, &object);
        break;
      }

      STATIC_ASSERT(kRootArrayNumberOfConstantEncodings ==
                    Heap::kOldSpaceRoots);
      STATIC_ASSERT(kMaxFixedRepeats == 15);
      FOUR_CASES(kFixedRepeat)
      FOUR_CASES(kFixedRepeat + 4)
      FOUR_CASES(kFixedRepeat + 8)
      case kFixedRepeat + 12:
      case kFixedRepeat + 13:
      case kFixedRepeat + 14: {
        int repeats = RepeatsForCode(data);
        Object* object;
        UnalignedCopy(&object, current - 1);
        DCHECK(!isolate->heap()->InNewSpace(object));
        for (int i = 0; i < repeats; i++) UnalignedCopy(current++, &object);
        break;
      }

      // Deserialize a new object and write a pointer to it to the current
      // object.
      ALL_SPACES(kNewObject, kPlain, kStartOfObject)
      // Support for direct instruction pointers in functions.  It's an inner
      // pointer because it points at the entry point, not at the start of the
      // code object.
      CASE_STATEMENT(kNewObject, kPlain, kInnerPointer, CODE_SPACE)
      CASE_BODY(kNewObject, kPlain, kInnerPointer, CODE_SPACE)
      // Deserialize a new code object and write a pointer to its first
      // instruction to the current code object.
      ALL_SPACES(kNewObject, kFromCode, kInnerPointer)
      // Find a recently deserialized object using its offset from the current
      // allocation point and write a pointer to it to the current object.
      ALL_SPACES(kBackref, kPlain, kStartOfObject)
      ALL_SPACES(kBackrefWithSkip, kPlain, kStartOfObject)
#if defined(V8_TARGET_ARCH_MIPS) || defined(V8_TARGET_ARCH_MIPS64) || \
    defined(V8_TARGET_ARCH_PPC) || V8_OOL_CONSTANT_POOL
      // Deserialize a new object from pointer found in code and write
      // a pointer to it to the current object. Required only for MIPS, PPC or
      // ARM with ool constant pool, and omitted on the other architectures
      // because it is fully unrolled and would cause bloat.
      ALL_SPACES(kNewObject, kFromCode, kStartOfObject)
      // Find a recently deserialized code object using its offset from the
      // current allocation point and write a pointer to it to the current
      // object. Required only for MIPS, PPC or ARM with ool constant pool.
      ALL_SPACES(kBackref, kFromCode, kStartOfObject)
      ALL_SPACES(kBackrefWithSkip, kFromCode, kStartOfObject)
#endif
      // Find a recently deserialized code object using its offset from the
      // current allocation point and write a pointer to its first instruction
      // to the current code object or the instruction pointer in a function
      // object.
      ALL_SPACES(kBackref, kFromCode, kInnerPointer)
      ALL_SPACES(kBackrefWithSkip, kFromCode, kInnerPointer)
      ALL_SPACES(kBackref, kPlain, kInnerPointer)
      ALL_SPACES(kBackrefWithSkip, kPlain, kInnerPointer)
      // Find an object in the roots array and write a pointer to it to the
      // current object.
      CASE_STATEMENT(kRootArray, kPlain, kStartOfObject, 0)
      CASE_BODY(kRootArray, kPlain, kStartOfObject, 0)
#if defined(V8_TARGET_ARCH_MIPS) || V8_OOL_CONSTANT_POOL || \
    defined(V8_TARGET_ARCH_MIPS64) || defined(V8_TARGET_ARCH_PPC)
      // Find an object in the roots array and write a pointer to it to in code.
      CASE_STATEMENT(kRootArray, kFromCode, kStartOfObject, 0)
      CASE_BODY(kRootArray, kFromCode, kStartOfObject, 0)
#endif
      // Find an object in the partial snapshots cache and write a pointer to it
      // to the current object.
      CASE_STATEMENT(kPartialSnapshotCache, kPlain, kStartOfObject, 0)
      CASE_BODY(kPartialSnapshotCache,
                kPlain,
                kStartOfObject,
                0)
      // Find an code entry in the partial snapshots cache and
      // write a pointer to it to the current object.
      CASE_STATEMENT(kPartialSnapshotCache, kPlain, kInnerPointer, 0)
      CASE_BODY(kPartialSnapshotCache,
                kPlain,
                kInnerPointer,
                0)
      // Find an external reference and write a pointer to it to the current
      // object.
      CASE_STATEMENT(kExternalReference, kPlain, kStartOfObject, 0)
      CASE_BODY(kExternalReference,
                kPlain,
                kStartOfObject,
                0)
      // Find an external reference and write a pointer to it in the current
      // code object.
      CASE_STATEMENT(kExternalReference, kFromCode, kStartOfObject, 0)
      CASE_BODY(kExternalReference,
                kFromCode,
                kStartOfObject,
                0)
      // Find a builtin and write a pointer to it to the current object.
      CASE_STATEMENT(kBuiltin, kPlain, kStartOfObject, 0)
      CASE_BODY(kBuiltin, kPlain, kStartOfObject, 0)
      CASE_STATEMENT(kBuiltin, kPlain, kInnerPointer, 0)
      CASE_BODY(kBuiltin, kPlain, kInnerPointer, 0)
      CASE_STATEMENT(kBuiltin, kFromCode, kInnerPointer, 0)
      CASE_BODY(kBuiltin, kFromCode, kInnerPointer, 0)
      // Find an object in the attached references and write a pointer to it to
      // the current object.
      CASE_STATEMENT(kAttachedReference, kPlain, kStartOfObject, 0)
      CASE_BODY(kAttachedReference, kPlain, kStartOfObject, 0)
      CASE_STATEMENT(kAttachedReference, kPlain, kInnerPointer, 0)
      CASE_BODY(kAttachedReference, kPlain, kInnerPointer, 0)
      CASE_STATEMENT(kAttachedReference, kFromCode, kInnerPointer, 0)
      CASE_BODY(kAttachedReference, kFromCode, kInnerPointer, 0)

#undef CASE_STATEMENT
#undef CASE_BODY
#undef ALL_SPACES

      case kSkip: {
        int size = source_.GetInt();
        current = reinterpret_cast<Object**>(
            reinterpret_cast<intptr_t>(current) + size);
        break;
      }

      case kNativesStringResource: {
        DCHECK(!isolate_->heap()->deserialization_complete());
        int index = source_.Get();
        Vector<const char> source_vector = Natives::GetScriptSource(index);
        NativesExternalStringResource* resource =
            new NativesExternalStringResource(source_vector.start(),
                                              source_vector.length());
        Object* resource_obj = reinterpret_cast<Object*>(resource);
        UnalignedCopy(current++, &resource_obj);
        break;
      }

      case kNextChunk: {
        int space = source_.Get();
        DCHECK(space < kNumberOfPreallocatedSpaces);
        int chunk_index = current_chunk_[space];
        const Heap::Reservation& reservation = reservations_[space];
        // Make sure the current chunk is indeed exhausted.
        CHECK_EQ(reservation[chunk_index].end, high_water_[space]);
        // Move to next reserved chunk.
        chunk_index = ++current_chunk_[space];
        CHECK_LT(chunk_index, reservation.length());
        high_water_[space] = reservation[chunk_index].start;
        break;
      }

      FOUR_CASES(kHotObjectWithSkip)
      FOUR_CASES(kHotObjectWithSkip + 4) {
        int skip = source_.GetInt();
        current = reinterpret_cast<Object**>(
            reinterpret_cast<Address>(current) + skip);
        // Fall through.
      }
      FOUR_CASES(kHotObject)
      FOUR_CASES(kHotObject + 4) {
        int index = data & kHotObjectIndexMask;
        Object* hot_object = hot_objects_.Get(index);
        UnalignedCopy(current, &hot_object);
        if (write_barrier_needed && isolate->heap()->InNewSpace(hot_object)) {
          Address current_address = reinterpret_cast<Address>(current);
          isolate->heap()->RecordWrite(
              current_object_address,
              static_cast<int>(current_address - current_object_address));
        }
        current++;
        break;
      }

      case kSynchronize: {
        // If we get here then that indicates that you have a mismatch between
        // the number of GC roots when serializing and deserializing.
        CHECK(false);
      }

      default:
        CHECK(false);
    }
  }
  CHECK_EQ(limit, current);
}


Serializer::Serializer(Isolate* isolate, SnapshotByteSink* sink)
    : isolate_(isolate),
      sink_(sink),
      external_reference_encoder_(new ExternalReferenceEncoder(isolate)),
      root_index_map_(isolate),
      code_address_map_(NULL),
      large_objects_total_size_(0),
      seen_large_objects_index_(0) {
  // The serializer is meant to be used only to generate initial heap images
  // from a context in which there is only one isolate.
  for (int i = 0; i < kNumberOfPreallocatedSpaces; i++) {
    pending_chunk_[i] = 0;
    max_chunk_size_[i] = static_cast<uint32_t>(
        MemoryAllocator::PageAreaSize(static_cast<AllocationSpace>(i)));
  }
}


Serializer::~Serializer() {
  delete external_reference_encoder_;
  if (code_address_map_ != NULL) delete code_address_map_;
}


void StartupSerializer::SerializeStrongReferences() {
  Isolate* isolate = this->isolate();
  // No active threads.
  CHECK_NULL(isolate->thread_manager()->FirstThreadStateInUse());
  // No active or weak handles.
  CHECK(isolate->handle_scope_implementer()->blocks()->is_empty());
  CHECK_EQ(0, isolate->global_handles()->NumberOfWeakHandles());
  CHECK_EQ(0, isolate->eternal_handles()->NumberOfHandles());
  // We don't support serializing installed extensions.
  CHECK(!isolate->has_installed_extensions());
  isolate->heap()->IterateSmiRoots(this);
  isolate->heap()->IterateStrongRoots(this, VISIT_ONLY_STRONG);
}


void StartupSerializer::VisitPointers(Object** start, Object** end) {
  for (Object** current = start; current < end; current++) {
    if (start == isolate()->heap()->roots_array_start()) {
      root_index_wave_front_ =
          Max(root_index_wave_front_, static_cast<intptr_t>(current - start));
    }
    if (ShouldBeSkipped(current)) {
      sink_->Put(kSkip, "Skip");
      sink_->PutInt(kPointerSize, "SkipOneWord");
    } else if ((*current)->IsSmi()) {
      sink_->Put(kOnePointerRawData, "Smi");
      for (int i = 0; i < kPointerSize; i++) {
        sink_->Put(reinterpret_cast<byte*>(current)[i], "Byte");
      }
    } else {
      SerializeObject(HeapObject::cast(*current), kPlain, kStartOfObject, 0);
    }
  }
}


void PartialSerializer::Serialize(Object** o) {
  if ((*o)->IsContext()) {
    Context* context = Context::cast(*o);
    global_object_ = context->global_object();
    back_reference_map()->AddGlobalProxy(context->global_proxy());
  }
  VisitPointer(o);
  SerializeOutdatedContextsAsFixedArray();
  Pad();
}


void PartialSerializer::SerializeOutdatedContextsAsFixedArray() {
  int length = outdated_contexts_.length();
  if (length == 0) {
    FixedArray* empty = isolate_->heap()->empty_fixed_array();
    SerializeObject(empty, kPlain, kStartOfObject, 0);
  } else {
    // Serialize an imaginary fixed array containing outdated contexts.
    int size = FixedArray::SizeFor(length);
    Allocate(NEW_SPACE, size);
    sink_->Put(kNewObject + NEW_SPACE, "emulated FixedArray");
    sink_->PutInt(size >> kObjectAlignmentBits, "FixedArray size in words");
    Map* map = isolate_->heap()->fixed_array_map();
    SerializeObject(map, kPlain, kStartOfObject, 0);
    Smi* length_smi = Smi::FromInt(length);
    sink_->Put(kOnePointerRawData, "Smi");
    for (int i = 0; i < kPointerSize; i++) {
      sink_->Put(reinterpret_cast<byte*>(&length_smi)[i], "Byte");
    }
    for (int i = 0; i < length; i++) {
      BackReference back_ref = outdated_contexts_[i];
      DCHECK(BackReferenceIsAlreadyAllocated(back_ref));
      sink_->Put(kBackref + back_ref.space(), "BackRef");
      sink_->PutInt(back_ref.reference(), "BackRefValue");
    }
  }
}


bool Serializer::ShouldBeSkipped(Object** current) {
  Object** roots = isolate()->heap()->roots_array_start();
  return current == &roots[Heap::kStoreBufferTopRootIndex]
      || current == &roots[Heap::kStackLimitRootIndex]
      || current == &roots[Heap::kRealStackLimitRootIndex];
}


void Serializer::VisitPointers(Object** start, Object** end) {
  for (Object** current = start; current < end; current++) {
    if ((*current)->IsSmi()) {
      sink_->Put(kOnePointerRawData, "Smi");
      for (int i = 0; i < kPointerSize; i++) {
        sink_->Put(reinterpret_cast<byte*>(current)[i], "Byte");
      }
    } else {
      SerializeObject(HeapObject::cast(*current), kPlain, kStartOfObject, 0);
    }
  }
}


void Serializer::EncodeReservations(
    List<SerializedData::Reservation>* out) const {
  for (int i = 0; i < kNumberOfPreallocatedSpaces; i++) {
    for (int j = 0; j < completed_chunks_[i].length(); j++) {
      out->Add(SerializedData::Reservation(completed_chunks_[i][j]));
    }

    if (pending_chunk_[i] > 0 || completed_chunks_[i].length() == 0) {
      out->Add(SerializedData::Reservation(pending_chunk_[i]));
    }
    out->last().mark_as_last();
  }

  out->Add(SerializedData::Reservation(large_objects_total_size_));
  out->last().mark_as_last();
}


// This ensures that the partial snapshot cache keeps things alive during GC and
// tracks their movement.  When it is called during serialization of the startup
// snapshot nothing happens.  When the partial (context) snapshot is created,
// this array is populated with the pointers that the partial snapshot will
// need. As that happens we emit serialized objects to the startup snapshot
// that correspond to the elements of this cache array.  On deserialization we
// therefore need to visit the cache array.  This fills it up with pointers to
// deserialized objects.
void SerializerDeserializer::Iterate(Isolate* isolate,
                                     ObjectVisitor* visitor) {
  if (isolate->serializer_enabled()) return;
  List<Object*>* cache = isolate->partial_snapshot_cache();
  for (int i = 0;; ++i) {
    // Extend the array ready to get a value when deserializing.
    if (cache->length() <= i) cache->Add(Smi::FromInt(0));
    visitor->VisitPointer(&cache->at(i));
    // Sentinel is the undefined object, which is a root so it will not normally
    // be found in the cache.
    if (cache->at(i)->IsUndefined()) break;
  }
}


int PartialSerializer::PartialSnapshotCacheIndex(HeapObject* heap_object) {
  Isolate* isolate = this->isolate();
  List<Object*>* cache = isolate->partial_snapshot_cache();
  int new_index = cache->length();

  int index = partial_cache_index_map_.LookupOrInsert(heap_object, new_index);
  if (index == PartialCacheIndexMap::kInvalidIndex) {
    // We didn't find the object in the cache.  So we add it to the cache and
    // then visit the pointer so that it becomes part of the startup snapshot
    // and we can refer to it from the partial snapshot.
    cache->Add(heap_object);
    startup_serializer_->VisitPointer(reinterpret_cast<Object**>(&heap_object));
    // We don't recurse from the startup snapshot generator into the partial
    // snapshot generator.
    return new_index;
  }
  return index;
}


#ifdef DEBUG
bool Serializer::BackReferenceIsAlreadyAllocated(BackReference reference) {
  DCHECK(reference.is_valid());
  DCHECK(!reference.is_source());
  DCHECK(!reference.is_global_proxy());
  AllocationSpace space = reference.space();
  int chunk_index = reference.chunk_index();
  if (space == LO_SPACE) {
    return chunk_index == 0 &&
           reference.large_object_index() < seen_large_objects_index_;
  } else if (chunk_index == completed_chunks_[space].length()) {
    return reference.chunk_offset() < pending_chunk_[space];
  } else {
    return chunk_index < completed_chunks_[space].length() &&
           reference.chunk_offset() < completed_chunks_[space][chunk_index];
  }
}
#endif  // DEBUG


bool Serializer::SerializeKnownObject(HeapObject* obj, HowToCode how_to_code,
                                      WhereToPoint where_to_point, int skip) {
  if (how_to_code == kPlain && where_to_point == kStartOfObject) {
    // Encode a reference to a hot object by its index in the working set.
    int index = hot_objects_.Find(obj);
    if (index != HotObjectsList::kNotFound) {
      DCHECK(index >= 0 && index <= kMaxHotObjectIndex);
      if (FLAG_trace_serializer) {
        PrintF(" Encoding hot object %d:", index);
        obj->ShortPrint();
        PrintF("\n");
      }
      if (skip != 0) {
        sink_->Put(kHotObjectWithSkip + index, "HotObjectWithSkip");
        sink_->PutInt(skip, "HotObjectSkipDistance");
      } else {
        sink_->Put(kHotObject + index, "HotObject");
      }
      return true;
    }
  }
  BackReference back_reference = back_reference_map_.Lookup(obj);
  if (back_reference.is_valid()) {
    // Encode the location of an already deserialized object in order to write
    // its location into a later object.  We can encode the location as an
    // offset fromthe start of the deserialized objects or as an offset
    // backwards from thecurrent allocation pointer.
    if (back_reference.is_source()) {
      FlushSkip(skip);
      if (FLAG_trace_serializer) PrintF(" Encoding source object\n");
      DCHECK(how_to_code == kPlain && where_to_point == kStartOfObject);
      sink_->Put(kAttachedReference + kPlain + kStartOfObject, "Source");
      sink_->PutInt(kSourceObjectReference, "kSourceObjectReference");
    } else if (back_reference.is_global_proxy()) {
      FlushSkip(skip);
      if (FLAG_trace_serializer) PrintF(" Encoding global proxy\n");
      DCHECK(how_to_code == kPlain && where_to_point == kStartOfObject);
      sink_->Put(kAttachedReference + kPlain + kStartOfObject, "Global Proxy");
      sink_->PutInt(kGlobalProxyReference, "kGlobalProxyReference");
    } else {
      if (FLAG_trace_serializer) {
        PrintF(" Encoding back reference to: ");
        obj->ShortPrint();
        PrintF("\n");
      }

      AllocationSpace space = back_reference.space();
      if (skip == 0) {
        sink_->Put(kBackref + how_to_code + where_to_point + space, "BackRef");
      } else {
        sink_->Put(kBackrefWithSkip + how_to_code + where_to_point + space,
                   "BackRefWithSkip");
        sink_->PutInt(skip, "BackRefSkipDistance");
      }
      DCHECK(BackReferenceIsAlreadyAllocated(back_reference));
      sink_->PutInt(back_reference.reference(), "BackRefValue");

      hot_objects_.Add(obj);
    }
    return true;
  }
  return false;
}


void StartupSerializer::SerializeObject(HeapObject* obj, HowToCode how_to_code,
                                        WhereToPoint where_to_point, int skip) {
  DCHECK(!obj->IsJSFunction());

  int root_index = root_index_map_.Lookup(obj);
  // We can only encode roots as such if it has already been serialized.
  // That applies to root indices below the wave front.
  if (root_index != RootIndexMap::kInvalidRootIndex &&
      root_index < root_index_wave_front_) {
    PutRoot(root_index, obj, how_to_code, where_to_point, skip);
    return;
  }

  if (obj->IsCode() && Code::cast(obj)->kind() == Code::FUNCTION) {
    obj = isolate()->builtins()->builtin(Builtins::kCompileLazy);
  }

  if (SerializeKnownObject(obj, how_to_code, where_to_point, skip)) return;

  FlushSkip(skip);

  // Object has not yet been serialized.  Serialize it here.
  ObjectSerializer object_serializer(this, obj, sink_, how_to_code,
                                     where_to_point);
  object_serializer.Serialize();
}


void StartupSerializer::SerializeWeakReferences() {
  // This phase comes right after the serialization (of the snapshot).
  // After we have done the partial serialization the partial snapshot cache
  // will contain some references needed to decode the partial snapshot.  We
  // add one entry with 'undefined' which is the sentinel that the deserializer
  // uses to know it is done deserializing the array.
  Object* undefined = isolate()->heap()->undefined_value();
  VisitPointer(&undefined);
  isolate()->heap()->IterateWeakRoots(this, VISIT_ALL);
  Pad();
}


void Serializer::PutRoot(int root_index,
                         HeapObject* object,
                         SerializerDeserializer::HowToCode how_to_code,
                         SerializerDeserializer::WhereToPoint where_to_point,
                         int skip) {
  if (FLAG_trace_serializer) {
    PrintF(" Encoding root %d:", root_index);
    object->ShortPrint();
    PrintF("\n");
  }

  if (how_to_code == kPlain &&
      where_to_point == kStartOfObject &&
      root_index < kRootArrayNumberOfConstantEncodings &&
      !isolate()->heap()->InNewSpace(object)) {
    if (skip == 0) {
      sink_->Put(kRootArrayConstants + kNoSkipDistance + root_index,
                 "RootConstant");
    } else {
      sink_->Put(kRootArrayConstants + kHasSkipDistance + root_index,
                 "RootConstant");
      sink_->PutInt(skip, "SkipInPutRoot");
    }
  } else {
    FlushSkip(skip);
    sink_->Put(kRootArray + how_to_code + where_to_point, "RootSerialization");
    sink_->PutInt(root_index, "root_index");
  }
}


void PartialSerializer::SerializeObject(HeapObject* obj, HowToCode how_to_code,
                                        WhereToPoint where_to_point, int skip) {
  if (obj->IsMap()) {
    // The code-caches link to context-specific code objects, which
    // the startup and context serializes cannot currently handle.
    DCHECK(Map::cast(obj)->code_cache() == obj->GetHeap()->empty_fixed_array());
  }

  // Replace typed arrays by undefined.
  if (obj->IsJSTypedArray()) obj = isolate_->heap()->undefined_value();

  int root_index = root_index_map_.Lookup(obj);
  if (root_index != RootIndexMap::kInvalidRootIndex) {
    PutRoot(root_index, obj, how_to_code, where_to_point, skip);
    return;
  }

  if (ShouldBeInThePartialSnapshotCache(obj)) {
    FlushSkip(skip);

    int cache_index = PartialSnapshotCacheIndex(obj);
    sink_->Put(kPartialSnapshotCache + how_to_code + where_to_point,
               "PartialSnapshotCache");
    sink_->PutInt(cache_index, "partial_snapshot_cache_index");
    return;
  }

  // Pointers from the partial snapshot to the objects in the startup snapshot
  // should go through the root array or through the partial snapshot cache.
  // If this is not the case you may have to add something to the root array.
  DCHECK(!startup_serializer_->back_reference_map()->Lookup(obj).is_valid());
  // All the internalized strings that the partial snapshot needs should be
  // either in the root table or in the partial snapshot cache.
  DCHECK(!obj->IsInternalizedString());

  if (SerializeKnownObject(obj, how_to_code, where_to_point, skip)) return;

  FlushSkip(skip);

  // Object has not yet been serialized.  Serialize it here.
  ObjectSerializer serializer(this, obj, sink_, how_to_code, where_to_point);
  serializer.Serialize();

  if (obj->IsContext() &&
      Context::cast(obj)->global_object() == global_object_) {
    // Context refers to the current global object. This reference will
    // become outdated after deserialization.
    BackReference back_reference = back_reference_map_.Lookup(obj);
    DCHECK(back_reference.is_valid());
    outdated_contexts_.Add(back_reference);
  }
}


void Serializer::ObjectSerializer::SerializePrologue(AllocationSpace space,
                                                     int size, Map* map) {
  if (serializer_->code_address_map_) {
    const char* code_name =
        serializer_->code_address_map_->Lookup(object_->address());
    LOG(serializer_->isolate_,
        CodeNameEvent(object_->address(), sink_->Position(), code_name));
    LOG(serializer_->isolate_,
        SnapshotPositionEvent(object_->address(), sink_->Position()));
  }

  BackReference back_reference;
  if (space == LO_SPACE) {
    sink_->Put(kNewObject + reference_representation_ + space,
               "NewLargeObject");
    sink_->PutInt(size >> kObjectAlignmentBits, "ObjectSizeInWords");
    if (object_->IsCode()) {
      sink_->Put(EXECUTABLE, "executable large object");
    } else {
      sink_->Put(NOT_EXECUTABLE, "not executable large object");
    }
    back_reference = serializer_->AllocateLargeObject(size);
  } else {
    bool needs_double_align = false;
    if (object_->NeedsToEnsureDoubleAlignment()) {
      // Add wriggle room for double alignment padding.
      back_reference = serializer_->Allocate(space, size + kPointerSize);
      needs_double_align = true;
    } else {
      back_reference = serializer_->Allocate(space, size);
    }
    sink_->Put(kNewObject + reference_representation_ + space, "NewObject");
    if (needs_double_align)
      sink_->PutInt(kDoubleAlignmentSentinel, "DoubleAlignSentinel");
    int encoded_size = size >> kObjectAlignmentBits;
    DCHECK_NE(kDoubleAlignmentSentinel, encoded_size);
    sink_->PutInt(encoded_size, "ObjectSizeInWords");
  }

  // Mark this object as already serialized.
  serializer_->back_reference_map()->Add(object_, back_reference);

  // Serialize the map (first word of the object).
  serializer_->SerializeObject(map, kPlain, kStartOfObject, 0);
}


void Serializer::ObjectSerializer::SerializeExternalString() {
  // Instead of serializing this as an external string, we serialize
  // an imaginary sequential string with the same content.
  Isolate* isolate = serializer_->isolate();
  DCHECK(object_->IsExternalString());
  DCHECK(object_->map() != isolate->heap()->native_source_string_map());
  ExternalString* string = ExternalString::cast(object_);
  int length = string->length();
  Map* map;
  int content_size;
  int allocation_size;
  const byte* resource;
  // Find the map and size for the imaginary sequential string.
  bool internalized = object_->IsInternalizedString();
  if (object_->IsExternalOneByteString()) {
    map = internalized ? isolate->heap()->one_byte_internalized_string_map()
                       : isolate->heap()->one_byte_string_map();
    allocation_size = SeqOneByteString::SizeFor(length);
    content_size = length * kCharSize;
    resource = reinterpret_cast<const byte*>(
        ExternalOneByteString::cast(string)->resource()->data());
  } else {
    map = internalized ? isolate->heap()->internalized_string_map()
                       : isolate->heap()->string_map();
    allocation_size = SeqTwoByteString::SizeFor(length);
    content_size = length * kShortSize;
    resource = reinterpret_cast<const byte*>(
        ExternalTwoByteString::cast(string)->resource()->data());
  }

  AllocationSpace space = (allocation_size > Page::kMaxRegularHeapObjectSize)
                              ? LO_SPACE
                              : OLD_DATA_SPACE;
  SerializePrologue(space, allocation_size, map);

  // Output the rest of the imaginary string.
  int bytes_to_output = allocation_size - HeapObject::kHeaderSize;

  // Output raw data header. Do not bother with common raw length cases here.
  sink_->Put(kRawData, "RawDataForString");
  sink_->PutInt(bytes_to_output, "length");

  // Serialize string header (except for map).
  Address string_start = string->address();
  for (int i = HeapObject::kHeaderSize; i < SeqString::kHeaderSize; i++) {
    sink_->PutSection(string_start[i], "StringHeader");
  }

  // Serialize string content.
  sink_->PutRaw(resource, content_size, "StringContent");

  // Since the allocation size is rounded up to object alignment, there
  // maybe left-over bytes that need to be padded.
  int padding_size = allocation_size - SeqString::kHeaderSize - content_size;
  DCHECK(0 <= padding_size && padding_size < kObjectAlignment);
  for (int i = 0; i < padding_size; i++) sink_->PutSection(0, "StringPadding");

  sink_->Put(kSkip, "SkipAfterString");
  sink_->PutInt(bytes_to_output, "SkipDistance");
}


void Serializer::ObjectSerializer::Serialize() {
  if (FLAG_trace_serializer) {
    PrintF(" Encoding heap object: ");
    object_->ShortPrint();
    PrintF("\n");
  }

  // We cannot serialize typed array objects correctly.
  DCHECK(!object_->IsJSTypedArray());

  if (object_->IsScript()) {
    // Clear cached line ends.
    Object* undefined = serializer_->isolate()->heap()->undefined_value();
    Script::cast(object_)->set_line_ends(undefined);
  }

  if (object_->IsExternalString()) {
    Heap* heap = serializer_->isolate()->heap();
    if (object_->map() != heap->native_source_string_map()) {
      // Usually we cannot recreate resources for external strings. To work
      // around this, external strings are serialized to look like ordinary
      // sequential strings.
      // The exception are native source code strings, since we can recreate
      // their resources. In that case we fall through and leave it to
      // VisitExternalOneByteString further down.
      SerializeExternalString();
      return;
    }
  }

  int size = object_->Size();
  Map* map = object_->map();
  AllocationSpace space =
      MemoryChunk::FromAddress(object_->address())->owner()->identity();
  SerializePrologue(space, size, map);

  // Serialize the rest of the object.
  CHECK_EQ(0, bytes_processed_so_far_);
  bytes_processed_so_far_ = kPointerSize;

  object_->IterateBody(map->instance_type(), size, this);
  OutputRawData(object_->address() + size);
}


void Serializer::ObjectSerializer::VisitPointers(Object** start,
                                                 Object** end) {
  Object** current = start;
  while (current < end) {
    while (current < end && (*current)->IsSmi()) current++;
    if (current < end) OutputRawData(reinterpret_cast<Address>(current));

    while (current < end && !(*current)->IsSmi()) {
      HeapObject* current_contents = HeapObject::cast(*current);
      int root_index = serializer_->root_index_map()->Lookup(current_contents);
      // Repeats are not subject to the write barrier so we can only use
      // immortal immovable root members. They are never in new space.
      if (current != start && root_index != RootIndexMap::kInvalidRootIndex &&
          Heap::RootIsImmortalImmovable(root_index) &&
          current_contents == current[-1]) {
        DCHECK(!serializer_->isolate()->heap()->InNewSpace(current_contents));
        int repeat_count = 1;
        while (&current[repeat_count] < end - 1 &&
               current[repeat_count] == current_contents) {
          repeat_count++;
        }
        current += repeat_count;
        bytes_processed_so_far_ += repeat_count * kPointerSize;
        if (repeat_count > kMaxFixedRepeats) {
          sink_->Put(kVariableRepeat, "SerializeRepeats");
          sink_->PutInt(repeat_count, "SerializeRepeats");
        } else {
          sink_->Put(CodeForRepeats(repeat_count), "SerializeRepeats");
        }
      } else {
        serializer_->SerializeObject(
                current_contents, kPlain, kStartOfObject, 0);
        bytes_processed_so_far_ += kPointerSize;
        current++;
      }
    }
  }
}


void Serializer::ObjectSerializer::VisitEmbeddedPointer(RelocInfo* rinfo) {
  // Out-of-line constant pool entries will be visited by the ConstantPoolArray.
  if (FLAG_enable_ool_constant_pool && rinfo->IsInConstantPool()) return;

  int skip = OutputRawData(rinfo->target_address_address(),
                           kCanReturnSkipInsteadOfSkipping);
  HowToCode how_to_code = rinfo->IsCodedSpecially() ? kFromCode : kPlain;
  Object* object = rinfo->target_object();
  serializer_->SerializeObject(HeapObject::cast(object), how_to_code,
                               kStartOfObject, skip);
  bytes_processed_so_far_ += rinfo->target_address_size();
}


void Serializer::ObjectSerializer::VisitExternalReference(Address* p) {
  int skip = OutputRawData(reinterpret_cast<Address>(p),
                           kCanReturnSkipInsteadOfSkipping);
  sink_->Put(kExternalReference + kPlain + kStartOfObject, "ExternalRef");
  sink_->PutInt(skip, "SkipB4ExternalRef");
  Address target = *p;
  sink_->PutInt(serializer_->EncodeExternalReference(target), "reference id");
  bytes_processed_so_far_ += kPointerSize;
}


void Serializer::ObjectSerializer::VisitExternalReference(RelocInfo* rinfo) {
  int skip = OutputRawData(rinfo->target_address_address(),
                           kCanReturnSkipInsteadOfSkipping);
  HowToCode how_to_code = rinfo->IsCodedSpecially() ? kFromCode : kPlain;
  sink_->Put(kExternalReference + how_to_code + kStartOfObject, "ExternalRef");
  sink_->PutInt(skip, "SkipB4ExternalRef");
  Address target = rinfo->target_external_reference();
  sink_->PutInt(serializer_->EncodeExternalReference(target), "reference id");
  bytes_processed_so_far_ += rinfo->target_address_size();
}


void Serializer::ObjectSerializer::VisitRuntimeEntry(RelocInfo* rinfo) {
  int skip = OutputRawData(rinfo->target_address_address(),
                           kCanReturnSkipInsteadOfSkipping);
  HowToCode how_to_code = rinfo->IsCodedSpecially() ? kFromCode : kPlain;
  sink_->Put(kExternalReference + how_to_code + kStartOfObject, "ExternalRef");
  sink_->PutInt(skip, "SkipB4ExternalRef");
  Address target = rinfo->target_address();
  sink_->PutInt(serializer_->EncodeExternalReference(target), "reference id");
  bytes_processed_so_far_ += rinfo->target_address_size();
}


void Serializer::ObjectSerializer::VisitCodeTarget(RelocInfo* rinfo) {
  // Out-of-line constant pool entries will be visited by the ConstantPoolArray.
  if (FLAG_enable_ool_constant_pool && rinfo->IsInConstantPool()) return;

  int skip = OutputRawData(rinfo->target_address_address(),
                           kCanReturnSkipInsteadOfSkipping);
  Code* object = Code::GetCodeFromTargetAddress(rinfo->target_address());
  serializer_->SerializeObject(object, kFromCode, kInnerPointer, skip);
  bytes_processed_so_far_ += rinfo->target_address_size();
}


void Serializer::ObjectSerializer::VisitCodeEntry(Address entry_address) {
  int skip = OutputRawData(entry_address, kCanReturnSkipInsteadOfSkipping);
  Code* object = Code::cast(Code::GetObjectFromEntryAddress(entry_address));
  serializer_->SerializeObject(object, kPlain, kInnerPointer, skip);
  bytes_processed_so_far_ += kPointerSize;
}


void Serializer::ObjectSerializer::VisitCell(RelocInfo* rinfo) {
  // Out-of-line constant pool entries will be visited by the ConstantPoolArray.
  if (FLAG_enable_ool_constant_pool && rinfo->IsInConstantPool()) return;

  int skip = OutputRawData(rinfo->pc(), kCanReturnSkipInsteadOfSkipping);
  Cell* object = Cell::cast(rinfo->target_cell());
  serializer_->SerializeObject(object, kPlain, kInnerPointer, skip);
  bytes_processed_so_far_ += kPointerSize;
}


void Serializer::ObjectSerializer::VisitExternalOneByteString(
    v8::String::ExternalOneByteStringResource** resource_pointer) {
  Address references_start = reinterpret_cast<Address>(resource_pointer);
  OutputRawData(references_start);
  for (int i = 0; i < Natives::GetBuiltinsCount(); i++) {
    Object* source =
        serializer_->isolate()->heap()->natives_source_cache()->get(i);
    if (!source->IsUndefined()) {
      ExternalOneByteString* string = ExternalOneByteString::cast(source);
      typedef v8::String::ExternalOneByteStringResource Resource;
      const Resource* resource = string->resource();
      if (resource == *resource_pointer) {
        sink_->Put(kNativesStringResource, "NativesStringResource");
        sink_->PutSection(i, "NativesStringResourceEnd");
        bytes_processed_so_far_ += sizeof(resource);
        return;
      }
    }
  }
  // One of the strings in the natives cache should match the resource.  We
  // don't expect any other kinds of external strings here.
  UNREACHABLE();
}


Address Serializer::ObjectSerializer::PrepareCode() {
  // To make snapshots reproducible, we make a copy of the code object
  // and wipe all pointers in the copy, which we then serialize.
  Code* original = Code::cast(object_);
  Code* code = serializer_->CopyCode(original);
  // Code age headers are not serializable.
  code->MakeYoung(serializer_->isolate());
  Address entry = original->entry();
  int mode_mask = RelocInfo::kCodeTargetMask |
                  RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT) |
                  RelocInfo::ModeMask(RelocInfo::EXTERNAL_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::RUNTIME_ENTRY) |
                  RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE_ENCODED);
  for (RelocIterator it(code, mode_mask); !it.done(); it.next()) {
    RelocInfo* rinfo = it.rinfo();
    RelocInfo::Mode rmode = rinfo->rmode();
    if (RelocInfo::IsInternalReference(rmode) ||
        RelocInfo::IsInternalReferenceEncoded(rmode)) {
      // Convert internal references to relative offsets.
      Address target = rinfo->target_internal_reference();
      intptr_t offset = target - entry;
      DCHECK(0 <= offset && offset <= original->instruction_size());
      rinfo->set_target_internal_reference(reinterpret_cast<Address>(offset));
    } else if (!(FLAG_enable_ool_constant_pool && rinfo->IsInConstantPool())) {
      rinfo->WipeOut();
    }
  }
  // We need to wipe out the header fields *after* wiping out the
  // relocations, because some of these fields are needed for the latter.
  code->WipeOutHeader();
  return code->address();
}


int Serializer::ObjectSerializer::OutputRawData(
    Address up_to, Serializer::ObjectSerializer::ReturnSkip return_skip) {
  Address object_start = object_->address();
  int base = bytes_processed_so_far_;
  int up_to_offset = static_cast<int>(up_to - object_start);
  int to_skip = up_to_offset - bytes_processed_so_far_;
  int bytes_to_output = to_skip;
  bytes_processed_so_far_ += to_skip;
  // This assert will fail if the reloc info gives us the target_address_address
  // locations in a non-ascending order.  Luckily that doesn't happen.
  DCHECK(to_skip >= 0);
  bool outputting_code = false;
  if (to_skip != 0 && code_object_ && !code_has_been_output_) {
    // Output the code all at once and fix later.
    bytes_to_output = object_->Size() + to_skip - bytes_processed_so_far_;
    outputting_code = true;
    code_has_been_output_ = true;
  }
  if (bytes_to_output != 0 &&
      (!code_object_ || outputting_code)) {
#define RAW_CASE(index)                                                        \
    if (!outputting_code && bytes_to_output == index * kPointerSize &&         \
        index * kPointerSize == to_skip) {                                     \
      sink_->PutSection(kRawData + index, "RawDataFixed");                     \
      to_skip = 0;  /* This insn already skips. */                             \
    } else  /* NOLINT */
    COMMON_RAW_LENGTHS(RAW_CASE)
#undef RAW_CASE
    {  /* NOLINT */
      // We always end up here if we are outputting the code of a code object.
      sink_->Put(kRawData, "RawData");
      sink_->PutInt(bytes_to_output, "length");
    }

    if (code_object_) object_start = PrepareCode();

    const char* description = code_object_ ? "Code" : "Byte";
#ifdef MEMORY_SANITIZER
    // Object sizes are usually rounded up with uninitialized padding space.
    MSAN_MEMORY_IS_INITIALIZED(object_start + base, bytes_to_output);
#endif  // MEMORY_SANITIZER
    sink_->PutRaw(object_start + base, bytes_to_output, description);
  }
  if (to_skip != 0 && return_skip == kIgnoringReturn) {
    sink_->Put(kSkip, "Skip");
    sink_->PutInt(to_skip, "SkipDistance");
    to_skip = 0;
  }
  return to_skip;
}


BackReference Serializer::AllocateLargeObject(int size) {
  // Large objects are allocated one-by-one when deserializing. We do not
  // have to keep track of multiple chunks.
  large_objects_total_size_ += size;
  return BackReference::LargeObjectReference(seen_large_objects_index_++);
}


BackReference Serializer::Allocate(AllocationSpace space, int size) {
  DCHECK(space >= 0 && space < kNumberOfPreallocatedSpaces);
  DCHECK(size > 0 && size <= static_cast<int>(max_chunk_size(space)));
  uint32_t new_chunk_size = pending_chunk_[space] + size;
  if (new_chunk_size > max_chunk_size(space)) {
    // The new chunk size would not fit onto a single page. Complete the
    // current chunk and start a new one.
    sink_->Put(kNextChunk, "NextChunk");
    sink_->Put(space, "NextChunkSpace");
    completed_chunks_[space].Add(pending_chunk_[space]);
    DCHECK_LE(completed_chunks_[space].length(), BackReference::kMaxChunkIndex);
    pending_chunk_[space] = 0;
    new_chunk_size = size;
  }
  uint32_t offset = pending_chunk_[space];
  pending_chunk_[space] = new_chunk_size;
  return BackReference::Reference(space, completed_chunks_[space].length(),
                                  offset);
}


void Serializer::Pad() {
  // The non-branching GetInt will read up to 3 bytes too far, so we need
  // to pad the snapshot to make sure we don't read over the end.
  for (unsigned i = 0; i < sizeof(int32_t) - 1; i++) {
    sink_->Put(kNop, "Padding");
  }
  // Pad up to pointer size for checksum.
  while (!IsAligned(sink_->Position(), kPointerAlignment)) {
    sink_->Put(kNop, "Padding");
  }
}


void Serializer::InitializeCodeAddressMap() {
  isolate_->InitializeLoggingAndCounters();
  code_address_map_ = new CodeAddressMap(isolate_);
}


Code* Serializer::CopyCode(Code* code) {
  code_buffer_.Rewind(0);  // Clear buffer without deleting backing store.
  int size = code->CodeSize();
  code_buffer_.AddAll(Vector<byte>(code->address(), size));
  return Code::cast(HeapObject::FromAddress(&code_buffer_.first()));
}


ScriptData* CodeSerializer::Serialize(Isolate* isolate,
                                      Handle<SharedFunctionInfo> info,
                                      Handle<String> source) {
  base::ElapsedTimer timer;
  if (FLAG_profile_deserialization) timer.Start();
  if (FLAG_trace_serializer) {
    PrintF("[Serializing from");
    Object* script = info->script();
    if (script->IsScript()) Script::cast(script)->name()->ShortPrint();
    PrintF("]\n");
  }

  // Serialize code object.
  SnapshotByteSink sink(info->code()->CodeSize() * 2);
  CodeSerializer cs(isolate, &sink, *source, info->code());
  DisallowHeapAllocation no_gc;
  Object** location = Handle<Object>::cast(info).location();
  cs.VisitPointer(location);
  cs.Pad();

  SerializedCodeData data(sink.data(), cs);
  ScriptData* script_data = data.GetScriptData();

  if (FLAG_profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    int length = script_data->length();
    PrintF("[Serializing to %d bytes took %0.3f ms]\n", length, ms);
  }

  return script_data;
}


void CodeSerializer::SerializeObject(HeapObject* obj, HowToCode how_to_code,
                                     WhereToPoint where_to_point, int skip) {
  int root_index = root_index_map_.Lookup(obj);
  if (root_index != RootIndexMap::kInvalidRootIndex) {
    PutRoot(root_index, obj, how_to_code, where_to_point, skip);
    return;
  }

  if (SerializeKnownObject(obj, how_to_code, where_to_point, skip)) return;

  FlushSkip(skip);

  if (obj->IsCode()) {
    Code* code_object = Code::cast(obj);
    switch (code_object->kind()) {
      case Code::OPTIMIZED_FUNCTION:  // No optimized code compiled yet.
      case Code::HANDLER:             // No handlers patched in yet.
      case Code::REGEXP:              // No regexp literals initialized yet.
      case Code::NUMBER_OF_KINDS:     // Pseudo enum value.
        CHECK(false);
      case Code::BUILTIN:
        SerializeBuiltin(code_object->builtin_index(), how_to_code,
                         where_to_point);
        return;
      case Code::STUB:
        SerializeCodeStub(code_object->stub_key(), how_to_code, where_to_point);
        return;
#define IC_KIND_CASE(KIND) case Code::KIND:
        IC_KIND_LIST(IC_KIND_CASE)
#undef IC_KIND_CASE
        SerializeIC(code_object, how_to_code, where_to_point);
        return;
      case Code::FUNCTION:
        DCHECK(code_object->has_reloc_info_for_serialization());
        // Only serialize the code for the toplevel function unless specified
        // by flag. Replace code of inner functions by the lazy compile builtin.
        // This is safe, as checked in Compiler::BuildFunctionInfo.
        if (code_object != main_code_ && !FLAG_serialize_inner) {
          SerializeBuiltin(Builtins::kCompileLazy, how_to_code, where_to_point);
        } else {
          SerializeGeneric(code_object, how_to_code, where_to_point);
        }
        return;
    }
    UNREACHABLE();
  }

  // Past this point we should not see any (context-specific) maps anymore.
  CHECK(!obj->IsMap());
  // There should be no references to the global object embedded.
  CHECK(!obj->IsJSGlobalProxy() && !obj->IsGlobalObject());
  // There should be no hash table embedded. They would require rehashing.
  CHECK(!obj->IsHashTable());
  // We expect no instantiated function objects or contexts.
  CHECK(!obj->IsJSFunction() && !obj->IsContext());

  SerializeGeneric(obj, how_to_code, where_to_point);
}


void CodeSerializer::SerializeGeneric(HeapObject* heap_object,
                                      HowToCode how_to_code,
                                      WhereToPoint where_to_point) {
  if (heap_object->IsInternalizedString()) num_internalized_strings_++;

  // Object has not yet been serialized.  Serialize it here.
  ObjectSerializer serializer(this, heap_object, sink_, how_to_code,
                              where_to_point);
  serializer.Serialize();
}


void CodeSerializer::SerializeBuiltin(int builtin_index, HowToCode how_to_code,
                                      WhereToPoint where_to_point) {
  DCHECK((how_to_code == kPlain && where_to_point == kStartOfObject) ||
         (how_to_code == kPlain && where_to_point == kInnerPointer) ||
         (how_to_code == kFromCode && where_to_point == kInnerPointer));
  DCHECK_LT(builtin_index, Builtins::builtin_count);
  DCHECK_LE(0, builtin_index);

  if (FLAG_trace_serializer) {
    PrintF(" Encoding builtin: %s\n",
           isolate()->builtins()->name(builtin_index));
  }

  sink_->Put(kBuiltin + how_to_code + where_to_point, "Builtin");
  sink_->PutInt(builtin_index, "builtin_index");
}


void CodeSerializer::SerializeCodeStub(uint32_t stub_key, HowToCode how_to_code,
                                       WhereToPoint where_to_point) {
  DCHECK((how_to_code == kPlain && where_to_point == kStartOfObject) ||
         (how_to_code == kPlain && where_to_point == kInnerPointer) ||
         (how_to_code == kFromCode && where_to_point == kInnerPointer));
  DCHECK(CodeStub::MajorKeyFromKey(stub_key) != CodeStub::NoCache);
  DCHECK(!CodeStub::GetCode(isolate(), stub_key).is_null());

  int index = AddCodeStubKey(stub_key) + kCodeStubsBaseIndex;

  if (FLAG_trace_serializer) {
    PrintF(" Encoding code stub %s as %d\n",
           CodeStub::MajorName(CodeStub::MajorKeyFromKey(stub_key), false),
           index);
  }

  sink_->Put(kAttachedReference + how_to_code + where_to_point, "CodeStub");
  sink_->PutInt(index, "CodeStub key");
}


void CodeSerializer::SerializeIC(Code* ic, HowToCode how_to_code,
                                 WhereToPoint where_to_point) {
  // The IC may be implemented as a stub.
  uint32_t stub_key = ic->stub_key();
  if (stub_key != CodeStub::NoCacheKey()) {
    if (FLAG_trace_serializer) {
      PrintF(" %s is a code stub\n", Code::Kind2String(ic->kind()));
    }
    SerializeCodeStub(stub_key, how_to_code, where_to_point);
    return;
  }
  // The IC may be implemented as builtin. Only real builtins have an
  // actual builtin_index value attached (otherwise it's just garbage).
  // Compare to make sure we are really dealing with a builtin.
  int builtin_index = ic->builtin_index();
  if (builtin_index < Builtins::builtin_count) {
    Builtins::Name name = static_cast<Builtins::Name>(builtin_index);
    Code* builtin = isolate()->builtins()->builtin(name);
    if (builtin == ic) {
      if (FLAG_trace_serializer) {
        PrintF(" %s is a builtin\n", Code::Kind2String(ic->kind()));
      }
      DCHECK(ic->kind() == Code::KEYED_LOAD_IC ||
             ic->kind() == Code::KEYED_STORE_IC);
      SerializeBuiltin(builtin_index, how_to_code, where_to_point);
      return;
    }
  }
  // The IC may also just be a piece of code kept in the non_monomorphic_cache.
  // In that case, just serialize as a normal code object.
  if (FLAG_trace_serializer) {
    PrintF(" %s has no special handling\n", Code::Kind2String(ic->kind()));
  }
  DCHECK(ic->kind() == Code::LOAD_IC || ic->kind() == Code::STORE_IC);
  SerializeGeneric(ic, how_to_code, where_to_point);
}


int CodeSerializer::AddCodeStubKey(uint32_t stub_key) {
  // TODO(yangguo) Maybe we need a hash table for a faster lookup than O(n^2).
  int index = 0;
  while (index < stub_keys_.length()) {
    if (stub_keys_[index] == stub_key) return index;
    index++;
  }
  stub_keys_.Add(stub_key);
  return index;
}


MaybeHandle<SharedFunctionInfo> CodeSerializer::Deserialize(
    Isolate* isolate, ScriptData* cached_data, Handle<String> source) {
  base::ElapsedTimer timer;
  if (FLAG_profile_deserialization) timer.Start();

  HandleScope scope(isolate);

  SmartPointer<SerializedCodeData> scd(
      SerializedCodeData::FromCachedData(isolate, cached_data, *source));
  if (scd.is_empty()) {
    if (FLAG_profile_deserialization) PrintF("[Cached code failed check]\n");
    DCHECK(cached_data->rejected());
    return MaybeHandle<SharedFunctionInfo>();
  }

  // Eagerly expand string table to avoid allocations during deserialization.
  StringTable::EnsureCapacityForDeserialization(isolate,
                                                scd->NumInternalizedStrings());

  // Prepare and register list of attached objects.
  Vector<const uint32_t> code_stub_keys = scd->CodeStubKeys();
  Vector<Handle<Object> > attached_objects = Vector<Handle<Object> >::New(
      code_stub_keys.length() + kCodeStubsBaseIndex);
  attached_objects[kSourceObjectIndex] = source;
  for (int i = 0; i < code_stub_keys.length(); i++) {
    attached_objects[i + kCodeStubsBaseIndex] =
        CodeStub::GetCode(isolate, code_stub_keys[i]).ToHandleChecked();
  }

  Deserializer deserializer(scd.get());
  deserializer.SetAttachedObjects(attached_objects);

  // Deserialize.
  Handle<SharedFunctionInfo> result;
  if (!deserializer.DeserializeCode(isolate).ToHandle(&result)) {
    // Deserializing may fail if the reservations cannot be fulfilled.
    if (FLAG_profile_deserialization) PrintF("[Deserializing failed]\n");
    return MaybeHandle<SharedFunctionInfo>();
  }
  deserializer.FlushICacheForNewCodeObjects();

  if (FLAG_profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    int length = cached_data->length();
    PrintF("[Deserializing from %d bytes took %0.3f ms]\n", length, ms);
  }
  result->set_deserialized(true);

  if (isolate->logger()->is_logging_code_events() ||
      isolate->cpu_profiler()->is_profiling()) {
    String* name = isolate->heap()->empty_string();
    if (result->script()->IsScript()) {
      Script* script = Script::cast(result->script());
      if (script->name()->IsString()) name = String::cast(script->name());
    }
    isolate->logger()->CodeCreateEvent(Logger::SCRIPT_TAG, result->code(),
                                       *result, NULL, name);
  }

  return scope.CloseAndEscape(result);
}


void SerializedData::AllocateData(int size) {
  DCHECK(!owns_data_);
  data_ = NewArray<byte>(size);
  size_ = size;
  owns_data_ = true;
  DCHECK(IsAligned(reinterpret_cast<intptr_t>(data_), kPointerAlignment));
}


SnapshotData::SnapshotData(const Serializer& ser) {
  DisallowHeapAllocation no_gc;
  List<Reservation> reservations;
  ser.EncodeReservations(&reservations);
  const List<byte>& payload = ser.sink()->data();

  // Calculate sizes.
  int reservation_size = reservations.length() * kInt32Size;
  int size = kHeaderSize + reservation_size + payload.length();

  // Allocate backing store and create result data.
  AllocateData(size);

  // Set header values.
  SetMagicNumber(ser.isolate());
  SetHeaderValue(kCheckSumOffset, Version::Hash());
  SetHeaderValue(kNumReservationsOffset, reservations.length());
  SetHeaderValue(kPayloadLengthOffset, payload.length());

  // Copy reservation chunk sizes.
  CopyBytes(data_ + kHeaderSize, reinterpret_cast<byte*>(reservations.begin()),
            reservation_size);

  // Copy serialized data.
  CopyBytes(data_ + kHeaderSize + reservation_size, payload.begin(),
            static_cast<size_t>(payload.length()));
}


bool SnapshotData::IsSane() {
  return GetHeaderValue(kCheckSumOffset) == Version::Hash();
}


Vector<const SerializedData::Reservation> SnapshotData::Reservations() const {
  return Vector<const Reservation>(
      reinterpret_cast<const Reservation*>(data_ + kHeaderSize),
      GetHeaderValue(kNumReservationsOffset));
}


Vector<const byte> SnapshotData::Payload() const {
  int reservations_size = GetHeaderValue(kNumReservationsOffset) * kInt32Size;
  const byte* payload = data_ + kHeaderSize + reservations_size;
  int length = GetHeaderValue(kPayloadLengthOffset);
  DCHECK_EQ(data_ + size_, payload + length);
  return Vector<const byte>(payload, length);
}


class Checksum {
 public:
  explicit Checksum(Vector<const byte> payload) {
    // Fletcher's checksum. Modified to reduce 64-bit sums to 32-bit.
    uintptr_t a = 1;
    uintptr_t b = 0;
    const uintptr_t* cur = reinterpret_cast<const uintptr_t*>(payload.start());
    DCHECK(IsAligned(payload.length(), kIntptrSize));
    const uintptr_t* end = cur + payload.length() / kIntptrSize;
    while (cur < end) {
      // Unsigned overflow expected and intended.
      a += *cur++;
      b += a;
    }
#if V8_HOST_ARCH_64_BIT
    a ^= a >> 32;
    b ^= b >> 32;
#endif  // V8_HOST_ARCH_64_BIT
    a_ = static_cast<uint32_t>(a);
    b_ = static_cast<uint32_t>(b);
  }

  bool Check(uint32_t a, uint32_t b) const { return a == a_ && b == b_; }

  uint32_t a() const { return a_; }
  uint32_t b() const { return b_; }

 private:
  uint32_t a_;
  uint32_t b_;

  DISALLOW_COPY_AND_ASSIGN(Checksum);
};


SerializedCodeData::SerializedCodeData(const List<byte>& payload,
                                       const CodeSerializer& cs) {
  DisallowHeapAllocation no_gc;
  const List<uint32_t>* stub_keys = cs.stub_keys();

  List<Reservation> reservations;
  cs.EncodeReservations(&reservations);

  // Calculate sizes.
  int reservation_size = reservations.length() * kInt32Size;
  int num_stub_keys = stub_keys->length();
  int stub_keys_size = stub_keys->length() * kInt32Size;
  int payload_offset = kHeaderSize + reservation_size + stub_keys_size;
  int padded_payload_offset = POINTER_SIZE_ALIGN(payload_offset);
  int size = padded_payload_offset + payload.length();

  // Allocate backing store and create result data.
  AllocateData(size);

  // Set header values.
  SetMagicNumber(cs.isolate());
  SetHeaderValue(kVersionHashOffset, Version::Hash());
  SetHeaderValue(kSourceHashOffset, SourceHash(cs.source()));
  SetHeaderValue(kCpuFeaturesOffset,
                 static_cast<uint32_t>(CpuFeatures::SupportedFeatures()));
  SetHeaderValue(kFlagHashOffset, FlagList::Hash());
  SetHeaderValue(kNumInternalizedStringsOffset, cs.num_internalized_strings());
  SetHeaderValue(kNumReservationsOffset, reservations.length());
  SetHeaderValue(kNumCodeStubKeysOffset, num_stub_keys);
  SetHeaderValue(kPayloadLengthOffset, payload.length());

  Checksum checksum(payload.ToConstVector());
  SetHeaderValue(kChecksum1Offset, checksum.a());
  SetHeaderValue(kChecksum2Offset, checksum.b());

  // Copy reservation chunk sizes.
  CopyBytes(data_ + kHeaderSize, reinterpret_cast<byte*>(reservations.begin()),
            reservation_size);

  // Copy code stub keys.
  CopyBytes(data_ + kHeaderSize + reservation_size,
            reinterpret_cast<byte*>(stub_keys->begin()), stub_keys_size);

  memset(data_ + payload_offset, 0, padded_payload_offset - payload_offset);

  // Copy serialized data.
  CopyBytes(data_ + padded_payload_offset, payload.begin(),
            static_cast<size_t>(payload.length()));
}


SerializedCodeData::SanityCheckResult SerializedCodeData::SanityCheck(
    Isolate* isolate, String* source) const {
  uint32_t magic_number = GetMagicNumber();
  uint32_t version_hash = GetHeaderValue(kVersionHashOffset);
  uint32_t source_hash = GetHeaderValue(kSourceHashOffset);
  uint32_t cpu_features = GetHeaderValue(kCpuFeaturesOffset);
  uint32_t flags_hash = GetHeaderValue(kFlagHashOffset);
  uint32_t c1 = GetHeaderValue(kChecksum1Offset);
  uint32_t c2 = GetHeaderValue(kChecksum2Offset);
  if (magic_number != ComputeMagicNumber(isolate)) return MAGIC_NUMBER_MISMATCH;
  if (version_hash != Version::Hash()) return VERSION_MISMATCH;
  if (source_hash != SourceHash(source)) return SOURCE_MISMATCH;
  if (cpu_features != static_cast<uint32_t>(CpuFeatures::SupportedFeatures())) {
    return CPU_FEATURES_MISMATCH;
  }
  if (flags_hash != FlagList::Hash()) return FLAGS_MISMATCH;
  if (!Checksum(Payload()).Check(c1, c2)) return CHECKSUM_MISMATCH;
  return CHECK_SUCCESS;
}


// Return ScriptData object and relinquish ownership over it to the caller.
ScriptData* SerializedCodeData::GetScriptData() {
  DCHECK(owns_data_);
  ScriptData* result = new ScriptData(data_, size_);
  result->AcquireDataOwnership();
  owns_data_ = false;
  data_ = NULL;
  return result;
}


Vector<const SerializedData::Reservation> SerializedCodeData::Reservations()
    const {
  return Vector<const Reservation>(
      reinterpret_cast<const Reservation*>(data_ + kHeaderSize),
      GetHeaderValue(kNumReservationsOffset));
}


Vector<const byte> SerializedCodeData::Payload() const {
  int reservations_size = GetHeaderValue(kNumReservationsOffset) * kInt32Size;
  int code_stubs_size = GetHeaderValue(kNumCodeStubKeysOffset) * kInt32Size;
  int payload_offset = kHeaderSize + reservations_size + code_stubs_size;
  int padded_payload_offset = POINTER_SIZE_ALIGN(payload_offset);
  const byte* payload = data_ + padded_payload_offset;
  DCHECK(IsAligned(reinterpret_cast<intptr_t>(payload), kPointerAlignment));
  int length = GetHeaderValue(kPayloadLengthOffset);
  DCHECK_EQ(data_ + size_, payload + length);
  return Vector<const byte>(payload, length);
}


int SerializedCodeData::NumInternalizedStrings() const {
  return GetHeaderValue(kNumInternalizedStringsOffset);
}

Vector<const uint32_t> SerializedCodeData::CodeStubKeys() const {
  int reservations_size = GetHeaderValue(kNumReservationsOffset) * kInt32Size;
  const byte* start = data_ + kHeaderSize + reservations_size;
  return Vector<const uint32_t>(reinterpret_cast<const uint32_t*>(start),
                                GetHeaderValue(kNumCodeStubKeysOffset));
}


SerializedCodeData::SerializedCodeData(ScriptData* data)
    : SerializedData(const_cast<byte*>(data->data()), data->length()) {}


SerializedCodeData* SerializedCodeData::FromCachedData(Isolate* isolate,
                                                       ScriptData* cached_data,
                                                       String* source) {
  DisallowHeapAllocation no_gc;
  SerializedCodeData* scd = new SerializedCodeData(cached_data);
  SanityCheckResult r = scd->SanityCheck(isolate, source);
  if (r == CHECK_SUCCESS) return scd;
  cached_data->Reject();
  source->GetIsolate()->counters()->code_cache_reject_reason()->AddSample(r);
  delete scd;
  return NULL;
}
} }  // namespace v8::internal
