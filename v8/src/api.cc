// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api.h"

#include <string.h>  // For memcpy, strlen.
#ifdef V8_USE_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif  // V8_USE_ADDRESS_SANITIZER
#include <cmath>  // For isnan.
#include "include/v8-debug.h"
#include "include/v8-profiler.h"
#include "include/v8-testing.h"
#include "src/api-natives.h"
#include "src/assert-scope.h"
#include "src/background-parsing-task.h"
#include "src/base/functional.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/utils/random-number-generator.h"
#include "src/bootstrapper.h"
#include "src/code-stubs.h"
#include "src/compiler.h"
#include "src/conversions-inl.h"
#include "src/counters.h"
#include "src/cpu-profiler.h"
#include "src/debug.h"
#include "src/deoptimizer.h"
#include "src/execution.h"
#include "src/global-handles.h"
#include "src/heap-profiler.h"
#include "src/heap-snapshot-generator-inl.h"
#include "src/icu_util.h"
#include "src/json-parser.h"
#include "src/messages.h"
#include "src/natives.h"
#include "src/parser.h"
#include "src/profile-generator-inl.h"
#include "src/property.h"
#include "src/property-details.h"
#include "src/prototype.h"
#include "src/runtime/runtime.h"
#include "src/runtime-profiler.h"
#include "src/sampler.h"
#include "src/scanner-character-streams.h"
#include "src/simulator.h"
#include "src/snapshot.h"
#include "src/unicode-inl.h"
#include "src/v8threads.h"
#include "src/version.h"
#include "src/vm-state-inl.h"


#define LOG_API(isolate, expr) LOG(isolate, ApiEntryCall(expr))

#define ENTER_V8(isolate)             \
  i::VMState<v8::OTHER> __state__((isolate))

namespace v8 {

#define EXCEPTION_PREAMBLE(isolate)                                         \
  (isolate)->handle_scope_implementer()->IncrementCallDepth();              \
  DCHECK(!(isolate)->external_caught_exception());                          \
  bool has_pending_exception = false


#define EXCEPTION_BAILOUT_CHECK(isolate, value)                              \
  do {                                                                       \
    i::HandleScopeImplementer* handle_scope_implementer =                    \
        (isolate)->handle_scope_implementer();                               \
    handle_scope_implementer->DecrementCallDepth();                          \
    if (has_pending_exception) {                                             \
      bool call_depth_is_zero = handle_scope_implementer->CallDepthIsZero(); \
      (isolate)->OptionalRescheduleException(call_depth_is_zero);            \
      return value;                                                          \
    }                                                                        \
  } while (false)


#define PREPARE_FOR_EXECUTION_GENERIC(isolate, context, function_name, \
                                      bailout_value, HandleScopeClass, \
                                      do_callback)                     \
  if (IsExecutionTerminatingCheck(isolate)) {                          \
    return bailout_value;                                              \
  }                                                                    \
  HandleScopeClass handle_scope(isolate);                              \
  CallDepthScope call_depth_scope(isolate, context, do_callback);      \
  LOG_API(isolate, function_name);                                     \
  ENTER_V8(isolate);                                                   \
  bool has_pending_exception = false


#define PREPARE_FOR_EXECUTION_WITH_CONTEXT(                                  \
    context, function_name, bailout_value, HandleScopeClass, do_callback)    \
  auto isolate = context.IsEmpty()                                           \
                     ? i::Isolate::Current()                                 \
                     : reinterpret_cast<i::Isolate*>(context->GetIsolate()); \
  PREPARE_FOR_EXECUTION_GENERIC(isolate, context, function_name,             \
                                bailout_value, HandleScopeClass, do_callback);


#define PREPARE_FOR_EXECUTION_WITH_ISOLATE(isolate, function_name, T)     \
  PREPARE_FOR_EXECUTION_GENERIC(isolate, Local<Context>(), function_name, \
                                MaybeLocal<T>(), InternalEscapableScope,  \
                                false);


#define PREPARE_FOR_EXECUTION(context, function_name, T)                      \
  PREPARE_FOR_EXECUTION_WITH_CONTEXT(context, function_name, MaybeLocal<T>(), \
                                     InternalEscapableScope, false)


#define PREPARE_FOR_EXECUTION_WITH_CALLBACK(context, function_name, T)        \
  PREPARE_FOR_EXECUTION_WITH_CONTEXT(context, function_name, MaybeLocal<T>(), \
                                     InternalEscapableScope, true)


#define PREPARE_FOR_EXECUTION_PRIMITIVE(context, function_name, T)         \
  PREPARE_FOR_EXECUTION_WITH_CONTEXT(context, function_name, Nothing<T>(), \
                                     i::HandleScope, false)


#define EXCEPTION_BAILOUT_CHECK_SCOPED(isolate, value) \
  do {                                                 \
    if (has_pending_exception) {                       \
      call_depth_scope.Escape();                       \
      return value;                                    \
    }                                                  \
  } while (false)


#define RETURN_ON_FAILED_EXECUTION(T) \
  EXCEPTION_BAILOUT_CHECK_SCOPED(isolate, MaybeLocal<T>())


#define RETURN_ON_FAILED_EXECUTION_PRIMITIVE(T) \
  EXCEPTION_BAILOUT_CHECK_SCOPED(isolate, Nothing<T>())


#define RETURN_TO_LOCAL_UNCHECKED(maybe_local, T) \
  return maybe_local.FromMaybe(Local<T>());


#define RETURN_ESCAPED(value) return handle_scope.Escape(value);


namespace {

Local<Context> ContextFromHeapObject(i::Handle<i::Object> obj) {
  return reinterpret_cast<v8::Isolate*>(i::HeapObject::cast(*obj)->GetIsolate())
      ->GetCurrentContext();
}

class InternalEscapableScope : public v8::EscapableHandleScope {
 public:
  explicit inline InternalEscapableScope(i::Isolate* isolate)
      : v8::EscapableHandleScope(reinterpret_cast<v8::Isolate*>(isolate)) {}
};


class CallDepthScope {
 public:
  explicit CallDepthScope(i::Isolate* isolate, Local<Context> context,
                          bool do_callback)
      : isolate_(isolate),
        context_(context),
        escaped_(false),
        do_callback_(do_callback) {
    // TODO(dcarney): remove this when blink stops crashing.
    DCHECK(!isolate_->external_caught_exception());
    isolate_->handle_scope_implementer()->IncrementCallDepth();
    if (!context_.IsEmpty()) context_->Enter();
  }
  ~CallDepthScope() {
    if (!context_.IsEmpty()) context_->Exit();
    if (!escaped_) isolate_->handle_scope_implementer()->DecrementCallDepth();
    if (do_callback_) isolate_->FireCallCompletedCallback();
  }

  void Escape() {
    DCHECK(!escaped_);
    escaped_ = true;
    auto handle_scope_implementer = isolate_->handle_scope_implementer();
    handle_scope_implementer->DecrementCallDepth();
    bool call_depth_is_zero = handle_scope_implementer->CallDepthIsZero();
    isolate_->OptionalRescheduleException(call_depth_is_zero);
  }

 private:
  i::Isolate* const isolate_;
  Local<Context> context_;
  bool escaped_;
  bool do_callback_;
};

}  // namespace


static ScriptOrigin GetScriptOriginForScript(i::Isolate* isolate,
                                             i::Handle<i::Script> script) {
  i::Handle<i::Object> scriptName(i::Script::GetNameOrSourceURL(script));
  i::Handle<i::Object> source_map_url(script->source_mapping_url(), isolate);
  v8::Isolate* v8_isolate =
      reinterpret_cast<v8::Isolate*>(script->GetIsolate());
  v8::ScriptOrigin origin(
      Utils::ToLocal(scriptName),
      v8::Integer::New(v8_isolate, script->line_offset()->value()),
      v8::Integer::New(v8_isolate, script->column_offset()->value()),
      v8::Boolean::New(v8_isolate, script->is_shared_cross_origin()),
      v8::Integer::New(v8_isolate, script->id()->value()),
      v8::Boolean::New(v8_isolate, script->is_embedder_debug_script()),
      Utils::ToLocal(source_map_url));
  return origin;
}


// --- E x c e p t i o n   B e h a v i o r ---


void i::FatalProcessOutOfMemory(const char* location) {
  i::V8::FatalProcessOutOfMemory(location, false);
}


// When V8 cannot allocated memory FatalProcessOutOfMemory is called.
// The default fatal error handler is called and execution is stopped.
void i::V8::FatalProcessOutOfMemory(const char* location, bool take_snapshot) {
  i::HeapStats heap_stats;
  int start_marker;
  heap_stats.start_marker = &start_marker;
  int new_space_size;
  heap_stats.new_space_size = &new_space_size;
  int new_space_capacity;
  heap_stats.new_space_capacity = &new_space_capacity;
  intptr_t old_pointer_space_size;
  heap_stats.old_pointer_space_size = &old_pointer_space_size;
  intptr_t old_pointer_space_capacity;
  heap_stats.old_pointer_space_capacity = &old_pointer_space_capacity;
  intptr_t old_data_space_size;
  heap_stats.old_data_space_size = &old_data_space_size;
  intptr_t old_data_space_capacity;
  heap_stats.old_data_space_capacity = &old_data_space_capacity;
  intptr_t code_space_size;
  heap_stats.code_space_size = &code_space_size;
  intptr_t code_space_capacity;
  heap_stats.code_space_capacity = &code_space_capacity;
  intptr_t map_space_size;
  heap_stats.map_space_size = &map_space_size;
  intptr_t map_space_capacity;
  heap_stats.map_space_capacity = &map_space_capacity;
  intptr_t cell_space_size;
  heap_stats.cell_space_size = &cell_space_size;
  intptr_t cell_space_capacity;
  heap_stats.cell_space_capacity = &cell_space_capacity;
  intptr_t property_cell_space_size;
  heap_stats.property_cell_space_size = &property_cell_space_size;
  intptr_t property_cell_space_capacity;
  heap_stats.property_cell_space_capacity = &property_cell_space_capacity;
  intptr_t lo_space_size;
  heap_stats.lo_space_size = &lo_space_size;
  int global_handle_count;
  heap_stats.global_handle_count = &global_handle_count;
  int weak_global_handle_count;
  heap_stats.weak_global_handle_count = &weak_global_handle_count;
  int pending_global_handle_count;
  heap_stats.pending_global_handle_count = &pending_global_handle_count;
  int near_death_global_handle_count;
  heap_stats.near_death_global_handle_count = &near_death_global_handle_count;
  int free_global_handle_count;
  heap_stats.free_global_handle_count = &free_global_handle_count;
  intptr_t memory_allocator_size;
  heap_stats.memory_allocator_size = &memory_allocator_size;
  intptr_t memory_allocator_capacity;
  heap_stats.memory_allocator_capacity = &memory_allocator_capacity;
  int objects_per_type[LAST_TYPE + 1] = {0};
  heap_stats.objects_per_type = objects_per_type;
  int size_per_type[LAST_TYPE + 1] = {0};
  heap_stats.size_per_type = size_per_type;
  int os_error;
  heap_stats.os_error = &os_error;
  int end_marker;
  heap_stats.end_marker = &end_marker;
  i::Isolate* isolate = i::Isolate::Current();
  if (isolate->heap()->HasBeenSetUp()) {
    // BUG(1718): Don't use the take_snapshot since we don't support
    // HeapIterator here without doing a special GC.
    isolate->heap()->RecordStats(&heap_stats, false);
  }
  Utils::ApiCheck(false, location, "Allocation failed - process out of memory");
  // If the fatal error handler returns, we stop execution.
  FATAL("API fatal error handler returned after process out of memory");
}


void Utils::ReportApiFailure(const char* location, const char* message) {
  i::Isolate* isolate = i::Isolate::Current();
  FatalErrorCallback callback = isolate->exception_behavior();
  if (callback == NULL) {
    base::OS::PrintError("\n#\n# Fatal error in %s\n# %s\n#\n\n", location,
                         message);
    base::OS::Abort();
  } else {
    callback(location, message);
  }
  isolate->SignalFatalError();
}


static inline bool IsExecutionTerminatingCheck(i::Isolate* isolate) {
  if (isolate->has_scheduled_exception()) {
    return isolate->scheduled_exception() ==
        isolate->heap()->termination_exception();
  }
  return false;
}


void V8::SetNativesDataBlob(StartupData* natives_blob) {
  i::V8::SetNativesBlob(natives_blob);
}


void V8::SetSnapshotDataBlob(StartupData* snapshot_blob) {
  i::V8::SetSnapshotBlob(snapshot_blob);
}


bool RunExtraCode(Isolate* isolate, const char* utf8_source) {
  // Run custom script if provided.
  base::ElapsedTimer timer;
  timer.Start();
  TryCatch try_catch;
  Local<String> source_string = String::NewFromUtf8(isolate, utf8_source);
  if (try_catch.HasCaught()) return false;
  ScriptOrigin origin(String::NewFromUtf8(isolate, "<embedded script>"));
  ScriptCompiler::Source source(source_string, origin);
  Local<Script> script = ScriptCompiler::Compile(isolate, &source);
  if (try_catch.HasCaught()) return false;
  script->Run();
  if (i::FLAG_profile_deserialization) {
    i::PrintF("Executing custom snapshot script took %0.3f ms\n",
              timer.Elapsed().InMillisecondsF());
  }
  timer.Stop();
  return !try_catch.HasCaught();
}


StartupData V8::CreateSnapshotDataBlob(const char* custom_source) {
  i::Isolate* internal_isolate = new i::Isolate(true);
  Isolate* isolate = reinterpret_cast<Isolate*>(internal_isolate);
  StartupData result = {NULL, 0};
  {
    base::ElapsedTimer timer;
    timer.Start();
    Isolate::Scope isolate_scope(isolate);
    internal_isolate->Init(NULL);
    Persistent<Context> context;
    i::Snapshot::Metadata metadata;
    {
      HandleScope handle_scope(isolate);
      Handle<Context> new_context = Context::New(isolate);
      context.Reset(isolate, new_context);
      if (custom_source != NULL) {
        metadata.set_embeds_script(true);
        Context::Scope context_scope(new_context);
        if (!RunExtraCode(isolate, custom_source)) context.Reset();
      }
    }
    if (!context.IsEmpty()) {
      // Make sure all builtin scripts are cached.
      {
        HandleScope scope(isolate);
        for (int i = 0; i < i::Natives::GetBuiltinsCount(); i++) {
          internal_isolate->bootstrapper()->NativesSourceLookup(i);
        }
      }
      // If we don't do this then we end up with a stray root pointing at the
      // context even after we have disposed of the context.
      internal_isolate->heap()->CollectAllAvailableGarbage("mksnapshot");
      i::Object* raw_context = *v8::Utils::OpenPersistent(context);
      context.Reset();

      i::SnapshotByteSink snapshot_sink;
      i::StartupSerializer ser(internal_isolate, &snapshot_sink);
      ser.SerializeStrongReferences();

      i::SnapshotByteSink context_sink;
      i::PartialSerializer context_ser(internal_isolate, &ser, &context_sink);
      context_ser.Serialize(&raw_context);
      ser.SerializeWeakReferences();

      result = i::Snapshot::CreateSnapshotBlob(ser, context_ser, metadata);
    }
    if (i::FLAG_profile_deserialization) {
      i::PrintF("Creating snapshot took %0.3f ms\n",
                timer.Elapsed().InMillisecondsF());
    }
    timer.Stop();
  }
  isolate->Dispose();
  return result;
}


void V8::SetFlagsFromString(const char* str, int length) {
  i::FlagList::SetFlagsFromString(str, length);
}


void V8::SetFlagsFromCommandLine(int* argc, char** argv, bool remove_flags) {
  i::FlagList::SetFlagsFromCommandLine(argc, argv, remove_flags);
}


RegisteredExtension* RegisteredExtension::first_extension_ = NULL;


RegisteredExtension::RegisteredExtension(Extension* extension)
    : extension_(extension) { }


void RegisteredExtension::Register(RegisteredExtension* that) {
  that->next_ = first_extension_;
  first_extension_ = that;
}


void RegisteredExtension::UnregisterAll() {
  RegisteredExtension* re = first_extension_;
  while (re != NULL) {
    RegisteredExtension* next = re->next();
    delete re;
    re = next;
  }
  first_extension_ = NULL;
}


void RegisterExtension(Extension* that) {
  RegisteredExtension* extension = new RegisteredExtension(that);
  RegisteredExtension::Register(extension);
}


Extension::Extension(const char* name,
                     const char* source,
                     int dep_count,
                     const char** deps,
                     int source_length)
    : name_(name),
      source_length_(source_length >= 0 ?
                     source_length :
                     (source ? static_cast<int>(strlen(source)) : 0)),
      source_(source, source_length_),
      dep_count_(dep_count),
      deps_(deps),
      auto_enable_(false) {
  CHECK(source != NULL || source_length_ == 0);
}


ResourceConstraints::ResourceConstraints()
    : max_semi_space_size_(0),
      max_old_space_size_(0),
      max_executable_size_(0),
      stack_limit_(NULL),
      max_available_threads_(0),
      code_range_size_(0) { }

void ResourceConstraints::ConfigureDefaults(uint64_t physical_memory,
                                            uint64_t virtual_memory_limit,
                                            uint32_t number_of_processors) {
#if V8_OS_ANDROID
  // Android has higher physical memory requirements before raising the maximum
  // heap size limits since it has no swap space.
  const uint64_t low_limit = 512ul * i::MB;
  const uint64_t medium_limit = 1ul * i::GB;
  const uint64_t high_limit = 2ul * i::GB;
#else
  const uint64_t low_limit = 512ul * i::MB;
  const uint64_t medium_limit = 768ul * i::MB;
  const uint64_t high_limit = 1ul  * i::GB;
#endif

  if (physical_memory <= low_limit) {
    set_max_semi_space_size(i::Heap::kMaxSemiSpaceSizeLowMemoryDevice);
    set_max_old_space_size(i::Heap::kMaxOldSpaceSizeLowMemoryDevice);
    set_max_executable_size(i::Heap::kMaxExecutableSizeLowMemoryDevice);
  } else if (physical_memory <= medium_limit) {
    set_max_semi_space_size(i::Heap::kMaxSemiSpaceSizeMediumMemoryDevice);
    set_max_old_space_size(i::Heap::kMaxOldSpaceSizeMediumMemoryDevice);
    set_max_executable_size(i::Heap::kMaxExecutableSizeMediumMemoryDevice);
  } else if (physical_memory <= high_limit) {
    set_max_semi_space_size(i::Heap::kMaxSemiSpaceSizeHighMemoryDevice);
    set_max_old_space_size(i::Heap::kMaxOldSpaceSizeHighMemoryDevice);
    set_max_executable_size(i::Heap::kMaxExecutableSizeHighMemoryDevice);
  } else {
    set_max_semi_space_size(i::Heap::kMaxSemiSpaceSizeHugeMemoryDevice);
    set_max_old_space_size(i::Heap::kMaxOldSpaceSizeHugeMemoryDevice);
    set_max_executable_size(i::Heap::kMaxExecutableSizeHugeMemoryDevice);
  }

  set_max_available_threads(i::Max(i::Min(number_of_processors, 4u), 1u));

  if (virtual_memory_limit > 0 && i::kRequiresCodeRange) {
    // Reserve no more than 1/8 of the memory for the code range, but at most
    // kMaximalCodeRangeSize.
    set_code_range_size(
        i::Min(i::kMaximalCodeRangeSize / i::MB,
               static_cast<size_t>((virtual_memory_limit >> 3) / i::MB)));
  }
}


void SetResourceConstraints(i::Isolate* isolate,
                            const ResourceConstraints& constraints) {
  int semi_space_size = constraints.max_semi_space_size();
  int old_space_size = constraints.max_old_space_size();
  int max_executable_size = constraints.max_executable_size();
  size_t code_range_size = constraints.code_range_size();
  if (semi_space_size != 0 || old_space_size != 0 ||
      max_executable_size != 0 || code_range_size != 0) {
    isolate->heap()->ConfigureHeap(semi_space_size, old_space_size,
                                   max_executable_size, code_range_size);
  }
  if (constraints.stack_limit() != NULL) {
    uintptr_t limit = reinterpret_cast<uintptr_t>(constraints.stack_limit());
    isolate->stack_guard()->SetStackLimit(limit);
  }

  isolate->set_max_available_threads(constraints.max_available_threads());
}


i::Object** V8::GlobalizeReference(i::Isolate* isolate, i::Object** obj) {
  LOG_API(isolate, "Persistent::New");
  i::Handle<i::Object> result = isolate->global_handles()->Create(*obj);
#ifdef VERIFY_HEAP
  if (i::FLAG_verify_heap) {
    (*obj)->ObjectVerify();
  }
#endif  // VERIFY_HEAP
  return result.location();
}


i::Object** V8::CopyPersistent(i::Object** obj) {
  i::Handle<i::Object> result = i::GlobalHandles::CopyGlobal(obj);
#ifdef VERIFY_HEAP
  if (i::FLAG_verify_heap) {
    (*obj)->ObjectVerify();
  }
#endif  // VERIFY_HEAP
  return result.location();
}


void V8::MakeWeak(i::Object** object, void* parameter,
                  WeakCallback weak_callback) {
  i::GlobalHandles::MakeWeak(object, parameter, weak_callback);
}


void V8::MakeWeak(i::Object** object, void* parameter,
                  int internal_field_index1, int internal_field_index2,
                  WeakCallbackInfo<void>::Callback weak_callback) {
  WeakCallbackType type = WeakCallbackType::kParameter;
  if (internal_field_index1 == 0) {
    if (internal_field_index2 == 1) {
      type = WeakCallbackType::kInternalFields;
    } else {
      DCHECK_EQ(internal_field_index2, -1);
      type = WeakCallbackType::kInternalFields;
    }
  } else {
    DCHECK_EQ(internal_field_index1, -1);
    DCHECK_EQ(internal_field_index2, -1);
  }
  i::GlobalHandles::MakeWeak(object, parameter, weak_callback, type);
}


void V8::MakeWeak(i::Object** object, void* parameter,
                  WeakCallbackInfo<void>::Callback weak_callback,
                  WeakCallbackType type) {
  i::GlobalHandles::MakeWeak(object, parameter, weak_callback, type);
}


void* V8::ClearWeak(i::Object** obj) {
  return i::GlobalHandles::ClearWeakness(obj);
}


void V8::DisposeGlobal(i::Object** obj) {
  i::GlobalHandles::Destroy(obj);
}


void V8::Eternalize(Isolate* v8_isolate, Value* value, int* index) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::Object* object = *Utils::OpenHandle(value);
  isolate->eternal_handles()->Create(isolate, object, index);
}


Local<Value> V8::GetEternal(Isolate* v8_isolate, int index) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return Utils::ToLocal(isolate->eternal_handles()->Get(index));
}


void V8::CheckIsJust(bool is_just) {
  Utils::ApiCheck(is_just, "v8::FromJust", "Maybe value is Nothing.");
}


// --- H a n d l e s ---


HandleScope::HandleScope(Isolate* isolate) {
  Initialize(isolate);
}


void HandleScope::Initialize(Isolate* isolate) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  // We do not want to check the correct usage of the Locker class all over the
  // place, so we do it only here: Without a HandleScope, an embedder can do
  // almost nothing, so it is enough to check in this central place.
  // We make an exception if the serializer is enabled, which means that the
  // Isolate is exclusively used to create a snapshot.
  Utils::ApiCheck(
      !v8::Locker::IsActive() ||
          internal_isolate->thread_manager()->IsLockedByCurrentThread() ||
          internal_isolate->serializer_enabled(),
      "HandleScope::HandleScope",
      "Entering the V8 API without proper locking in place");
  i::HandleScopeData* current = internal_isolate->handle_scope_data();
  isolate_ = internal_isolate;
  prev_next_ = current->next;
  prev_limit_ = current->limit;
  current->level++;
}


HandleScope::~HandleScope() {
  i::HandleScope::CloseScope(isolate_, prev_next_, prev_limit_);
}


int HandleScope::NumberOfHandles(Isolate* isolate) {
  return i::HandleScope::NumberOfHandles(
      reinterpret_cast<i::Isolate*>(isolate));
}


i::Object** HandleScope::CreateHandle(i::Isolate* isolate, i::Object* value) {
  return i::HandleScope::CreateHandle(isolate, value);
}


i::Object** HandleScope::CreateHandle(i::HeapObject* heap_object,
                                      i::Object* value) {
  DCHECK(heap_object->IsHeapObject());
  return i::HandleScope::CreateHandle(heap_object->GetIsolate(), value);
}


EscapableHandleScope::EscapableHandleScope(Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  escape_slot_ = CreateHandle(isolate, isolate->heap()->the_hole_value());
  Initialize(v8_isolate);
}


i::Object** EscapableHandleScope::Escape(i::Object** escape_value) {
  i::Heap* heap = reinterpret_cast<i::Isolate*>(GetIsolate())->heap();
  Utils::ApiCheck(*escape_slot_ == heap->the_hole_value(),
                  "EscapeableHandleScope::Escape",
                  "Escape value set twice");
  if (escape_value == NULL) {
    *escape_slot_ = heap->undefined_value();
    return NULL;
  }
  *escape_slot_ = *escape_value;
  return escape_slot_;
}


void Context::Enter() {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  i::Isolate* isolate = env->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  impl->EnterContext(env);
  impl->SaveContext(isolate->context());
  isolate->set_context(*env);
}


void Context::Exit() {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  i::Isolate* isolate = env->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  if (!Utils::ApiCheck(impl->LastEnteredContextWas(env),
                       "v8::Context::Exit()",
                       "Cannot exit non-entered context")) {
    return;
  }
  impl->LeaveContext();
  isolate->set_context(impl->RestoreContext());
}


static void* DecodeSmiToAligned(i::Object* value, const char* location) {
  Utils::ApiCheck(value->IsSmi(), location, "Not a Smi");
  return reinterpret_cast<void*>(value);
}


static i::Smi* EncodeAlignedAsSmi(void* value, const char* location) {
  i::Smi* smi = reinterpret_cast<i::Smi*>(value);
  Utils::ApiCheck(smi->IsSmi(), location, "Pointer is not aligned");
  return smi;
}


static i::Handle<i::FixedArray> EmbedderDataFor(Context* context,
                                                int index,
                                                bool can_grow,
                                                const char* location) {
  i::Handle<i::Context> env = Utils::OpenHandle(context);
  bool ok =
      Utils::ApiCheck(env->IsNativeContext(),
                      location,
                      "Not a native context") &&
      Utils::ApiCheck(index >= 0, location, "Negative index");
  if (!ok) return i::Handle<i::FixedArray>();
  i::Handle<i::FixedArray> data(env->embedder_data());
  if (index < data->length()) return data;
  if (!Utils::ApiCheck(can_grow, location, "Index too large")) {
    return i::Handle<i::FixedArray>();
  }
  int new_size = i::Max(index, data->length() << 1) + 1;
  data = i::FixedArray::CopySize(data, new_size);
  env->set_embedder_data(*data);
  return data;
}


v8::Local<v8::Value> Context::SlowGetEmbedderData(int index) {
  const char* location = "v8::Context::GetEmbedderData()";
  i::Handle<i::FixedArray> data = EmbedderDataFor(this, index, false, location);
  if (data.is_null()) return Local<Value>();
  i::Handle<i::Object> result(data->get(index), data->GetIsolate());
  return Utils::ToLocal(result);
}


void Context::SetEmbedderData(int index, v8::Handle<Value> value) {
  const char* location = "v8::Context::SetEmbedderData()";
  i::Handle<i::FixedArray> data = EmbedderDataFor(this, index, true, location);
  if (data.is_null()) return;
  i::Handle<i::Object> val = Utils::OpenHandle(*value);
  data->set(index, *val);
  DCHECK_EQ(*Utils::OpenHandle(*value),
            *Utils::OpenHandle(*GetEmbedderData(index)));
}


void* Context::SlowGetAlignedPointerFromEmbedderData(int index) {
  const char* location = "v8::Context::GetAlignedPointerFromEmbedderData()";
  i::Handle<i::FixedArray> data = EmbedderDataFor(this, index, false, location);
  if (data.is_null()) return NULL;
  return DecodeSmiToAligned(data->get(index), location);
}


void Context::SetAlignedPointerInEmbedderData(int index, void* value) {
  const char* location = "v8::Context::SetAlignedPointerInEmbedderData()";
  i::Handle<i::FixedArray> data = EmbedderDataFor(this, index, true, location);
  data->set(index, EncodeAlignedAsSmi(value, location));
  DCHECK_EQ(value, GetAlignedPointerFromEmbedderData(index));
}


// --- N e a n d e r ---


// A constructor cannot easily return an error value, therefore it is necessary
// to check for a dead VM with ON_BAILOUT before constructing any Neander
// objects.  To remind you about this there is no HandleScope in the
// NeanderObject constructor.  When you add one to the site calling the
// constructor you should check that you ensured the VM was not dead first.
NeanderObject::NeanderObject(v8::internal::Isolate* isolate, int size) {
  ENTER_V8(isolate);
  value_ = isolate->factory()->NewNeanderObject();
  i::Handle<i::FixedArray> elements = isolate->factory()->NewFixedArray(size);
  value_->set_elements(*elements);
}


int NeanderObject::size() {
  return i::FixedArray::cast(value_->elements())->length();
}


NeanderArray::NeanderArray(v8::internal::Isolate* isolate) : obj_(isolate, 2) {
  obj_.set(0, i::Smi::FromInt(0));
}


int NeanderArray::length() {
  return i::Smi::cast(obj_.get(0))->value();
}


i::Object* NeanderArray::get(int offset) {
  DCHECK(0 <= offset);
  DCHECK(offset < length());
  return obj_.get(offset + 1);
}


// This method cannot easily return an error value, therefore it is necessary
// to check for a dead VM with ON_BAILOUT before calling it.  To remind you
// about this there is no HandleScope in this method.  When you add one to the
// site calling this method you should check that you ensured the VM was not
// dead first.
void NeanderArray::add(i::Isolate* isolate, i::Handle<i::Object> value) {
  int length = this->length();
  int size = obj_.size();
  if (length == size - 1) {
    i::Factory* factory = isolate->factory();
    i::Handle<i::FixedArray> new_elms = factory->NewFixedArray(2 * size);
    for (int i = 0; i < length; i++)
      new_elms->set(i + 1, get(i));
    obj_.value()->set_elements(*new_elms);
  }
  obj_.set(length + 1, *value);
  obj_.set(0, i::Smi::FromInt(length + 1));
}


void NeanderArray::set(int index, i::Object* value) {
  if (index < 0 || index >= this->length()) return;
  obj_.set(index + 1, value);
}


// --- T e m p l a t e ---


static void InitializeTemplate(i::Handle<i::TemplateInfo> that, int type) {
  that->set_tag(i::Smi::FromInt(type));
}


void Template::Set(v8::Handle<Name> name,
                   v8::Handle<Data> value,
                   v8::PropertyAttribute attribute) {
  auto templ = Utils::OpenHandle(this);
  i::Isolate* isolate = templ->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  // TODO(dcarney): split api to allow values of v8::Value or v8::TemplateInfo.
  i::ApiNatives::AddDataProperty(isolate, templ, Utils::OpenHandle(*name),
                                 Utils::OpenHandle(*value),
                                 static_cast<PropertyAttributes>(attribute));
}


void Template::SetAccessorProperty(
    v8::Local<v8::Name> name,
    v8::Local<FunctionTemplate> getter,
    v8::Local<FunctionTemplate> setter,
    v8::PropertyAttribute attribute,
    v8::AccessControl access_control) {
  // TODO(verwaest): Remove |access_control|.
  DCHECK_EQ(v8::DEFAULT, access_control);
  auto templ = Utils::OpenHandle(this);
  auto isolate = templ->GetIsolate();
  ENTER_V8(isolate);
  DCHECK(!name.IsEmpty());
  DCHECK(!getter.IsEmpty() || !setter.IsEmpty());
  i::HandleScope scope(isolate);
  i::ApiNatives::AddAccessorProperty(
      isolate, templ, Utils::OpenHandle(*name),
      Utils::OpenHandle(*getter, true), Utils::OpenHandle(*setter, true),
      static_cast<PropertyAttributes>(attribute));
}


// --- F u n c t i o n   T e m p l a t e ---
static void InitializeFunctionTemplate(
    i::Handle<i::FunctionTemplateInfo> info) {
  info->set_tag(i::Smi::FromInt(Consts::FUNCTION_TEMPLATE));
  info->set_flag(0);
}


Local<ObjectTemplate> FunctionTemplate::PrototypeTemplate() {
  i::Isolate* i_isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(i_isolate);
  i::Handle<i::Object> result(Utils::OpenHandle(this)->prototype_template(),
                              i_isolate);
  if (result->IsUndefined()) {
    v8::Isolate* isolate = reinterpret_cast<v8::Isolate*>(i_isolate);
    result = Utils::OpenHandle(*ObjectTemplate::New(isolate));
    Utils::OpenHandle(this)->set_prototype_template(*result);
  }
  return ToApiHandle<ObjectTemplate>(result);
}


static void EnsureNotInstantiated(i::Handle<i::FunctionTemplateInfo> info,
                                  const char* func) {
  Utils::ApiCheck(!info->instantiated(), func,
                  "FunctionTemplate already instantiated");
}


void FunctionTemplate::Inherit(v8::Handle<FunctionTemplate> value) {
  auto info = Utils::OpenHandle(this);
  EnsureNotInstantiated(info, "v8::FunctionTemplate::Inherit");
  i::Isolate* isolate = info->GetIsolate();
  ENTER_V8(isolate);
  info->set_parent_template(*Utils::OpenHandle(*value));
}


static Local<FunctionTemplate> FunctionTemplateNew(
    i::Isolate* isolate,
    FunctionCallback callback,
    v8::Handle<Value> data,
    v8::Handle<Signature> signature,
    int length,
    bool do_not_cache) {
  i::Handle<i::Struct> struct_obj =
      isolate->factory()->NewStruct(i::FUNCTION_TEMPLATE_INFO_TYPE);
  i::Handle<i::FunctionTemplateInfo> obj =
      i::Handle<i::FunctionTemplateInfo>::cast(struct_obj);
  InitializeFunctionTemplate(obj);
  obj->set_do_not_cache(do_not_cache);
  int next_serial_number = 0;
  if (!do_not_cache) {
    next_serial_number = isolate->next_serial_number() + 1;
    isolate->set_next_serial_number(next_serial_number);
  }
  obj->set_serial_number(i::Smi::FromInt(next_serial_number));
  if (callback != 0) {
    if (data.IsEmpty()) {
      data = v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
    }
    Utils::ToLocal(obj)->SetCallHandler(callback, data);
  }
  obj->set_length(length);
  obj->set_undetectable(false);
  obj->set_needs_access_check(false);
  if (!signature.IsEmpty())
    obj->set_signature(*Utils::OpenHandle(*signature));
  return Utils::ToLocal(obj);
}

Local<FunctionTemplate> FunctionTemplate::New(
    Isolate* isolate,
    FunctionCallback callback,
    v8::Handle<Value> data,
    v8::Handle<Signature> signature,
    int length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  // Changes to the environment cannot be captured in the snapshot. Expect no
  // function templates when the isolate is created for serialization.
  DCHECK(!i_isolate->serializer_enabled());
  LOG_API(i_isolate, "FunctionTemplate::New");
  ENTER_V8(i_isolate);
  return FunctionTemplateNew(
      i_isolate, callback, data, signature, length, false);
}


Local<Signature> Signature::New(Isolate* isolate,
                                Handle<FunctionTemplate> receiver) {
  return Utils::SignatureToLocal(Utils::OpenHandle(*receiver));
}


Local<AccessorSignature> AccessorSignature::New(
    Isolate* isolate,
    Handle<FunctionTemplate> receiver) {
  return Utils::AccessorSignatureToLocal(Utils::OpenHandle(*receiver));
}


Local<TypeSwitch> TypeSwitch::New(Handle<FunctionTemplate> type) {
  Handle<FunctionTemplate> types[1] = { type };
  return TypeSwitch::New(1, types);
}


Local<TypeSwitch> TypeSwitch::New(int argc, Handle<FunctionTemplate> types[]) {
  i::Isolate* isolate = i::Isolate::Current();
  LOG_API(isolate, "TypeSwitch::New");
  ENTER_V8(isolate);
  i::Handle<i::FixedArray> vector = isolate->factory()->NewFixedArray(argc);
  for (int i = 0; i < argc; i++)
    vector->set(i, *Utils::OpenHandle(*types[i]));
  i::Handle<i::Struct> struct_obj =
      isolate->factory()->NewStruct(i::TYPE_SWITCH_INFO_TYPE);
  i::Handle<i::TypeSwitchInfo> obj =
      i::Handle<i::TypeSwitchInfo>::cast(struct_obj);
  obj->set_types(*vector);
  return Utils::ToLocal(obj);
}


int TypeSwitch::match(v8::Handle<Value> value) {
  i::Handle<i::TypeSwitchInfo> info = Utils::OpenHandle(this);
  LOG_API(info->GetIsolate(), "TypeSwitch::match");
  i::Handle<i::Object> obj = Utils::OpenHandle(*value);
  i::FixedArray* types = i::FixedArray::cast(info->types());
  for (int i = 0; i < types->length(); i++) {
    if (i::FunctionTemplateInfo::cast(types->get(i))->IsTemplateFor(*obj))
      return i + 1;
  }
  return 0;
}


#define SET_FIELD_WRAPPED(obj, setter, cdata) do {                      \
    i::Handle<i::Object> foreign = FromCData(obj->GetIsolate(), cdata); \
    (obj)->setter(*foreign);                                            \
  } while (false)


void FunctionTemplate::SetCallHandler(FunctionCallback callback,
                                      v8::Handle<Value> data) {
  auto info = Utils::OpenHandle(this);
  EnsureNotInstantiated(info, "v8::FunctionTemplate::SetCallHandler");
  i::Isolate* isolate = info->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::Struct> struct_obj =
      isolate->factory()->NewStruct(i::CALL_HANDLER_INFO_TYPE);
  i::Handle<i::CallHandlerInfo> obj =
      i::Handle<i::CallHandlerInfo>::cast(struct_obj);
  SET_FIELD_WRAPPED(obj, set_callback, callback);
  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  obj->set_data(*Utils::OpenHandle(*data));
  info->set_call_code(*obj);
}


static i::Handle<i::AccessorInfo> SetAccessorInfoProperties(
    i::Handle<i::AccessorInfo> obj,
    v8::Handle<Name> name,
    v8::AccessControl settings,
    v8::PropertyAttribute attributes,
    v8::Handle<AccessorSignature> signature) {
  obj->set_name(*Utils::OpenHandle(*name));
  if (settings & ALL_CAN_READ) obj->set_all_can_read(true);
  if (settings & ALL_CAN_WRITE) obj->set_all_can_write(true);
  obj->set_property_attributes(static_cast<PropertyAttributes>(attributes));
  if (!signature.IsEmpty()) {
    obj->set_expected_receiver_type(*Utils::OpenHandle(*signature));
  }
  return obj;
}


template<typename Getter, typename Setter>
static i::Handle<i::AccessorInfo> MakeAccessorInfo(
    v8::Handle<Name> name,
    Getter getter,
    Setter setter,
    v8::Handle<Value> data,
    v8::AccessControl settings,
    v8::PropertyAttribute attributes,
    v8::Handle<AccessorSignature> signature) {
  i::Isolate* isolate = Utils::OpenHandle(*name)->GetIsolate();
  i::Handle<i::ExecutableAccessorInfo> obj =
      isolate->factory()->NewExecutableAccessorInfo();
  SET_FIELD_WRAPPED(obj, set_getter, getter);
  SET_FIELD_WRAPPED(obj, set_setter, setter);
  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  obj->set_data(*Utils::OpenHandle(*data));
  return SetAccessorInfoProperties(obj, name, settings, attributes, signature);
}


Local<ObjectTemplate> FunctionTemplate::InstanceTemplate() {
  i::Handle<i::FunctionTemplateInfo> handle = Utils::OpenHandle(this, true);
  if (!Utils::ApiCheck(!handle.is_null(),
                       "v8::FunctionTemplate::InstanceTemplate()",
                       "Reading from empty handle")) {
    return Local<ObjectTemplate>();
  }
  i::Isolate* isolate = handle->GetIsolate();
  ENTER_V8(isolate);
  if (handle->instance_template()->IsUndefined()) {
    Local<ObjectTemplate> templ =
        ObjectTemplate::New(isolate, ToApiHandle<FunctionTemplate>(handle));
    handle->set_instance_template(*Utils::OpenHandle(*templ));
  }
  i::Handle<i::ObjectTemplateInfo> result(
      i::ObjectTemplateInfo::cast(handle->instance_template()));
  return Utils::ToLocal(result);
}


void FunctionTemplate::SetLength(int length) {
  auto info = Utils::OpenHandle(this);
  EnsureNotInstantiated(info, "v8::FunctionTemplate::SetLength");
  auto isolate = info->GetIsolate();
  ENTER_V8(isolate);
  info->set_length(length);
}


void FunctionTemplate::SetClassName(Handle<String> name) {
  auto info = Utils::OpenHandle(this);
  EnsureNotInstantiated(info, "v8::FunctionTemplate::SetClassName");
  auto isolate = info->GetIsolate();
  ENTER_V8(isolate);
  info->set_class_name(*Utils::OpenHandle(*name));
}


void FunctionTemplate::SetHiddenPrototype(bool value) {
  auto info = Utils::OpenHandle(this);
  EnsureNotInstantiated(info, "v8::FunctionTemplate::SetHiddenPrototype");
  auto isolate = info->GetIsolate();
  ENTER_V8(isolate);
  info->set_hidden_prototype(value);
}


void FunctionTemplate::ReadOnlyPrototype() {
  auto info = Utils::OpenHandle(this);
  EnsureNotInstantiated(info, "v8::FunctionTemplate::ReadOnlyPrototype");
  auto isolate = info->GetIsolate();
  ENTER_V8(isolate);
  info->set_read_only_prototype(true);
}


void FunctionTemplate::RemovePrototype() {
  auto info = Utils::OpenHandle(this);
  EnsureNotInstantiated(info, "v8::FunctionTemplate::RemovePrototype");
  auto isolate = info->GetIsolate();
  ENTER_V8(isolate);
  info->set_remove_prototype(true);
}


// --- O b j e c t T e m p l a t e ---


Local<ObjectTemplate> ObjectTemplate::New(Isolate* isolate) {
  return New(reinterpret_cast<i::Isolate*>(isolate), Local<FunctionTemplate>());
}


Local<ObjectTemplate> ObjectTemplate::New() {
  return New(i::Isolate::Current(), Local<FunctionTemplate>());
}


Local<ObjectTemplate> ObjectTemplate::New(
    i::Isolate* isolate,
    v8::Handle<FunctionTemplate> constructor) {
  // Changes to the environment cannot be captured in the snapshot. Expect no
  // object templates when the isolate is created for serialization.
  DCHECK(!isolate->serializer_enabled());
  LOG_API(isolate, "ObjectTemplate::New");
  ENTER_V8(isolate);
  i::Handle<i::Struct> struct_obj =
      isolate->factory()->NewStruct(i::OBJECT_TEMPLATE_INFO_TYPE);
  i::Handle<i::ObjectTemplateInfo> obj =
      i::Handle<i::ObjectTemplateInfo>::cast(struct_obj);
  InitializeTemplate(obj, Consts::OBJECT_TEMPLATE);
  if (!constructor.IsEmpty())
    obj->set_constructor(*Utils::OpenHandle(*constructor));
  obj->set_internal_field_count(i::Smi::FromInt(0));
  return Utils::ToLocal(obj);
}


// Ensure that the object template has a constructor.  If no
// constructor is available we create one.
static i::Handle<i::FunctionTemplateInfo> EnsureConstructor(
    i::Isolate* isolate,
    ObjectTemplate* object_template) {
  i::Object* obj = Utils::OpenHandle(object_template)->constructor();
  if (!obj ->IsUndefined()) {
    i::FunctionTemplateInfo* info = i::FunctionTemplateInfo::cast(obj);
    return i::Handle<i::FunctionTemplateInfo>(info, isolate);
  }
  Local<FunctionTemplate> templ =
      FunctionTemplate::New(reinterpret_cast<Isolate*>(isolate));
  i::Handle<i::FunctionTemplateInfo> constructor = Utils::OpenHandle(*templ);
  constructor->set_instance_template(*Utils::OpenHandle(object_template));
  Utils::OpenHandle(object_template)->set_constructor(*constructor);
  return constructor;
}


static inline i::Handle<i::TemplateInfo> GetTemplateInfo(
    i::Isolate* isolate,
    Template* template_obj) {
  return Utils::OpenHandle(template_obj);
}


// TODO(dcarney): remove this with ObjectTemplate::SetAccessor
static inline i::Handle<i::TemplateInfo> GetTemplateInfo(
    i::Isolate* isolate,
    ObjectTemplate* object_template) {
  EnsureConstructor(isolate, object_template);
  return Utils::OpenHandle(object_template);
}


template<typename Getter, typename Setter, typename Data, typename Template>
static bool TemplateSetAccessor(
    Template* template_obj,
    v8::Local<Name> name,
    Getter getter,
    Setter setter,
    Data data,
    AccessControl settings,
    PropertyAttribute attribute,
    v8::Local<AccessorSignature> signature) {
  auto isolate = Utils::OpenHandle(template_obj)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  auto obj = MakeAccessorInfo(name, getter, setter, data, settings, attribute,
                              signature);
  if (obj.is_null()) return false;
  auto info = GetTemplateInfo(isolate, template_obj);
  i::ApiNatives::AddNativeDataProperty(isolate, info, obj);
  return true;
}


void Template::SetNativeDataProperty(v8::Local<String> name,
                                     AccessorGetterCallback getter,
                                     AccessorSetterCallback setter,
                                     v8::Handle<Value> data,
                                     PropertyAttribute attribute,
                                     v8::Local<AccessorSignature> signature,
                                     AccessControl settings) {
  TemplateSetAccessor(
      this, name, getter, setter, data, settings, attribute, signature);
}


void Template::SetNativeDataProperty(v8::Local<Name> name,
                                     AccessorNameGetterCallback getter,
                                     AccessorNameSetterCallback setter,
                                     v8::Handle<Value> data,
                                     PropertyAttribute attribute,
                                     v8::Local<AccessorSignature> signature,
                                     AccessControl settings) {
  TemplateSetAccessor(
      this, name, getter, setter, data, settings, attribute, signature);
}


void ObjectTemplate::SetAccessor(v8::Handle<String> name,
                                 AccessorGetterCallback getter,
                                 AccessorSetterCallback setter,
                                 v8::Handle<Value> data,
                                 AccessControl settings,
                                 PropertyAttribute attribute,
                                 v8::Handle<AccessorSignature> signature) {
  TemplateSetAccessor(
      this, name, getter, setter, data, settings, attribute, signature);
}


void ObjectTemplate::SetAccessor(v8::Handle<Name> name,
                                 AccessorNameGetterCallback getter,
                                 AccessorNameSetterCallback setter,
                                 v8::Handle<Value> data,
                                 AccessControl settings,
                                 PropertyAttribute attribute,
                                 v8::Handle<AccessorSignature> signature) {
  TemplateSetAccessor(
      this, name, getter, setter, data, settings, attribute, signature);
}


template <typename Getter, typename Setter, typename Query, typename Deleter,
          typename Enumerator>
static void ObjectTemplateSetNamedPropertyHandler(
    ObjectTemplate* templ, Getter getter, Setter setter, Query query,
    Deleter remover, Enumerator enumerator, Handle<Value> data,
    bool can_intercept_symbols, PropertyHandlerFlags flags) {
  i::Isolate* isolate = Utils::OpenHandle(templ)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  auto cons = EnsureConstructor(isolate, templ);
  EnsureNotInstantiated(cons, "ObjectTemplateSetNamedPropertyHandler");
  auto obj = i::Handle<i::InterceptorInfo>::cast(
      isolate->factory()->NewStruct(i::INTERCEPTOR_INFO_TYPE));

  if (getter != 0) SET_FIELD_WRAPPED(obj, set_getter, getter);
  if (setter != 0) SET_FIELD_WRAPPED(obj, set_setter, setter);
  if (query != 0) SET_FIELD_WRAPPED(obj, set_query, query);
  if (remover != 0) SET_FIELD_WRAPPED(obj, set_deleter, remover);
  if (enumerator != 0) SET_FIELD_WRAPPED(obj, set_enumerator, enumerator);
  obj->set_flags(0);
  obj->set_can_intercept_symbols(can_intercept_symbols);
  obj->set_all_can_read(static_cast<int>(flags) &
                        static_cast<int>(PropertyHandlerFlags::kAllCanRead));

  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  obj->set_data(*Utils::OpenHandle(*data));
  cons->set_named_property_handler(*obj);
}


void ObjectTemplate::SetNamedPropertyHandler(
    NamedPropertyGetterCallback getter, NamedPropertySetterCallback setter,
    NamedPropertyQueryCallback query, NamedPropertyDeleterCallback remover,
    NamedPropertyEnumeratorCallback enumerator, Handle<Value> data) {
  ObjectTemplateSetNamedPropertyHandler(this, getter, setter, query, remover,
                                        enumerator, data, false,
                                        PropertyHandlerFlags::kNone);
}


void ObjectTemplate::SetHandler(
    const NamedPropertyHandlerConfiguration& config) {
  ObjectTemplateSetNamedPropertyHandler(
      this, config.getter, config.setter, config.query, config.deleter,
      config.enumerator, config.data, true, config.flags);
}


void ObjectTemplate::MarkAsUndetectable() {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  auto cons = EnsureConstructor(isolate, this);
  EnsureNotInstantiated(cons, "v8::ObjectTemplate::MarkAsUndetectable");
  cons->set_undetectable(true);
}


void ObjectTemplate::SetAccessCheckCallbacks(
    NamedSecurityCallback named_callback,
    IndexedSecurityCallback indexed_callback,
    Handle<Value> data,
    bool turned_on_by_default) {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  auto cons = EnsureConstructor(isolate, this);
  EnsureNotInstantiated(cons, "v8::ObjectTemplate::SetAccessCheckCallbacks");

  i::Handle<i::Struct> struct_info =
      isolate->factory()->NewStruct(i::ACCESS_CHECK_INFO_TYPE);
  i::Handle<i::AccessCheckInfo> info =
      i::Handle<i::AccessCheckInfo>::cast(struct_info);

  SET_FIELD_WRAPPED(info, set_named_callback, named_callback);
  SET_FIELD_WRAPPED(info, set_indexed_callback, indexed_callback);

  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  info->set_data(*Utils::OpenHandle(*data));

  cons->set_access_check_info(*info);
  cons->set_needs_access_check(turned_on_by_default);
}


void ObjectTemplate::SetHandler(
    const IndexedPropertyHandlerConfiguration& config) {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  auto cons = EnsureConstructor(isolate, this);
  EnsureNotInstantiated(cons, "v8::ObjectTemplate::SetHandler");
  auto obj = i::Handle<i::InterceptorInfo>::cast(
      isolate->factory()->NewStruct(i::INTERCEPTOR_INFO_TYPE));

  if (config.getter != 0) SET_FIELD_WRAPPED(obj, set_getter, config.getter);
  if (config.setter != 0) SET_FIELD_WRAPPED(obj, set_setter, config.setter);
  if (config.query != 0) SET_FIELD_WRAPPED(obj, set_query, config.query);
  if (config.deleter != 0) SET_FIELD_WRAPPED(obj, set_deleter, config.deleter);
  if (config.enumerator != 0) {
    SET_FIELD_WRAPPED(obj, set_enumerator, config.enumerator);
  }
  obj->set_flags(0);
  obj->set_all_can_read(static_cast<int>(config.flags) &
                        static_cast<int>(PropertyHandlerFlags::kAllCanRead));

  v8::Local<v8::Value> data = config.data;
  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  obj->set_data(*Utils::OpenHandle(*data));
  cons->set_indexed_property_handler(*obj);
}


void ObjectTemplate::SetCallAsFunctionHandler(FunctionCallback callback,
                                              Handle<Value> data) {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  auto cons = EnsureConstructor(isolate, this);
  EnsureNotInstantiated(cons, "v8::ObjectTemplate::SetCallAsFunctionHandler");
  i::Handle<i::Struct> struct_obj =
      isolate->factory()->NewStruct(i::CALL_HANDLER_INFO_TYPE);
  i::Handle<i::CallHandlerInfo> obj =
      i::Handle<i::CallHandlerInfo>::cast(struct_obj);
  SET_FIELD_WRAPPED(obj, set_callback, callback);
  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  obj->set_data(*Utils::OpenHandle(*data));
  cons->set_instance_call_handler(*obj);
}


int ObjectTemplate::InternalFieldCount() {
  return i::Smi::cast(Utils::OpenHandle(this)->internal_field_count())->value();
}


void ObjectTemplate::SetInternalFieldCount(int value) {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  if (!Utils::ApiCheck(i::Smi::IsValid(value),
                       "v8::ObjectTemplate::SetInternalFieldCount()",
                       "Invalid internal field count")) {
    return;
  }
  ENTER_V8(isolate);
  if (value > 0) {
    // The internal field count is set by the constructor function's
    // construct code, so we ensure that there is a constructor
    // function to do the setting.
    EnsureConstructor(isolate, this);
  }
  Utils::OpenHandle(this)->set_internal_field_count(i::Smi::FromInt(value));
}


// --- S c r i p t s ---


// Internally, UnboundScript is a SharedFunctionInfo, and Script is a
// JSFunction.

ScriptCompiler::CachedData::CachedData(const uint8_t* data_, int length_,
                                       BufferPolicy buffer_policy_)
    : data(data_),
      length(length_),
      rejected(false),
      buffer_policy(buffer_policy_) {}


ScriptCompiler::CachedData::~CachedData() {
  if (buffer_policy == BufferOwned) {
    delete[] data;
  }
}


ScriptCompiler::StreamedSource::StreamedSource(ExternalSourceStream* stream,
                                               Encoding encoding)
    : impl_(new i::StreamedSource(stream, encoding)) {}


ScriptCompiler::StreamedSource::~StreamedSource() { delete impl_; }


const ScriptCompiler::CachedData*
ScriptCompiler::StreamedSource::GetCachedData() const {
  return impl_->cached_data.get();
}


Local<Script> UnboundScript::BindToCurrentContext() {
  i::Handle<i::HeapObject> obj =
      i::Handle<i::HeapObject>::cast(Utils::OpenHandle(this));
  i::Handle<i::SharedFunctionInfo>
      function_info(i::SharedFunctionInfo::cast(*obj), obj->GetIsolate());
  i::Handle<i::JSFunction> function =
      obj->GetIsolate()->factory()->NewFunctionFromSharedFunctionInfo(
          function_info, obj->GetIsolate()->native_context());
  return ToApiHandle<Script>(function);
}


int UnboundScript::GetId() {
  i::Handle<i::HeapObject> obj =
      i::Handle<i::HeapObject>::cast(Utils::OpenHandle(this));
  i::Isolate* isolate = obj->GetIsolate();
  LOG_API(isolate, "v8::UnboundScript::GetId");
  i::HandleScope scope(isolate);
  i::Handle<i::SharedFunctionInfo> function_info(
      i::SharedFunctionInfo::cast(*obj));
  i::Handle<i::Script> script(i::Script::cast(function_info->script()));
  return script->id()->value();
}


int UnboundScript::GetLineNumber(int code_pos) {
  i::Handle<i::SharedFunctionInfo> obj =
      i::Handle<i::SharedFunctionInfo>::cast(Utils::OpenHandle(this));
  i::Isolate* isolate = obj->GetIsolate();
  LOG_API(isolate, "UnboundScript::GetLineNumber");
  if (obj->script()->IsScript()) {
    i::Handle<i::Script> script(i::Script::cast(obj->script()));
    return i::Script::GetLineNumber(script, code_pos);
  } else {
    return -1;
  }
}


Handle<Value> UnboundScript::GetScriptName() {
  i::Handle<i::SharedFunctionInfo> obj =
      i::Handle<i::SharedFunctionInfo>::cast(Utils::OpenHandle(this));
  i::Isolate* isolate = obj->GetIsolate();
  LOG_API(isolate, "UnboundScript::GetName");
  if (obj->script()->IsScript()) {
    i::Object* name = i::Script::cast(obj->script())->name();
    return Utils::ToLocal(i::Handle<i::Object>(name, isolate));
  } else {
    return Handle<String>();
  }
}


Handle<Value> UnboundScript::GetSourceURL() {
  i::Handle<i::SharedFunctionInfo> obj =
      i::Handle<i::SharedFunctionInfo>::cast(Utils::OpenHandle(this));
  i::Isolate* isolate = obj->GetIsolate();
  LOG_API(isolate, "UnboundScript::GetSourceURL");
  if (obj->script()->IsScript()) {
    i::Object* url = i::Script::cast(obj->script())->source_url();
    return Utils::ToLocal(i::Handle<i::Object>(url, isolate));
  } else {
    return Handle<String>();
  }
}


Handle<Value> UnboundScript::GetSourceMappingURL() {
  i::Handle<i::SharedFunctionInfo> obj =
      i::Handle<i::SharedFunctionInfo>::cast(Utils::OpenHandle(this));
  i::Isolate* isolate = obj->GetIsolate();
  LOG_API(isolate, "UnboundScript::GetSourceMappingURL");
  if (obj->script()->IsScript()) {
    i::Object* url = i::Script::cast(obj->script())->source_mapping_url();
    return Utils::ToLocal(i::Handle<i::Object>(url, isolate));
  } else {
    return Handle<String>();
  }
}


MaybeLocal<Value> Script::Run(Local<Context> context) {
  PREPARE_FOR_EXECUTION_WITH_CALLBACK(context, "v8::Script::Run()", Value)
  i::AggregatingHistogramTimerScope timer(isolate->counters()->compile_lazy());
  i::TimerEventScope<i::TimerEventExecute> timer_scope(isolate);
  auto fun = i::Handle<i::JSFunction>::cast(Utils::OpenHandle(this));
  i::Handle<i::Object> receiver(isolate->global_proxy(), isolate);
  Local<Value> result;
  has_pending_exception =
      !ToLocal<Value>(i::Execution::Call(isolate, fun, receiver, 0, NULL),
                      &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<Value> Script::Run() {
  auto self = Utils::OpenHandle(this, true);
  // If execution is terminating, Compile(..)->Run() requires this
  // check.
  if (self.is_null()) return Local<Value>();
  auto context = ContextFromHeapObject(self);
  RETURN_TO_LOCAL_UNCHECKED(Run(context), Value);
}


Local<UnboundScript> Script::GetUnboundScript() {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  return ToApiHandle<UnboundScript>(
      i::Handle<i::SharedFunctionInfo>(i::JSFunction::cast(*obj)->shared()));
}


MaybeLocal<UnboundScript> ScriptCompiler::CompileUnboundInternal(
    Isolate* v8_isolate, Source* source, CompileOptions options,
    bool is_module) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  PREPARE_FOR_EXECUTION_WITH_ISOLATE(
      isolate, "v8::ScriptCompiler::CompileUnbound()", UnboundScript);

  // Support the old API for a transition period:
  // - kProduceToCache -> kProduceParserCache
  // - kNoCompileOptions + cached_data != NULL -> kConsumeParserCache
  if (options == kProduceDataToCache) {
    options = kProduceParserCache;
  } else if (options == kNoCompileOptions && source->cached_data) {
    options = kConsumeParserCache;
  }

  // Don't try to produce any kind of cache when the debugger is loaded.
  if (isolate->debug()->is_loaded() &&
      (options == kProduceParserCache || options == kProduceCodeCache)) {
    options = kNoCompileOptions;
  }

  i::ScriptData* script_data = NULL;
  if (options == kConsumeParserCache || options == kConsumeCodeCache) {
    DCHECK(source->cached_data);
    // ScriptData takes care of pointer-aligning the data.
    script_data = new i::ScriptData(source->cached_data->data,
                                    source->cached_data->length);
  }

  i::Handle<i::String> str = Utils::OpenHandle(*(source->source_string));
  i::SharedFunctionInfo* raw_result = NULL;
  { i::HandleScope scope(isolate);
    i::HistogramTimerScope total(isolate->counters()->compile_script(), true);
    i::Handle<i::Object> name_obj;
    i::Handle<i::Object> source_map_url;
    int line_offset = 0;
    int column_offset = 0;
    bool is_embedder_debug_script = false;
    bool is_shared_cross_origin = false;
    if (!source->resource_name.IsEmpty()) {
      name_obj = Utils::OpenHandle(*(source->resource_name));
    }
    if (!source->resource_line_offset.IsEmpty()) {
      line_offset = static_cast<int>(source->resource_line_offset->Value());
    }
    if (!source->resource_column_offset.IsEmpty()) {
      column_offset =
          static_cast<int>(source->resource_column_offset->Value());
    }
    if (!source->resource_is_shared_cross_origin.IsEmpty()) {
      is_shared_cross_origin =
          source->resource_is_shared_cross_origin->IsTrue();
    }
    if (!source->resource_is_embedder_debug_script.IsEmpty()) {
      is_embedder_debug_script =
          source->resource_is_embedder_debug_script->IsTrue();
    }
    if (!source->source_map_url.IsEmpty()) {
      source_map_url = Utils::OpenHandle(*(source->source_map_url));
    }
    i::Handle<i::SharedFunctionInfo> result = i::Compiler::CompileScript(
        str, name_obj, line_offset, column_offset, is_embedder_debug_script,
        is_shared_cross_origin, source_map_url, isolate->native_context(), NULL,
        &script_data, options, i::NOT_NATIVES_CODE, is_module);
    has_pending_exception = result.is_null();
    if (has_pending_exception && script_data != NULL) {
      // This case won't happen during normal operation; we have compiled
      // successfully and produced cached data, and but the second compilation
      // of the same source code fails.
      delete script_data;
      script_data = NULL;
    }
    RETURN_ON_FAILED_EXECUTION(UnboundScript);
    raw_result = *result;

    if ((options == kProduceParserCache || options == kProduceCodeCache) &&
        script_data != NULL) {
      // script_data now contains the data that was generated. source will
      // take the ownership.
      source->cached_data = new CachedData(
          script_data->data(), script_data->length(), CachedData::BufferOwned);
      script_data->ReleaseDataOwnership();
    } else if (options == kConsumeParserCache || options == kConsumeCodeCache) {
      source->cached_data->rejected = script_data->rejected();
    }
    delete script_data;
  }
  i::Handle<i::SharedFunctionInfo> result(raw_result, isolate);
  RETURN_ESCAPED(ToApiHandle<UnboundScript>(result));
}


MaybeLocal<UnboundScript> ScriptCompiler::CompileUnboundScript(
    Isolate* v8_isolate, Source* source, CompileOptions options) {
  return CompileUnboundInternal(v8_isolate, source, options, false);
}


Local<UnboundScript> ScriptCompiler::CompileUnbound(Isolate* v8_isolate,
                                                    Source* source,
                                                    CompileOptions options) {
  RETURN_TO_LOCAL_UNCHECKED(
      CompileUnboundInternal(v8_isolate, source, options, false),
      UnboundScript);
}


MaybeLocal<Script> ScriptCompiler::Compile(Local<Context> context,
                                           Source* source,
                                           CompileOptions options) {
  auto isolate = context->GetIsolate();
  auto maybe = CompileUnboundInternal(isolate, source, options, false);
  Local<UnboundScript> result;
  if (!maybe.ToLocal(&result)) return MaybeLocal<Script>();
  v8::Context::Scope scope(context);
  return result->BindToCurrentContext();
}


Local<Script> ScriptCompiler::Compile(
    Isolate* v8_isolate,
    Source* source,
    CompileOptions options) {
  auto context = v8_isolate->GetCurrentContext();
  RETURN_TO_LOCAL_UNCHECKED(Compile(context, source, options), Script);
}


MaybeLocal<Script> ScriptCompiler::CompileModule(Local<Context> context,
                                                 Source* source,
                                                 CompileOptions options) {
  CHECK(i::FLAG_harmony_modules);
  auto isolate = context->GetIsolate();
  auto maybe = CompileUnboundInternal(isolate, source, options, true);
  Local<UnboundScript> generic;
  if (!maybe.ToLocal(&generic)) return MaybeLocal<Script>();
  v8::Context::Scope scope(context);
  return generic->BindToCurrentContext();
}


Local<Script> ScriptCompiler::CompileModule(Isolate* v8_isolate, Source* source,
                                            CompileOptions options) {
  auto context = v8_isolate->GetCurrentContext();
  RETURN_TO_LOCAL_UNCHECKED(CompileModule(context, source, options), Script);
}


class IsIdentifierHelper {
 public:
  IsIdentifierHelper() : is_identifier_(false), first_char_(true) {}

  bool Check(i::String* string) {
    i::ConsString* cons_string = i::String::VisitFlat(this, string, 0);
    if (cons_string == NULL) return is_identifier_;
    // We don't support cons strings here.
    return false;
  }
  void VisitOneByteString(const uint8_t* chars, int length) {
    for (int i = 0; i < length; ++i) {
      if (first_char_) {
        first_char_ = false;
        is_identifier_ = unicode_cache_.IsIdentifierStart(chars[0]);
      } else {
        is_identifier_ &= unicode_cache_.IsIdentifierPart(chars[i]);
      }
    }
  }
  void VisitTwoByteString(const uint16_t* chars, int length) {
    for (int i = 0; i < length; ++i) {
      if (first_char_) {
        first_char_ = false;
        is_identifier_ = unicode_cache_.IsIdentifierStart(chars[0]);
      } else {
        is_identifier_ &= unicode_cache_.IsIdentifierPart(chars[i]);
      }
    }
  }

 private:
  bool is_identifier_;
  bool first_char_;
  i::UnicodeCache unicode_cache_;
  DISALLOW_COPY_AND_ASSIGN(IsIdentifierHelper);
};


MaybeLocal<Function> ScriptCompiler::CompileFunctionInContext(
    Local<Context> v8_context, Source* source, size_t arguments_count,
    Local<String> arguments[], size_t context_extension_count,
    Local<Object> context_extensions[]) {
  PREPARE_FOR_EXECUTION(
      v8_context, "v8::ScriptCompiler::CompileFunctionInContext()", Function);
  i::Handle<i::String> source_string;
  auto factory = isolate->factory();
  if (arguments_count) {
    source_string = factory->NewStringFromStaticChars("(function(");
    for (size_t i = 0; i < arguments_count; ++i) {
      IsIdentifierHelper helper;
      if (!helper.Check(*Utils::OpenHandle(*arguments[i]))) {
        return Local<Function>();
      }
      has_pending_exception =
          !factory->NewConsString(source_string,
                                  Utils::OpenHandle(*arguments[i]))
               .ToHandle(&source_string);
      RETURN_ON_FAILED_EXECUTION(Function);
      if (i + 1 == arguments_count) continue;
      has_pending_exception =
          !factory->NewConsString(source_string,
                                  factory->LookupSingleCharacterStringFromCode(
                                      ',')).ToHandle(&source_string);
      RETURN_ON_FAILED_EXECUTION(Function);
    }
    auto brackets = factory->NewStringFromStaticChars("){");
    has_pending_exception = !factory->NewConsString(source_string, brackets)
                                 .ToHandle(&source_string);
    RETURN_ON_FAILED_EXECUTION(Function);
  } else {
    source_string = factory->NewStringFromStaticChars("(function(){");
  }

  int scope_position = source_string->length();
  has_pending_exception =
      !factory->NewConsString(source_string,
                              Utils::OpenHandle(*source->source_string))
           .ToHandle(&source_string);
  RETURN_ON_FAILED_EXECUTION(Function);
  // Include \n in case the source contains a line end comment.
  auto brackets = factory->NewStringFromStaticChars("\n})");
  has_pending_exception =
      !factory->NewConsString(source_string, brackets).ToHandle(&source_string);
  RETURN_ON_FAILED_EXECUTION(Function);

  i::Handle<i::Context> context = Utils::OpenHandle(*v8_context);
  i::Handle<i::SharedFunctionInfo> outer_info(context->closure()->shared(),
                                              isolate);
  for (size_t i = 0; i < context_extension_count; ++i) {
    i::Handle<i::JSObject> extension =
        Utils::OpenHandle(*context_extensions[i]);
    i::Handle<i::JSFunction> closure(context->closure(), isolate);
    context = factory->NewWithContext(closure, context, extension);
  }

  i::Handle<i::JSFunction> fun;
  has_pending_exception =
      !i::Compiler::GetFunctionFromEval(
           source_string, outer_info, context, i::SLOPPY,
           i::ONLY_SINGLE_FUNCTION_LITERAL, scope_position).ToHandle(&fun);
  RETURN_ON_FAILED_EXECUTION(Function);

  i::Handle<i::Object> result;
  has_pending_exception =
      !i::Execution::Call(isolate, fun,
                          Utils::OpenHandle(*v8_context->Global()), 0,
                          nullptr).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Function);
  RETURN_ESCAPED(Utils::ToLocal(i::Handle<i::JSFunction>::cast(result)));
}


Local<Function> ScriptCompiler::CompileFunctionInContext(
    Isolate* v8_isolate, Source* source, Local<Context> v8_context,
    size_t arguments_count, Local<String> arguments[],
    size_t context_extension_count, Local<Object> context_extensions[]) {
  RETURN_TO_LOCAL_UNCHECKED(
      CompileFunctionInContext(v8_context, source, arguments_count, arguments,
                               context_extension_count, context_extensions),
      Function);
}


ScriptCompiler::ScriptStreamingTask* ScriptCompiler::StartStreamingScript(
    Isolate* v8_isolate, StreamedSource* source, CompileOptions options) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return new i::BackgroundParsingTask(source->impl(), options,
                                      i::FLAG_stack_size, isolate);
}


MaybeLocal<Script> ScriptCompiler::Compile(Local<Context> context,
                                           StreamedSource* v8_source,
                                           Handle<String> full_source_string,
                                           const ScriptOrigin& origin) {
  PREPARE_FOR_EXECUTION(context, "v8::ScriptCompiler::Compile()", Script);
  i::StreamedSource* source = v8_source->impl();
  i::SharedFunctionInfo* raw_result = nullptr;
  {
    i::HandleScope scope(isolate);
    i::Handle<i::String> str = Utils::OpenHandle(*(full_source_string));
    i::Handle<i::Script> script = isolate->factory()->NewScript(str);
    if (!origin.ResourceName().IsEmpty()) {
      script->set_name(*Utils::OpenHandle(*(origin.ResourceName())));
    }
    if (!origin.ResourceLineOffset().IsEmpty()) {
      script->set_line_offset(i::Smi::FromInt(
          static_cast<int>(origin.ResourceLineOffset()->Value())));
    }
    if (!origin.ResourceColumnOffset().IsEmpty()) {
      script->set_column_offset(i::Smi::FromInt(
          static_cast<int>(origin.ResourceColumnOffset()->Value())));
    }
    if (!origin.ResourceIsSharedCrossOrigin().IsEmpty()) {
      script->set_is_shared_cross_origin(
          origin.ResourceIsSharedCrossOrigin()->IsTrue());
    }
    if (!origin.ResourceIsEmbedderDebugScript().IsEmpty()) {
      script->set_is_embedder_debug_script(
          origin.ResourceIsEmbedderDebugScript()->IsTrue());
    }
    if (!origin.SourceMapUrl().IsEmpty()) {
      script->set_source_mapping_url(
          *Utils::OpenHandle(*(origin.SourceMapUrl())));
    }

    source->info->set_script(script);
    source->info->set_context(isolate->native_context());

    // Do the parsing tasks which need to be done on the main thread. This will
    // also handle parse errors.
    source->parser->Internalize(isolate, script,
                                source->info->function() == nullptr);
    source->parser->HandleSourceURLComments(isolate, script);

    i::Handle<i::SharedFunctionInfo> result;
    if (source->info->function() != nullptr) {
      // Parsing has succeeded.
      result = i::Compiler::CompileStreamedScript(script, source->info.get(),
                                                  str->length());
    }
    has_pending_exception = result.is_null();
    if (has_pending_exception) isolate->ReportPendingMessages();
    RETURN_ON_FAILED_EXECUTION(Script);

    source->info->clear_script();  // because script goes out of scope.
    raw_result = *result;          // TODO(titzer): use CloseAndEscape?
  }

  i::Handle<i::SharedFunctionInfo> result(raw_result, isolate);
  Local<UnboundScript> generic = ToApiHandle<UnboundScript>(result);
  if (generic.IsEmpty()) return Local<Script>();
  RETURN_ESCAPED(generic->BindToCurrentContext());
}


Local<Script> ScriptCompiler::Compile(Isolate* v8_isolate,
                                      StreamedSource* v8_source,
                                      Handle<String> full_source_string,
                                      const ScriptOrigin& origin) {
  auto context = v8_isolate->GetCurrentContext();
  RETURN_TO_LOCAL_UNCHECKED(
      Compile(context, v8_source, full_source_string, origin), Script);
}


uint32_t ScriptCompiler::CachedDataVersionTag() {
  return static_cast<uint32_t>(base::hash_combine(
      internal::Version::Hash(), internal::FlagList::Hash(),
      static_cast<uint32_t>(internal::CpuFeatures::SupportedFeatures())));
}


MaybeLocal<Script> Script::Compile(Local<Context> context,
                                   Handle<String> source,
                                   ScriptOrigin* origin) {
  if (origin) {
    ScriptCompiler::Source script_source(source, *origin);
    return ScriptCompiler::Compile(context, &script_source);
  }
  ScriptCompiler::Source script_source(source);
  return ScriptCompiler::Compile(context, &script_source);
}


Local<Script> Script::Compile(v8::Handle<String> source,
                              v8::ScriptOrigin* origin) {
  auto str = Utils::OpenHandle(*source);
  auto context = ContextFromHeapObject(str);
  RETURN_TO_LOCAL_UNCHECKED(Compile(context, source, origin), Script);
}


Local<Script> Script::Compile(v8::Handle<String> source,
                              v8::Handle<String> file_name) {
  ScriptOrigin origin(file_name);
  return Compile(source, &origin);
}


// --- E x c e p t i o n s ---


v8::TryCatch::TryCatch()
    : isolate_(i::Isolate::Current()),
      next_(isolate_->try_catch_handler()),
      is_verbose_(false),
      can_continue_(true),
      capture_message_(true),
      rethrow_(false),
      has_terminated_(false) {
  ResetInternal();
  // Special handling for simulators which have a separate JS stack.
  js_stack_comparable_address_ =
      reinterpret_cast<void*>(v8::internal::SimulatorStack::RegisterCTryCatch(
          v8::internal::GetCurrentStackPosition()));
  isolate_->RegisterTryCatchHandler(this);
}


v8::TryCatch::TryCatch(v8::Isolate* isolate)
    : isolate_(reinterpret_cast<i::Isolate*>(isolate)),
      next_(isolate_->try_catch_handler()),
      is_verbose_(false),
      can_continue_(true),
      capture_message_(true),
      rethrow_(false),
      has_terminated_(false) {
  ResetInternal();
  // Special handling for simulators which have a separate JS stack.
  js_stack_comparable_address_ =
      reinterpret_cast<void*>(v8::internal::SimulatorStack::RegisterCTryCatch(
          v8::internal::GetCurrentStackPosition()));
  isolate_->RegisterTryCatchHandler(this);
}


v8::TryCatch::~TryCatch() {
  if (rethrow_) {
    v8::Isolate* isolate = reinterpret_cast<Isolate*>(isolate_);
    v8::HandleScope scope(isolate);
    v8::Local<v8::Value> exc = v8::Local<v8::Value>::New(isolate, Exception());
    if (HasCaught() && capture_message_) {
      // If an exception was caught and rethrow_ is indicated, the saved
      // message, script, and location need to be restored to Isolate TLS
      // for reuse.  capture_message_ needs to be disabled so that Throw()
      // does not create a new message.
      isolate_->thread_local_top()->rethrowing_message_ = true;
      isolate_->RestorePendingMessageFromTryCatch(this);
    }
    isolate_->UnregisterTryCatchHandler(this);
    v8::internal::SimulatorStack::UnregisterCTryCatch();
    reinterpret_cast<Isolate*>(isolate_)->ThrowException(exc);
    DCHECK(!isolate_->thread_local_top()->rethrowing_message_);
  } else {
    if (HasCaught() && isolate_->has_scheduled_exception()) {
      // If an exception was caught but is still scheduled because no API call
      // promoted it, then it is canceled to prevent it from being propagated.
      // Note that this will not cancel termination exceptions.
      isolate_->CancelScheduledExceptionFromTryCatch(this);
    }
    isolate_->UnregisterTryCatchHandler(this);
    v8::internal::SimulatorStack::UnregisterCTryCatch();
  }
}


bool v8::TryCatch::HasCaught() const {
  return !reinterpret_cast<i::Object*>(exception_)->IsTheHole();
}


bool v8::TryCatch::CanContinue() const {
  return can_continue_;
}


bool v8::TryCatch::HasTerminated() const {
  return has_terminated_;
}


v8::Handle<v8::Value> v8::TryCatch::ReThrow() {
  if (!HasCaught()) return v8::Local<v8::Value>();
  rethrow_ = true;
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate_));
}


v8::Local<Value> v8::TryCatch::Exception() const {
  if (HasCaught()) {
    // Check for out of memory exception.
    i::Object* exception = reinterpret_cast<i::Object*>(exception_);
    return v8::Utils::ToLocal(i::Handle<i::Object>(exception, isolate_));
  } else {
    return v8::Local<Value>();
  }
}


v8::Local<Value> v8::TryCatch::StackTrace() const {
  if (HasCaught()) {
    i::Object* raw_obj = reinterpret_cast<i::Object*>(exception_);
    if (!raw_obj->IsJSObject()) return v8::Local<Value>();
    i::HandleScope scope(isolate_);
    i::Handle<i::JSObject> obj(i::JSObject::cast(raw_obj), isolate_);
    i::Handle<i::String> name = isolate_->factory()->stack_string();
    {
      EXCEPTION_PREAMBLE(isolate_);
      Maybe<bool> maybe = i::JSReceiver::HasProperty(obj, name);
      has_pending_exception = !maybe.IsJust();
      EXCEPTION_BAILOUT_CHECK(isolate_, v8::Local<Value>());
      if (!maybe.FromJust()) return v8::Local<Value>();
    }
    i::Handle<i::Object> value;
    EXCEPTION_PREAMBLE(isolate_);
    has_pending_exception = !i::Object::GetProperty(obj, name).ToHandle(&value);
    EXCEPTION_BAILOUT_CHECK(isolate_, v8::Local<Value>());
    return v8::Utils::ToLocal(scope.CloseAndEscape(value));
  } else {
    return v8::Local<Value>();
  }
}


v8::Local<v8::Message> v8::TryCatch::Message() const {
  i::Object* message = reinterpret_cast<i::Object*>(message_obj_);
  DCHECK(message->IsJSMessageObject() || message->IsTheHole());
  if (HasCaught() && !message->IsTheHole()) {
    return v8::Utils::MessageToLocal(i::Handle<i::Object>(message, isolate_));
  } else {
    return v8::Local<v8::Message>();
  }
}


void v8::TryCatch::Reset() {
  if (!rethrow_ && HasCaught() && isolate_->has_scheduled_exception()) {
    // If an exception was caught but is still scheduled because no API call
    // promoted it, then it is canceled to prevent it from being propagated.
    // Note that this will not cancel termination exceptions.
    isolate_->CancelScheduledExceptionFromTryCatch(this);
  }
  ResetInternal();
}


void v8::TryCatch::ResetInternal() {
  i::Object* the_hole = isolate_->heap()->the_hole_value();
  exception_ = the_hole;
  message_obj_ = the_hole;
}


void v8::TryCatch::SetVerbose(bool value) {
  is_verbose_ = value;
}


void v8::TryCatch::SetCaptureMessage(bool value) {
  capture_message_ = value;
}


// --- M e s s a g e ---


Local<String> Message::Get() const {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  EscapableHandleScope scope(reinterpret_cast<Isolate*>(isolate));
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::String> raw_result = i::MessageHandler::GetMessage(isolate, obj);
  Local<String> result = Utils::ToLocal(raw_result);
  return scope.Escape(result);
}


ScriptOrigin Message::GetScriptOrigin() const {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  auto message = i::Handle<i::JSMessageObject>::cast(Utils::OpenHandle(this));
  auto script_wraper = i::Handle<i::Object>(message->script(), isolate);
  auto script_value = i::Handle<i::JSValue>::cast(script_wraper);
  i::Handle<i::Script> script(i::Script::cast(script_value->value()));
  return GetScriptOriginForScript(isolate, script);
}


v8::Handle<Value> Message::GetScriptResourceName() const {
  return GetScriptOrigin().ResourceName();
}


v8::Handle<v8::StackTrace> Message::GetStackTrace() const {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  EscapableHandleScope scope(reinterpret_cast<Isolate*>(isolate));
  auto message = i::Handle<i::JSMessageObject>::cast(Utils::OpenHandle(this));
  i::Handle<i::Object> stackFramesObj(message->stack_frames(), isolate);
  if (!stackFramesObj->IsJSArray()) return v8::Handle<v8::StackTrace>();
  auto stackTrace = i::Handle<i::JSArray>::cast(stackFramesObj);
  return scope.Escape(Utils::StackTraceToLocal(stackTrace));
}


MUST_USE_RESULT static i::MaybeHandle<i::Object> CallV8HeapFunction(
    i::Isolate* isolate, const char* name, i::Handle<i::Object> recv, int argc,
    i::Handle<i::Object> argv[]) {
  i::Handle<i::Object> object_fun =
      i::Object::GetProperty(
          isolate, isolate->js_builtins_object(), name).ToHandleChecked();
  i::Handle<i::JSFunction> fun = i::Handle<i::JSFunction>::cast(object_fun);
  return i::Execution::Call(isolate, fun, recv, argc, argv);
}


MUST_USE_RESULT static i::MaybeHandle<i::Object> CallV8HeapFunction(
    i::Isolate* isolate, const char* name, i::Handle<i::Object> data) {
  i::Handle<i::Object> argv[] = { data };
  return CallV8HeapFunction(isolate, name, isolate->js_builtins_object(),
                            arraysize(argv), argv);
}


Maybe<int> Message::GetLineNumber(Local<Context> context) const {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Message::GetLineNumber()", int);
  i::Handle<i::Object> result;
  has_pending_exception =
      !CallV8HeapFunction(isolate, "GetLineNumber", Utils::OpenHandle(this))
           .ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(int);
  return Just(static_cast<int>(result->Number()));
}


int Message::GetLineNumber() const {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return GetLineNumber(context).FromMaybe(0);
}


int Message::GetStartPosition() const {
  auto self = Utils::OpenHandle(this);
  return self->start_position();
}


int Message::GetEndPosition() const {
  auto self = Utils::OpenHandle(this);
  return self->end_position();
}


Maybe<int> Message::GetStartColumn(Local<Context> context) const {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Message::GetStartColumn()",
                                  int);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> start_col_obj;
  has_pending_exception = !CallV8HeapFunction(isolate, "GetPositionInLine",
                                              self).ToHandle(&start_col_obj);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(int);
  return Just(static_cast<int>(start_col_obj->Number()));
}


int Message::GetStartColumn() const {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  const int default_value = kNoColumnInfo;
  return GetStartColumn(context).FromMaybe(default_value);
}


Maybe<int> Message::GetEndColumn(Local<Context> context) const {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Message::GetEndColumn()", int);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> start_col_obj;
  has_pending_exception = !CallV8HeapFunction(isolate, "GetPositionInLine",
                                              self).ToHandle(&start_col_obj);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(int);
  int start = self->start_position();
  int end = self->end_position();
  return Just(static_cast<int>(start_col_obj->Number()) + (end - start));
}


int Message::GetEndColumn() const {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  const int default_value = kNoColumnInfo;
  return GetEndColumn(context).FromMaybe(default_value);
}


bool Message::IsSharedCrossOrigin() const {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  auto self = Utils::OpenHandle(this);
  auto script = i::Handle<i::JSValue>::cast(
      i::Handle<i::Object>(self->script(), isolate));
  return i::Script::cast(script->value())->is_shared_cross_origin();
}


MaybeLocal<String> Message::GetSourceLine(Local<Context> context) const {
  PREPARE_FOR_EXECUTION(context, "v8::Message::GetSourceLine()", String);
  i::Handle<i::Object> result;
  has_pending_exception =
      !CallV8HeapFunction(isolate, "GetSourceLine", Utils::OpenHandle(this))
           .ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(String);
  Local<String> str;
  if (result->IsString()) {
    str = Utils::ToLocal(i::Handle<i::String>::cast(result));
  }
  RETURN_ESCAPED(str);
}


Local<String> Message::GetSourceLine() const {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(GetSourceLine(context), String)
}


void Message::PrintCurrentStackTrace(Isolate* isolate, FILE* out) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ENTER_V8(i_isolate);
  i_isolate->PrintCurrentStackTrace(out);
}


// --- S t a c k T r a c e ---

Local<StackFrame> StackTrace::GetFrame(uint32_t index) const {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  EscapableHandleScope scope(reinterpret_cast<Isolate*>(isolate));
  auto self = Utils::OpenHandle(this);
  auto obj = i::Object::GetElement(isolate, self, index).ToHandleChecked();
  auto jsobj = i::Handle<i::JSObject>::cast(obj);
  return scope.Escape(Utils::StackFrameToLocal(jsobj));
}


int StackTrace::GetFrameCount() const {
  return i::Smi::cast(Utils::OpenHandle(this)->length())->value();
}


Local<Array> StackTrace::AsArray() {
  return Utils::ToLocal(Utils::OpenHandle(this));
}


Local<StackTrace> StackTrace::CurrentStackTrace(
    Isolate* isolate,
    int frame_limit,
    StackTraceOptions options) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ENTER_V8(i_isolate);
  // TODO(dcarney): remove when ScriptDebugServer is fixed.
  options = static_cast<StackTraceOptions>(
      static_cast<int>(options) | kExposeFramesAcrossSecurityOrigins);
  i::Handle<i::JSArray> stackTrace =
      i_isolate->CaptureCurrentStackTrace(frame_limit, options);
  return Utils::StackTraceToLocal(stackTrace);
}


// --- S t a c k F r a m e ---

static int getIntProperty(const StackFrame* f, const char* propertyName,
                          int defaultValue) {
  i::Isolate* isolate = Utils::OpenHandle(f)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::JSObject> self = Utils::OpenHandle(f);
  i::Handle<i::Object> obj =
      i::Object::GetProperty(isolate, self, propertyName).ToHandleChecked();
  return obj->IsSmi() ? i::Smi::cast(*obj)->value() : defaultValue;
}


int StackFrame::GetLineNumber() const {
  return getIntProperty(this, "lineNumber", Message::kNoLineNumberInfo);
}


int StackFrame::GetColumn() const {
  return getIntProperty(this, "column", Message::kNoColumnInfo);
}


int StackFrame::GetScriptId() const {
  return getIntProperty(this, "scriptId", Message::kNoScriptIdInfo);
}


static Local<String> getStringProperty(const StackFrame* f,
                                       const char* propertyName) {
  i::Isolate* isolate = Utils::OpenHandle(f)->GetIsolate();
  ENTER_V8(isolate);
  EscapableHandleScope scope(reinterpret_cast<Isolate*>(isolate));
  i::Handle<i::JSObject> self = Utils::OpenHandle(f);
  i::Handle<i::Object> obj =
      i::Object::GetProperty(isolate, self, propertyName).ToHandleChecked();
  return obj->IsString()
             ? scope.Escape(Local<String>::Cast(Utils::ToLocal(obj)))
             : Local<String>();
}


Local<String> StackFrame::GetScriptName() const {
  return getStringProperty(this, "scriptName");
}


Local<String> StackFrame::GetScriptNameOrSourceURL() const {
  return getStringProperty(this, "scriptNameOrSourceURL");
}


Local<String> StackFrame::GetFunctionName() const {
  return getStringProperty(this, "functionName");
}


static bool getBoolProperty(const StackFrame* f, const char* propertyName) {
  i::Isolate* isolate = Utils::OpenHandle(f)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::JSObject> self = Utils::OpenHandle(f);
  i::Handle<i::Object> obj =
      i::Object::GetProperty(isolate, self, propertyName).ToHandleChecked();
  return obj->IsTrue();
}

bool StackFrame::IsEval() const { return getBoolProperty(this, "isEval"); }


bool StackFrame::IsConstructor() const {
  return getBoolProperty(this, "isConstructor");
}


// --- N a t i v e W e a k M a p ---

Local<NativeWeakMap> NativeWeakMap::New(Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8(isolate);
  i::Handle<i::JSWeakMap> weakmap = isolate->factory()->NewJSWeakMap();
  i::Runtime::WeakCollectionInitialize(isolate, weakmap);
  return Utils::NativeWeakMapToLocal(weakmap);
}


void NativeWeakMap::Set(Handle<Value> v8_key, Handle<Value> v8_value) {
  i::Handle<i::JSWeakMap> weak_collection = Utils::OpenHandle(this);
  i::Isolate* isolate = weak_collection->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::Object> key = Utils::OpenHandle(*v8_key);
  i::Handle<i::Object> value = Utils::OpenHandle(*v8_value);
  if (!key->IsJSReceiver() && !key->IsSymbol()) {
    DCHECK(false);
    return;
  }
  i::Handle<i::ObjectHashTable> table(
      i::ObjectHashTable::cast(weak_collection->table()));
  if (!table->IsKey(*key)) {
    DCHECK(false);
    return;
  }
  i::Runtime::WeakCollectionSet(weak_collection, key, value);
}


Local<Value> NativeWeakMap::Get(Handle<Value> v8_key) {
  i::Handle<i::JSWeakMap> weak_collection = Utils::OpenHandle(this);
  i::Isolate* isolate = weak_collection->GetIsolate();
  ENTER_V8(isolate);
  i::Handle<i::Object> key = Utils::OpenHandle(*v8_key);
  if (!key->IsJSReceiver() && !key->IsSymbol()) {
    DCHECK(false);
    return v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  i::Handle<i::ObjectHashTable> table(
      i::ObjectHashTable::cast(weak_collection->table()));
  if (!table->IsKey(*key)) {
    DCHECK(false);
    return v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  }
  i::Handle<i::Object> lookup(table->Lookup(key), isolate);
  if (lookup->IsTheHole())
    return v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
  return Utils::ToLocal(lookup);
}


bool NativeWeakMap::Has(Handle<Value> v8_key) {
  i::Handle<i::JSWeakMap> weak_collection = Utils::OpenHandle(this);
  i::Isolate* isolate = weak_collection->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::Object> key = Utils::OpenHandle(*v8_key);
  if (!key->IsJSReceiver() && !key->IsSymbol()) {
    DCHECK(false);
    return false;
  }
  i::Handle<i::ObjectHashTable> table(
      i::ObjectHashTable::cast(weak_collection->table()));
  if (!table->IsKey(*key)) {
    DCHECK(false);
    return false;
  }
  i::Handle<i::Object> lookup(table->Lookup(key), isolate);
  return !lookup->IsTheHole();
}


bool NativeWeakMap::Delete(Handle<Value> v8_key) {
  i::Handle<i::JSWeakMap> weak_collection = Utils::OpenHandle(this);
  i::Isolate* isolate = weak_collection->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::Object> key = Utils::OpenHandle(*v8_key);
  if (!key->IsJSReceiver() && !key->IsSymbol()) {
    DCHECK(false);
    return false;
  }
  i::Handle<i::ObjectHashTable> table(
      i::ObjectHashTable::cast(weak_collection->table()));
  if (!table->IsKey(*key)) {
    DCHECK(false);
    return false;
  }
  return i::Runtime::WeakCollectionDelete(weak_collection, key);
}


// --- J S O N ---

MaybeLocal<Value> JSON::Parse(Isolate* v8_isolate, Local<String> json_string) {
  auto isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  PREPARE_FOR_EXECUTION_WITH_ISOLATE(isolate, "JSON::Parse", Value);
  i::Handle<i::String> string = Utils::OpenHandle(*json_string);
  i::Handle<i::String> source = i::String::Flatten(string);
  auto maybe = source->IsSeqOneByteString()
                   ? i::JsonParser<true>::Parse(source)
                   : i::JsonParser<false>::Parse(source);
  Local<Value> result;
  has_pending_exception = !ToLocal<Value>(maybe, &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<Value> JSON::Parse(Local<String> json_string) {
  auto isolate = reinterpret_cast<v8::Isolate*>(
      Utils::OpenHandle(*json_string)->GetIsolate());
  RETURN_TO_LOCAL_UNCHECKED(Parse(isolate, json_string), Value);
}


// --- D a t a ---

bool Value::FullIsUndefined() const {
  bool result = Utils::OpenHandle(this)->IsUndefined();
  DCHECK_EQ(result, QuickIsUndefined());
  return result;
}


bool Value::FullIsNull() const {
  bool result = Utils::OpenHandle(this)->IsNull();
  DCHECK_EQ(result, QuickIsNull());
  return result;
}


bool Value::IsTrue() const {
  return Utils::OpenHandle(this)->IsTrue();
}


bool Value::IsFalse() const {
  return Utils::OpenHandle(this)->IsFalse();
}


bool Value::IsFunction() const {
  return Utils::OpenHandle(this)->IsJSFunction();
}


bool Value::IsName() const {
  return Utils::OpenHandle(this)->IsName();
}


bool Value::FullIsString() const {
  bool result = Utils::OpenHandle(this)->IsString();
  DCHECK_EQ(result, QuickIsString());
  return result;
}


bool Value::IsSymbol() const {
  return Utils::OpenHandle(this)->IsSymbol();
}


bool Value::IsArray() const {
  return Utils::OpenHandle(this)->IsJSArray();
}


bool Value::IsArrayBuffer() const {
  return Utils::OpenHandle(this)->IsJSArrayBuffer();
}


bool Value::IsArrayBufferView() const {
  return Utils::OpenHandle(this)->IsJSArrayBufferView();
}


bool Value::IsTypedArray() const {
  return Utils::OpenHandle(this)->IsJSTypedArray();
}


#define VALUE_IS_TYPED_ARRAY(Type, typeName, TYPE, ctype, size)            \
  bool Value::Is##Type##Array() const {                                    \
    i::Handle<i::Object> obj = Utils::OpenHandle(this);                    \
    return obj->IsJSTypedArray() &&                                        \
           i::JSTypedArray::cast(*obj)->type() == kExternal##Type##Array;  \
  }

TYPED_ARRAYS(VALUE_IS_TYPED_ARRAY)

#undef VALUE_IS_TYPED_ARRAY


bool Value::IsDataView() const {
  return Utils::OpenHandle(this)->IsJSDataView();
}


bool Value::IsObject() const {
  return Utils::OpenHandle(this)->IsJSObject();
}


bool Value::IsNumber() const {
  return Utils::OpenHandle(this)->IsNumber();
}


#define VALUE_IS_SPECIFIC_TYPE(Type, Class)                            \
  bool Value::Is##Type() const {                                       \
    i::Handle<i::Object> obj = Utils::OpenHandle(this);                \
    if (!obj->IsHeapObject()) return false;                            \
    i::Isolate* isolate = i::HeapObject::cast(*obj)->GetIsolate();     \
    return obj->HasSpecificClassOf(isolate->heap()->Class##_string()); \
  }

VALUE_IS_SPECIFIC_TYPE(ArgumentsObject, Arguments)
VALUE_IS_SPECIFIC_TYPE(BooleanObject, Boolean)
VALUE_IS_SPECIFIC_TYPE(NumberObject, Number)
VALUE_IS_SPECIFIC_TYPE(StringObject, String)
VALUE_IS_SPECIFIC_TYPE(SymbolObject, Symbol)
VALUE_IS_SPECIFIC_TYPE(Date, Date)
VALUE_IS_SPECIFIC_TYPE(Map, Map)
VALUE_IS_SPECIFIC_TYPE(Set, Set)
VALUE_IS_SPECIFIC_TYPE(WeakMap, WeakMap)
VALUE_IS_SPECIFIC_TYPE(WeakSet, WeakSet)

#undef VALUE_IS_SPECIFIC_TYPE


bool Value::IsBoolean() const {
  return Utils::OpenHandle(this)->IsBoolean();
}


bool Value::IsExternal() const {
  return Utils::OpenHandle(this)->IsExternal();
}


bool Value::IsInt32() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) return true;
  if (obj->IsNumber()) {
    return i::IsInt32Double(obj->Number());
  }
  return false;
}


bool Value::IsUint32() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) return i::Smi::cast(*obj)->value() >= 0;
  if (obj->IsNumber()) {
    double value = obj->Number();
    return !i::IsMinusZero(value) &&
        value >= 0 &&
        value <= i::kMaxUInt32 &&
        value == i::FastUI2D(i::FastD2UI(value));
  }
  return false;
}


static bool CheckConstructor(i::Isolate* isolate,
                             i::Handle<i::JSObject> obj,
                             const char* class_name) {
  i::Handle<i::Object> constr(obj->map()->GetConstructor(), isolate);
  if (!constr->IsJSFunction()) return false;
  i::Handle<i::JSFunction> func = i::Handle<i::JSFunction>::cast(constr);
  return func->shared()->native() && constr.is_identical_to(
      i::Object::GetProperty(isolate,
                             isolate->js_builtins_object(),
                             class_name).ToHandleChecked());
}


bool Value::IsNativeError() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsJSObject()) {
    i::Handle<i::JSObject> js_obj(i::JSObject::cast(*obj));
    i::Isolate* isolate = js_obj->GetIsolate();
    return CheckConstructor(isolate, js_obj, "$Error") ||
        CheckConstructor(isolate, js_obj, "$EvalError") ||
        CheckConstructor(isolate, js_obj, "$RangeError") ||
        CheckConstructor(isolate, js_obj, "$ReferenceError") ||
        CheckConstructor(isolate, js_obj, "$SyntaxError") ||
        CheckConstructor(isolate, js_obj, "$TypeError") ||
        CheckConstructor(isolate, js_obj, "$URIError");
  } else {
    return false;
  }
}


bool Value::IsRegExp() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  return obj->IsJSRegExp();
}


bool Value::IsGeneratorFunction() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (!obj->IsJSFunction()) return false;
  i::Handle<i::JSFunction> func = i::Handle<i::JSFunction>::cast(obj);
  return func->shared()->is_generator();
}


bool Value::IsGeneratorObject() const {
  return Utils::OpenHandle(this)->IsJSGeneratorObject();
}


bool Value::IsMapIterator() const {
  return Utils::OpenHandle(this)->IsJSMapIterator();
}


bool Value::IsSetIterator() const {
  return Utils::OpenHandle(this)->IsJSSetIterator();
}


MaybeLocal<String> Value::ToString(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsString()) return ToApiHandle<String>(obj);
  PREPARE_FOR_EXECUTION(context, "ToString", String);
  Local<String> result;
  has_pending_exception =
      !ToLocal<String>(i::Execution::ToString(isolate, obj), &result);
  RETURN_ON_FAILED_EXECUTION(String);
  RETURN_ESCAPED(result);
}


Local<String> Value::ToString(Isolate* isolate) const {
  RETURN_TO_LOCAL_UNCHECKED(ToString(isolate->GetCurrentContext()), String);
}


MaybeLocal<String> Value::ToDetailString(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsString()) return ToApiHandle<String>(obj);
  PREPARE_FOR_EXECUTION(context, "ToDetailString", String);
  Local<String> result;
  has_pending_exception =
      !ToLocal<String>(i::Execution::ToDetailString(isolate, obj), &result);
  RETURN_ON_FAILED_EXECUTION(String);
  RETURN_ESCAPED(result);
}


Local<String> Value::ToDetailString(Isolate* isolate) const {
  RETURN_TO_LOCAL_UNCHECKED(ToDetailString(isolate->GetCurrentContext()),
                            String);
}


MaybeLocal<Object> Value::ToObject(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsJSObject()) return ToApiHandle<Object>(obj);
  PREPARE_FOR_EXECUTION(context, "ToObject", Object);
  Local<Object> result;
  has_pending_exception =
      !ToLocal<Object>(i::Execution::ToObject(isolate, obj), &result);
  RETURN_ON_FAILED_EXECUTION(Object);
  RETURN_ESCAPED(result);
}


Local<v8::Object> Value::ToObject(Isolate* isolate) const {
  RETURN_TO_LOCAL_UNCHECKED(ToObject(isolate->GetCurrentContext()), Object);
}


MaybeLocal<Boolean> Value::ToBoolean(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsBoolean()) return ToApiHandle<Boolean>(obj);
  auto isolate = reinterpret_cast<i::Isolate*>(context->GetIsolate());
  auto val = isolate->factory()->ToBoolean(obj->BooleanValue());
  return ToApiHandle<Boolean>(val);
}


Local<Boolean> Value::ToBoolean(Isolate* v8_isolate) const {
  return ToBoolean(v8_isolate->GetCurrentContext()).ToLocalChecked();
}


MaybeLocal<Number> Value::ToNumber(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) return ToApiHandle<Number>(obj);
  PREPARE_FOR_EXECUTION(context, "ToNumber", Number);
  Local<Number> result;
  has_pending_exception =
      !ToLocal<Number>(i::Execution::ToNumber(isolate, obj), &result);
  RETURN_ON_FAILED_EXECUTION(Number);
  RETURN_ESCAPED(result);
}


Local<Number> Value::ToNumber(Isolate* isolate) const {
  RETURN_TO_LOCAL_UNCHECKED(ToNumber(isolate->GetCurrentContext()), Number);
}


MaybeLocal<Integer> Value::ToInteger(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) return ToApiHandle<Integer>(obj);
  PREPARE_FOR_EXECUTION(context, "ToInteger", Integer);
  Local<Integer> result;
  has_pending_exception =
      !ToLocal<Integer>(i::Execution::ToInteger(isolate, obj), &result);
  RETURN_ON_FAILED_EXECUTION(Integer);
  RETURN_ESCAPED(result);
}


Local<Integer> Value::ToInteger(Isolate* isolate) const {
  RETURN_TO_LOCAL_UNCHECKED(ToInteger(isolate->GetCurrentContext()), Integer);
}


MaybeLocal<Int32> Value::ToInt32(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) return ToApiHandle<Int32>(obj);
  Local<Int32> result;
  PREPARE_FOR_EXECUTION(context, "ToInt32", Int32);
  has_pending_exception =
      !ToLocal<Int32>(i::Execution::ToInt32(isolate, obj), &result);
  RETURN_ON_FAILED_EXECUTION(Int32);
  RETURN_ESCAPED(result);
}


Local<Int32> Value::ToInt32(Isolate* isolate) const {
  RETURN_TO_LOCAL_UNCHECKED(ToInt32(isolate->GetCurrentContext()), Int32);
}


MaybeLocal<Uint32> Value::ToUint32(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) return ToApiHandle<Uint32>(obj);
  Local<Uint32> result;
  PREPARE_FOR_EXECUTION(context, "ToUInt32", Uint32);
  has_pending_exception =
      !ToLocal<Uint32>(i::Execution::ToUint32(isolate, obj), &result);
  RETURN_ON_FAILED_EXECUTION(Uint32);
  RETURN_ESCAPED(result);
}


Local<Uint32> Value::ToUint32(Isolate* isolate) const {
  RETURN_TO_LOCAL_UNCHECKED(ToUint32(isolate->GetCurrentContext()), Uint32);
}


void i::Internals::CheckInitializedImpl(v8::Isolate* external_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(external_isolate);
  Utils::ApiCheck(isolate != NULL &&
                  !isolate->IsDead(),
                  "v8::internal::Internals::CheckInitialized()",
                  "Isolate is not initialized or V8 has died");
}


void External::CheckCast(v8::Value* that) {
  Utils::ApiCheck(Utils::OpenHandle(that)->IsExternal(),
                  "v8::External::Cast()",
                  "Could not convert to external");
}


void v8::Object::CheckCast(Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSObject(),
                  "v8::Object::Cast()",
                  "Could not convert to object");
}


void v8::Function::CheckCast(Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSFunction(),
                  "v8::Function::Cast()",
                  "Could not convert to function");
}


void v8::Boolean::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsBoolean(),
                  "v8::Boolean::Cast()",
                  "Could not convert to boolean");
}


void v8::Name::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsName(),
                  "v8::Name::Cast()",
                  "Could not convert to name");
}


void v8::String::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsString(),
                  "v8::String::Cast()",
                  "Could not convert to string");
}


void v8::Symbol::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsSymbol(),
                  "v8::Symbol::Cast()",
                  "Could not convert to symbol");
}


void v8::Number::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsNumber(),
                  "v8::Number::Cast()",
                  "Could not convert to number");
}


void v8::Integer::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsNumber(),
                  "v8::Integer::Cast()",
                  "Could not convert to number");
}


void v8::Int32::CheckCast(v8::Value* that) {
  Utils::ApiCheck(that->IsInt32(), "v8::Int32::Cast()",
                  "Could not convert to 32-bit signed integer");
}


void v8::Uint32::CheckCast(v8::Value* that) {
  Utils::ApiCheck(that->IsUint32(), "v8::Uint32::Cast()",
                  "Could not convert to 32-bit unsigned integer");
}


void v8::Array::CheckCast(Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSArray(),
                  "v8::Array::Cast()",
                  "Could not convert to array");
}


void v8::Promise::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsPromise(),
                  "v8::Promise::Cast()",
                  "Could not convert to promise");
}


void v8::Promise::Resolver::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsPromise(),
                  "v8::Promise::Resolver::Cast()",
                  "Could not convert to promise resolver");
}


void v8::ArrayBuffer::CheckCast(Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSArrayBuffer(),
                  "v8::ArrayBuffer::Cast()",
                  "Could not convert to ArrayBuffer");
}


void v8::ArrayBufferView::CheckCast(Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSArrayBufferView(),
                  "v8::ArrayBufferView::Cast()",
                  "Could not convert to ArrayBufferView");
}


void v8::TypedArray::CheckCast(Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSTypedArray(),
                  "v8::TypedArray::Cast()",
                  "Could not convert to TypedArray");
}


#define CHECK_TYPED_ARRAY_CAST(Type, typeName, TYPE, ctype, size)             \
  void v8::Type##Array::CheckCast(Value* that) {                              \
    i::Handle<i::Object> obj = Utils::OpenHandle(that);                       \
    Utils::ApiCheck(obj->IsJSTypedArray() &&                                  \
                    i::JSTypedArray::cast(*obj)->type() ==                    \
                        kExternal##Type##Array,                               \
                    "v8::" #Type "Array::Cast()",                             \
                    "Could not convert to " #Type "Array");                   \
  }


TYPED_ARRAYS(CHECK_TYPED_ARRAY_CAST)

#undef CHECK_TYPED_ARRAY_CAST


void v8::DataView::CheckCast(Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSDataView(),
                  "v8::DataView::Cast()",
                  "Could not convert to DataView");
}


void v8::Date::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  i::Isolate* isolate = NULL;
  if (obj->IsHeapObject()) isolate = i::HeapObject::cast(*obj)->GetIsolate();
  Utils::ApiCheck(isolate != NULL &&
                  obj->HasSpecificClassOf(isolate->heap()->Date_string()),
                  "v8::Date::Cast()",
                  "Could not convert to date");
}


void v8::StringObject::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  i::Isolate* isolate = NULL;
  if (obj->IsHeapObject()) isolate = i::HeapObject::cast(*obj)->GetIsolate();
  Utils::ApiCheck(isolate != NULL &&
                  obj->HasSpecificClassOf(isolate->heap()->String_string()),
                  "v8::StringObject::Cast()",
                  "Could not convert to StringObject");
}


void v8::SymbolObject::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  i::Isolate* isolate = NULL;
  if (obj->IsHeapObject()) isolate = i::HeapObject::cast(*obj)->GetIsolate();
  Utils::ApiCheck(isolate != NULL &&
                  obj->HasSpecificClassOf(isolate->heap()->Symbol_string()),
                  "v8::SymbolObject::Cast()",
                  "Could not convert to SymbolObject");
}


void v8::NumberObject::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  i::Isolate* isolate = NULL;
  if (obj->IsHeapObject()) isolate = i::HeapObject::cast(*obj)->GetIsolate();
  Utils::ApiCheck(isolate != NULL &&
                  obj->HasSpecificClassOf(isolate->heap()->Number_string()),
                  "v8::NumberObject::Cast()",
                  "Could not convert to NumberObject");
}


void v8::BooleanObject::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  i::Isolate* isolate = NULL;
  if (obj->IsHeapObject()) isolate = i::HeapObject::cast(*obj)->GetIsolate();
  Utils::ApiCheck(isolate != NULL &&
                  obj->HasSpecificClassOf(isolate->heap()->Boolean_string()),
                  "v8::BooleanObject::Cast()",
                  "Could not convert to BooleanObject");
}


void v8::RegExp::CheckCast(v8::Value* that) {
  i::Handle<i::Object> obj = Utils::OpenHandle(that);
  Utils::ApiCheck(obj->IsJSRegExp(),
                  "v8::RegExp::Cast()",
                  "Could not convert to regular expression");
}


Maybe<bool> Value::BooleanValue(Local<Context> context) const {
  return Just(Utils::OpenHandle(this)->BooleanValue());
}


bool Value::BooleanValue() const {
  return Utils::OpenHandle(this)->BooleanValue();
}


Maybe<double> Value::NumberValue(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) return Just(obj->Number());
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "NumberValue", double);
  i::Handle<i::Object> num;
  has_pending_exception = !i::Execution::ToNumber(isolate, obj).ToHandle(&num);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(double);
  return Just(num->Number());
}


double Value::NumberValue() const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) return obj->Number();
  return NumberValue(ContextFromHeapObject(obj))
      .FromMaybe(std::numeric_limits<double>::quiet_NaN());
}


Maybe<int64_t> Value::IntegerValue(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  i::Handle<i::Object> num;
  if (obj->IsNumber()) {
    num = obj;
  } else {
    PREPARE_FOR_EXECUTION_PRIMITIVE(context, "IntegerValue", int64_t);
    has_pending_exception =
        !i::Execution::ToInteger(isolate, obj).ToHandle(&num);
    RETURN_ON_FAILED_EXECUTION_PRIMITIVE(int64_t);
  }
  return Just(num->IsSmi() ? static_cast<int64_t>(i::Smi::cast(*num)->value())
                           : static_cast<int64_t>(num->Number()));
}


int64_t Value::IntegerValue() const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) {
    if (obj->IsSmi()) {
      return i::Smi::cast(*obj)->value();
    } else {
      return static_cast<int64_t>(obj->Number());
    }
  }
  return IntegerValue(ContextFromHeapObject(obj)).FromMaybe(0);
}


Maybe<int32_t> Value::Int32Value(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) return Just(NumberToInt32(*obj));
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "Int32Value", int32_t);
  i::Handle<i::Object> num;
  has_pending_exception = !i::Execution::ToInt32(isolate, obj).ToHandle(&num);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(int32_t);
  return Just(num->IsSmi() ? i::Smi::cast(*num)->value()
                           : static_cast<int32_t>(num->Number()));
}


int32_t Value::Int32Value() const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) return NumberToInt32(*obj);
  return Int32Value(ContextFromHeapObject(obj)).FromMaybe(0);
}


Maybe<uint32_t> Value::Uint32Value(Local<Context> context) const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) return Just(NumberToUint32(*obj));
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "Uint32Value", uint32_t);
  i::Handle<i::Object> num;
  has_pending_exception = !i::Execution::ToUint32(isolate, obj).ToHandle(&num);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(uint32_t);
  return Just(num->IsSmi() ? static_cast<uint32_t>(i::Smi::cast(*num)->value())
                           : static_cast<uint32_t>(num->Number()));
}


uint32_t Value::Uint32Value() const {
  auto obj = Utils::OpenHandle(this);
  if (obj->IsNumber()) return NumberToUint32(*obj);
  return Uint32Value(ContextFromHeapObject(obj)).FromMaybe(0);
}


MaybeLocal<Uint32> Value::ToArrayIndex(Local<Context> context) const {
  auto self = Utils::OpenHandle(this);
  if (self->IsSmi()) {
    if (i::Smi::cast(*self)->value() >= 0) return Utils::Uint32ToLocal(self);
    return Local<Uint32>();
  }
  PREPARE_FOR_EXECUTION(context, "ToArrayIndex", Uint32);
  i::Handle<i::Object> string_obj;
  has_pending_exception =
      !i::Execution::ToString(isolate, self).ToHandle(&string_obj);
  RETURN_ON_FAILED_EXECUTION(Uint32);
  i::Handle<i::String> str = i::Handle<i::String>::cast(string_obj);
  uint32_t index;
  if (str->AsArrayIndex(&index)) {
    i::Handle<i::Object> value;
    if (index <= static_cast<uint32_t>(i::Smi::kMaxValue)) {
      value = i::Handle<i::Object>(i::Smi::FromInt(index), isolate);
    } else {
      value = isolate->factory()->NewNumber(index);
    }
    RETURN_ESCAPED(Utils::Uint32ToLocal(value));
  }
  return Local<Uint32>();
}


Local<Uint32> Value::ToArrayIndex() const {
  auto self = Utils::OpenHandle(this);
  if (self->IsSmi()) {
    if (i::Smi::cast(*self)->value() >= 0) return Utils::Uint32ToLocal(self);
    return Local<Uint32>();
  }
  auto context = ContextFromHeapObject(self);
  RETURN_TO_LOCAL_UNCHECKED(ToArrayIndex(context), Uint32);
}


Maybe<bool> Value::Equals(Local<Context> context, Handle<Value> that) const {
  auto self = Utils::OpenHandle(this);
  auto other = Utils::OpenHandle(*that);
  if (self->IsSmi() && other->IsSmi()) {
    return Just(self->Number() == other->Number());
  }
  if (self->IsJSObject() && other->IsJSObject()) {
    return Just(*self == *other);
  }
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Value::Equals()", bool);
  i::Handle<i::Object> args[] = { other };
  i::Handle<i::Object> result;
  has_pending_exception =
      !CallV8HeapFunction(isolate, "EQUALS", self, arraysize(args), args)
           .ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(*result == i::Smi::FromInt(i::EQUAL));
}


bool Value::Equals(Handle<Value> that) const {
  auto self = Utils::OpenHandle(this);
  auto other = Utils::OpenHandle(*that);
  if (self->IsSmi() && other->IsSmi()) {
    return self->Number() == other->Number();
  }
  if (self->IsJSObject() && other->IsJSObject()) {
    return *self == *other;
  }
  auto heap_object = self->IsSmi() ? other : self;
  auto context = ContextFromHeapObject(heap_object);
  return Equals(context, that).FromMaybe(false);
}


bool Value::StrictEquals(Handle<Value> that) const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::Object> other = Utils::OpenHandle(*that);
  if (obj->IsSmi()) {
    return other->IsNumber() && obj->Number() == other->Number();
  }
  i::Isolate* isolate = i::HeapObject::cast(*obj)->GetIsolate();
  LOG_API(isolate, "StrictEquals");
  // Must check HeapNumber first, since NaN !== NaN.
  if (obj->IsHeapNumber()) {
    if (!other->IsNumber()) return false;
    double x = obj->Number();
    double y = other->Number();
    // Must check explicitly for NaN:s on Windows, but -0 works fine.
    return x == y && !std::isnan(x) && !std::isnan(y);
  } else if (*obj == *other) {  // Also covers Booleans.
    return true;
  } else if (obj->IsSmi()) {
    return other->IsNumber() && obj->Number() == other->Number();
  } else if (obj->IsString()) {
    return other->IsString() &&
        i::String::Equals(i::Handle<i::String>::cast(obj),
                          i::Handle<i::String>::cast(other));
  } else if (obj->IsUndefined() || obj->IsUndetectableObject()) {
    return other->IsUndefined() || other->IsUndetectableObject();
  } else {
    return false;
  }
}


bool Value::SameValue(Handle<Value> that) const {
  auto self = Utils::OpenHandle(this);
  auto other = Utils::OpenHandle(*that);
  return self->SameValue(*other);
}


Maybe<bool> v8::Object::Set(v8::Local<v8::Context> context,
                            v8::Local<Value> key, v8::Local<Value> value) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::Set()", bool);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  auto value_obj = Utils::OpenHandle(*value);
  has_pending_exception =
      i::Runtime::SetObjectProperty(isolate, self, key_obj, value_obj,
                                    i::SLOPPY).is_null();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(true);
}


bool v8::Object::Set(v8::Handle<Value> key, v8::Handle<Value> value) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return Set(context, key, value).FromMaybe(false);
}


Maybe<bool> v8::Object::Set(v8::Local<v8::Context> context, uint32_t index,
                            v8::Local<Value> value) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::Set()", bool);
  auto self = Utils::OpenHandle(this);
  auto value_obj = Utils::OpenHandle(*value);
  has_pending_exception = i::JSObject::SetElement(
      self, index, value_obj, NONE, i::SLOPPY).is_null();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(true);
}


bool v8::Object::Set(uint32_t index, v8::Handle<Value> value) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return Set(context, index, value).FromMaybe(false);
}


Maybe<bool> v8::Object::ForceSet(v8::Local<v8::Context> context,
                                 v8::Local<Value> key, v8::Local<Value> value,
                                 v8::PropertyAttribute attribs) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::Set()", bool);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  auto value_obj = Utils::OpenHandle(*value);
  has_pending_exception = i::Runtime::DefineObjectProperty(
      self,
      key_obj,
      value_obj,
      static_cast<PropertyAttributes>(attribs)).is_null();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(true);
}


bool v8::Object::ForceSet(v8::Handle<Value> key, v8::Handle<Value> value,
                          v8::PropertyAttribute attribs) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return ForceSet(context, key, value, attribs).FromMaybe(false);
}


bool v8::Object::SetPrivate(v8::Handle<Private> key, v8::Handle<Value> value) {
  return ForceSet(v8::Handle<Value>(reinterpret_cast<Value*>(*key)),
                  value, DontEnum);
}


namespace {

i::MaybeHandle<i::Object> DeleteObjectProperty(
    i::Isolate* isolate, i::Handle<i::JSReceiver> receiver,
    i::Handle<i::Object> key, i::LanguageMode language_mode) {
  // Check if the given key is an array index.
  uint32_t index;
  if (key->ToArrayIndex(&index)) {
    // In Firefox/SpiderMonkey, Safari and Opera you can access the
    // characters of a string using [] notation.  In the case of a
    // String object we just need to redirect the deletion to the
    // underlying string if the index is in range.  Since the
    // underlying string does nothing with the deletion, we can ignore
    // such deletions.
    if (receiver->IsStringObjectWithCharacterAt(index)) {
      return isolate->factory()->true_value();
    }

    return i::JSReceiver::DeleteElement(receiver, index, language_mode);
  }

  i::Handle<i::Name> name;
  if (key->IsName()) {
    name = i::Handle<i::Name>::cast(key);
  } else {
    // Call-back into JavaScript to convert the key to a string.
    i::Handle<i::Object> converted;
    if (!i::Execution::ToString(isolate, key).ToHandle(&converted)) {
      return i::MaybeHandle<i::Object>();
    }
    name = i::Handle<i::String>::cast(converted);
  }

  if (name->IsString()) {
    name = i::String::Flatten(i::Handle<i::String>::cast(name));
  }
  return i::JSReceiver::DeleteProperty(receiver, name, language_mode);
}

}  // namespace


MaybeLocal<Value> v8::Object::Get(Local<v8::Context> context,
                                  Local<Value> key) {
  PREPARE_FOR_EXECUTION(context, "v8::Object::Get()", Value);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  i::Handle<i::Object> result;
  has_pending_exception =
      !i::Runtime::GetObjectProperty(isolate, self, key_obj).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(Utils::ToLocal(result));
}


Local<Value> v8::Object::Get(v8::Handle<Value> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(Get(context, key), Value);
}


MaybeLocal<Value> v8::Object::Get(Local<Context> context, uint32_t index) {
  PREPARE_FOR_EXECUTION(context, "v8::Object::Get()", Value);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> result;
  has_pending_exception =
      !i::Object::GetElement(isolate, self, index).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(Utils::ToLocal(result));
}


Local<Value> v8::Object::Get(uint32_t index) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(Get(context, index), Value);
}


Local<Value> v8::Object::GetPrivate(v8::Handle<Private> key) {
  return Get(v8::Handle<Value>(reinterpret_cast<Value*>(*key)));
}


Maybe<PropertyAttribute> v8::Object::GetPropertyAttributes(
    Local<Context> context, Local<Value> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(
      context, "v8::Object::GetPropertyAttributes()", PropertyAttribute);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  if (!key_obj->IsName()) {
    has_pending_exception = !i::Execution::ToString(
        isolate, key_obj).ToHandle(&key_obj);
    RETURN_ON_FAILED_EXECUTION_PRIMITIVE(PropertyAttribute);
  }
  auto key_name = i::Handle<i::Name>::cast(key_obj);
  auto result = i::JSReceiver::GetPropertyAttributes(self, key_name);
  has_pending_exception = result.IsNothing();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(PropertyAttribute);
  if (result.FromJust() == ABSENT) {
    return Just(static_cast<PropertyAttribute>(NONE));
  }
  return Just(static_cast<PropertyAttribute>(result.FromJust()));
}


PropertyAttribute v8::Object::GetPropertyAttributes(v8::Handle<Value> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return GetPropertyAttributes(context, key)
      .FromMaybe(static_cast<PropertyAttribute>(NONE));
}


MaybeLocal<Value> v8::Object::GetOwnPropertyDescriptor(Local<Context> context,
                                                       Local<String> key) {
  PREPARE_FOR_EXECUTION(context, "v8::Object::GetOwnPropertyDescriptor()",
                        Value);
  auto obj = Utils::OpenHandle(this);
  auto key_name = Utils::OpenHandle(*key);
  i::Handle<i::Object> args[] = { obj, key_name };
  i::Handle<i::Object> result;
  has_pending_exception =
      !CallV8HeapFunction(isolate, "ObjectGetOwnPropertyDescriptor",
                          isolate->factory()->undefined_value(),
                          arraysize(args), args).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(Utils::ToLocal(result));
}


Local<Value> v8::Object::GetOwnPropertyDescriptor(Local<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(GetOwnPropertyDescriptor(context, key), Value);
}


Local<Value> v8::Object::GetPrototype() {
  auto isolate = Utils::OpenHandle(this)->GetIsolate();
  auto self = Utils::OpenHandle(this);
  i::PrototypeIterator iter(isolate, self);
  return Utils::ToLocal(i::PrototypeIterator::GetCurrent(iter));
}


Maybe<bool> v8::Object::SetPrototype(Local<Context> context,
                                     Local<Value> value) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::SetPrototype()", bool);
  auto self = Utils::OpenHandle(this);
  auto value_obj = Utils::OpenHandle(*value);
  // We do not allow exceptions thrown while setting the prototype
  // to propagate outside.
  TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  auto result = i::JSObject::SetPrototype(self, value_obj, false);
  has_pending_exception = result.is_null();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(true);
}


bool v8::Object::SetPrototype(Handle<Value> value) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return SetPrototype(context, value).FromMaybe(false);
}


Local<Object> v8::Object::FindInstanceInPrototypeChain(
    v8::Handle<FunctionTemplate> tmpl) {
  auto isolate = Utils::OpenHandle(this)->GetIsolate();
  i::PrototypeIterator iter(isolate, *Utils::OpenHandle(this),
                            i::PrototypeIterator::START_AT_RECEIVER);
  auto tmpl_info = *Utils::OpenHandle(*tmpl);
  while (!tmpl_info->IsTemplateFor(iter.GetCurrent())) {
    iter.Advance();
    if (iter.IsAtEnd()) {
      return Local<Object>();
    }
  }
  return Utils::ToLocal(
      i::handle(i::JSObject::cast(iter.GetCurrent()), isolate));
}


MaybeLocal<Array> v8::Object::GetPropertyNames(Local<Context> context) {
  PREPARE_FOR_EXECUTION(context, "v8::Object::GetPropertyNames()", Array);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::FixedArray> value;
  has_pending_exception = !i::JSReceiver::GetKeys(
      self, i::JSReceiver::INCLUDE_PROTOS).ToHandle(&value);
  RETURN_ON_FAILED_EXECUTION(Array);
  // Because we use caching to speed up enumeration it is important
  // to never change the result of the basic enumeration function so
  // we clone the result.
  auto elms = isolate->factory()->CopyFixedArray(value);
  auto result = isolate->factory()->NewJSArrayWithElements(elms);
  RETURN_ESCAPED(Utils::ToLocal(result));
}


Local<Array> v8::Object::GetPropertyNames() {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(GetPropertyNames(context), Array);
}


MaybeLocal<Array> v8::Object::GetOwnPropertyNames(Local<Context> context) {
  PREPARE_FOR_EXECUTION(context, "v8::Object::GetOwnPropertyNames()", Array);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::FixedArray> value;
  has_pending_exception = !i::JSReceiver::GetKeys(
      self, i::JSReceiver::OWN_ONLY).ToHandle(&value);
  RETURN_ON_FAILED_EXECUTION(Array);
  // Because we use caching to speed up enumeration it is important
  // to never change the result of the basic enumeration function so
  // we clone the result.
  auto elms = isolate->factory()->CopyFixedArray(value);
  auto result = isolate->factory()->NewJSArrayWithElements(elms);
  RETURN_ESCAPED(Utils::ToLocal(result));
}


Local<Array> v8::Object::GetOwnPropertyNames() {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(GetOwnPropertyNames(context), Array);
}


MaybeLocal<String> v8::Object::ObjectProtoToString(Local<Context> context) {
  auto self = Utils::OpenHandle(this);
  auto isolate = self->GetIsolate();
  auto v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  i::Handle<i::Object> name(self->class_name(), isolate);
  i::Handle<i::Object> tag;

  // Native implementation of Object.prototype.toString (v8natives.js):
  //   var c = %_ClassOf(this);
  //   if (c === 'Arguments') c  = 'Object';
  //   return "[object " + c + "]";

  if (!name->IsString()) {
    return v8::String::NewFromUtf8(v8_isolate, "[object ]");
  }
  auto class_name = i::Handle<i::String>::cast(name);
  if (i::String::Equals(class_name, isolate->factory()->Arguments_string())) {
    return v8::String::NewFromUtf8(v8_isolate, "[object Object]");
  }
  if (internal::FLAG_harmony_tostring) {
    PREPARE_FOR_EXECUTION(context, "v8::Object::ObjectProtoToString()", String);
    auto toStringTag = isolate->factory()->to_string_tag_symbol();
    has_pending_exception = !i::Runtime::GetObjectProperty(
                                 isolate, self, toStringTag).ToHandle(&tag);
    RETURN_ON_FAILED_EXECUTION(String);
    if (tag->IsString()) {
      class_name = i::Handle<i::String>::cast(tag).EscapeFrom(&handle_scope);
    }
  }
  const char* prefix = "[object ";
  Local<String> str = Utils::ToLocal(class_name);
  const char* postfix = "]";

  int prefix_len = i::StrLength(prefix);
  int str_len = str->Utf8Length();
  int postfix_len = i::StrLength(postfix);

  int buf_len = prefix_len + str_len + postfix_len;
  i::ScopedVector<char> buf(buf_len);

  // Write prefix.
  char* ptr = buf.start();
  i::MemCopy(ptr, prefix, prefix_len * v8::internal::kCharSize);
  ptr += prefix_len;

  // Write real content.
  str->WriteUtf8(ptr, str_len);
  ptr += str_len;

  // Write postfix.
  i::MemCopy(ptr, postfix, postfix_len * v8::internal::kCharSize);

  // Copy the buffer into a heap-allocated string and return it.
  return v8::String::NewFromUtf8(v8_isolate, buf.start(), String::kNormalString,
                                 buf_len);
}


Local<String> v8::Object::ObjectProtoToString() {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(ObjectProtoToString(context), String);
}


Local<String> v8::Object::GetConstructorName() {
  auto self = Utils::OpenHandle(this);
  i::Handle<i::String> name(self->constructor_name());
  return Utils::ToLocal(name);
}


Maybe<bool> v8::Object::Delete(Local<Context> context, Local<Value> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::Delete()", bool);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  i::Handle<i::Object> obj;
  has_pending_exception =
      !DeleteObjectProperty(isolate, self, key_obj, i::SLOPPY).ToHandle(&obj);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(obj->IsTrue());
}


bool v8::Object::Delete(v8::Handle<Value> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return Delete(context, key).FromMaybe(false);
}


bool v8::Object::DeletePrivate(v8::Handle<Private> key) {
  return Delete(v8::Handle<Value>(reinterpret_cast<Value*>(*key)));
}


Maybe<bool> v8::Object::Has(Local<Context> context, Local<Value> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::Get()", bool);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  Maybe<bool> maybe = Nothing<bool>();
  // Check if the given key is an array index.
  uint32_t index;
  if (key_obj->ToArrayIndex(&index)) {
    maybe = i::JSReceiver::HasElement(self, index);
  } else {
    // Convert the key to a name - possibly by calling back into JavaScript.
    i::Handle<i::Name> name;
    if (i::Runtime::ToName(isolate, key_obj).ToHandle(&name)) {
      maybe = i::JSReceiver::HasProperty(self, name);
    }
  }
  has_pending_exception = maybe.IsNothing();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return maybe;
}


bool v8::Object::Has(v8::Handle<Value> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return Has(context, key).FromMaybe(false);
}


bool v8::Object::HasPrivate(v8::Handle<Private> key) {
  // TODO(rossberg): this should use HasOwnProperty, but we'd need to
  // generalise that to a (noy yet existant) Name argument first.
  return Has(v8::Handle<Value>(reinterpret_cast<Value*>(*key)));
}


Maybe<bool> v8::Object::Delete(Local<Context> context, uint32_t index) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::DeleteProperty()",
                                  bool);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> obj;
  has_pending_exception =
      !i::JSReceiver::DeleteElement(self, index).ToHandle(&obj);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(obj->IsTrue());
}


bool v8::Object::Delete(uint32_t index) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return Delete(context, index).FromMaybe(false);
}


Maybe<bool> v8::Object::Has(Local<Context> context, uint32_t index) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::Get()", bool);
  auto self = Utils::OpenHandle(this);
  auto maybe = i::JSReceiver::HasElement(self, index);
  has_pending_exception = maybe.IsNothing();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return maybe;
}


bool v8::Object::Has(uint32_t index) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return Has(context, index).FromMaybe(false);
}


template <typename Getter, typename Setter, typename Data>
static Maybe<bool> ObjectSetAccessor(Local<Context> context, Object* obj,
                                     Local<Name> name, Getter getter,
                                     Setter setter, Data data,
                                     AccessControl settings,
                                     PropertyAttribute attributes) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::SetAccessor()", bool);
  v8::Handle<AccessorSignature> signature;
  auto info = MakeAccessorInfo(name, getter, setter, data, settings, attributes,
                               signature);
  if (info.is_null()) return Nothing<bool>();
  bool fast = Utils::OpenHandle(obj)->HasFastProperties();
  i::Handle<i::Object> result;
  has_pending_exception =
      !i::JSObject::SetAccessor(Utils::OpenHandle(obj), info).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  if (result->IsUndefined()) return Nothing<bool>();
  if (fast) {
    i::JSObject::MigrateSlowToFast(Utils::OpenHandle(obj), 0, "APISetAccessor");
  }
  return Just(true);
}


Maybe<bool> Object::SetAccessor(Local<Context> context, Local<Name> name,
                                AccessorNameGetterCallback getter,
                                AccessorNameSetterCallback setter,
                                MaybeLocal<Value> data, AccessControl settings,
                                PropertyAttribute attribute) {
  return ObjectSetAccessor(context, this, name, getter, setter,
                           data.FromMaybe(Local<Value>()), settings, attribute);
}


bool Object::SetAccessor(Handle<String> name,
                         AccessorGetterCallback getter,
                         AccessorSetterCallback setter,
                         v8::Handle<Value> data,
                         AccessControl settings,
                         PropertyAttribute attributes) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return ObjectSetAccessor(context, this, name, getter, setter, data, settings,
                           attributes).FromMaybe(false);
}


bool Object::SetAccessor(Handle<Name> name,
                         AccessorNameGetterCallback getter,
                         AccessorNameSetterCallback setter,
                         v8::Handle<Value> data,
                         AccessControl settings,
                         PropertyAttribute attributes) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return ObjectSetAccessor(context, this, name, getter, setter, data, settings,
                           attributes).FromMaybe(false);
}


void Object::SetAccessorProperty(Local<Name> name,
                                 Local<Function> getter,
                                 Handle<Function> setter,
                                 PropertyAttribute attribute,
                                 AccessControl settings) {
  // TODO(verwaest): Remove |settings|.
  DCHECK_EQ(v8::DEFAULT, settings);
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::Object> getter_i = v8::Utils::OpenHandle(*getter);
  i::Handle<i::Object> setter_i = v8::Utils::OpenHandle(*setter, true);
  if (setter_i.is_null()) setter_i = isolate->factory()->null_value();
  i::JSObject::DefineAccessor(v8::Utils::OpenHandle(this),
                              v8::Utils::OpenHandle(*name),
                              getter_i,
                              setter_i,
                              static_cast<PropertyAttributes>(attribute));
}


Maybe<bool> v8::Object::HasOwnProperty(Local<Context> context,
                                       Local<Name> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::HasOwnProperty()",
                                  bool);
  auto self = Utils::OpenHandle(this);
  auto key_val = Utils::OpenHandle(*key);
  auto result = i::JSReceiver::HasOwnProperty(self, key_val);
  has_pending_exception = result.IsNothing();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return result;
}


bool v8::Object::HasOwnProperty(Handle<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return HasOwnProperty(context, key).FromMaybe(false);
}


Maybe<bool> v8::Object::HasRealNamedProperty(Local<Context> context,
                                             Local<Name> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "v8::Object::HasRealNamedProperty()",
                                  bool);
  auto self = Utils::OpenHandle(this);
  auto key_val = Utils::OpenHandle(*key);
  auto result = i::JSObject::HasRealNamedProperty(self, key_val);
  has_pending_exception = result.IsNothing();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return result;
}


bool v8::Object::HasRealNamedProperty(Handle<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return HasRealNamedProperty(context, key).FromMaybe(false);
}


Maybe<bool> v8::Object::HasRealIndexedProperty(Local<Context> context,
                                               uint32_t index) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context,
                                  "v8::Object::HasRealIndexedProperty()", bool);
  auto self = Utils::OpenHandle(this);
  auto result = i::JSObject::HasRealElementProperty(self, index);
  has_pending_exception = result.IsNothing();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return result;
}


bool v8::Object::HasRealIndexedProperty(uint32_t index) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return HasRealIndexedProperty(context, index).FromMaybe(false);
}


Maybe<bool> v8::Object::HasRealNamedCallbackProperty(Local<Context> context,
                                                     Local<Name> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(
      context, "v8::Object::HasRealNamedCallbackProperty()", bool);
  auto self = Utils::OpenHandle(this);
  auto key_val = Utils::OpenHandle(*key);
  auto result = i::JSObject::HasRealNamedCallbackProperty(self, key_val);
  has_pending_exception = result.IsNothing();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return result;
}


bool v8::Object::HasRealNamedCallbackProperty(Handle<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return HasRealNamedCallbackProperty(context, key).FromMaybe(false);
}


bool v8::Object::HasNamedLookupInterceptor() {
  auto self = Utils::OpenHandle(this);
  return self->HasNamedInterceptor();
}


bool v8::Object::HasIndexedLookupInterceptor() {
  auto self = Utils::OpenHandle(this);
  return self->HasIndexedInterceptor();
}


MaybeLocal<Value> v8::Object::GetRealNamedPropertyInPrototypeChain(
    Local<Context> context, Local<Name> key) {
  PREPARE_FOR_EXECUTION(
      context, "v8::Object::GetRealNamedPropertyInPrototypeChain()", Value);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  i::PrototypeIterator iter(isolate, self);
  if (iter.IsAtEnd()) return MaybeLocal<Value>();
  auto proto = i::PrototypeIterator::GetCurrent(iter);
  i::LookupIterator it(self, key_obj, i::Handle<i::JSReceiver>::cast(proto),
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  if (!it.IsFound()) return MaybeLocal<Value>();
  Local<Value> result;
  has_pending_exception = !ToLocal<Value>(i::Object::GetProperty(&it), &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<Value> v8::Object::GetRealNamedPropertyInPrototypeChain(
    Handle<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(GetRealNamedPropertyInPrototypeChain(context, key),
                            Value);
}


Maybe<PropertyAttribute>
v8::Object::GetRealNamedPropertyAttributesInPrototypeChain(
    Local<Context> context, Local<Name> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(
      context, "v8::Object::GetRealNamedPropertyAttributesInPrototypeChain()",
      PropertyAttribute);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  i::PrototypeIterator iter(isolate, self);
  if (iter.IsAtEnd()) return Nothing<PropertyAttribute>();
  auto proto = i::PrototypeIterator::GetCurrent(iter);
  i::LookupIterator it(self, key_obj, i::Handle<i::JSReceiver>::cast(proto),
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  if (!it.IsFound()) return Nothing<PropertyAttribute>();
  auto result = i::JSReceiver::GetPropertyAttributes(&it);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(PropertyAttribute);
  if (result.FromJust() == ABSENT) {
    return Just(static_cast<PropertyAttribute>(NONE));
  }
  return Just<PropertyAttribute>(
      static_cast<PropertyAttribute>(result.FromJust()));
}


Maybe<PropertyAttribute>
v8::Object::GetRealNamedPropertyAttributesInPrototypeChain(Handle<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return GetRealNamedPropertyAttributesInPrototypeChain(context, key);
}


MaybeLocal<Value> v8::Object::GetRealNamedProperty(Local<Context> context,
                                                   Local<Name> key) {
  PREPARE_FOR_EXECUTION(
      context, "v8::Object::GetRealNamedPropertyInPrototypeChain()", Value);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  i::LookupIterator it(self, key_obj,
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  if (!it.IsFound()) return MaybeLocal<Value>();
  Local<Value> result;
  has_pending_exception = !ToLocal<Value>(i::Object::GetProperty(&it), &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<Value> v8::Object::GetRealNamedProperty(Handle<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(GetRealNamedProperty(context, key), Value);
}


Maybe<PropertyAttribute> v8::Object::GetRealNamedPropertyAttributes(
    Local<Context> context, Local<Name> key) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(
      context, "v8::Object::GetRealNamedPropertyAttributes()",
      PropertyAttribute);
  auto self = Utils::OpenHandle(this);
  auto key_obj = Utils::OpenHandle(*key);
  i::LookupIterator it(self, key_obj,
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  if (!it.IsFound()) return Nothing<PropertyAttribute>();
  auto result = i::JSReceiver::GetPropertyAttributes(&it);
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(PropertyAttribute);
  if (result.FromJust() == ABSENT) {
    return Just(static_cast<PropertyAttribute>(NONE));
  }
  return Just<PropertyAttribute>(
      static_cast<PropertyAttribute>(result.FromJust()));
}


Maybe<PropertyAttribute> v8::Object::GetRealNamedPropertyAttributes(
    Handle<String> key) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  return GetRealNamedPropertyAttributes(context, key);
}


// Turns on access checks by copying the map and setting the check flag.
// Because the object gets a new map, existing inline cache caching
// the old map of this object will fail.
void v8::Object::TurnOnAccessCheck() {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);

  // When turning on access checks for a global object deoptimize all functions
  // as optimized code does not always handle access checks.
  i::Deoptimizer::DeoptimizeGlobalObject(*obj);

  i::Handle<i::Map> new_map =
      i::Map::Copy(i::Handle<i::Map>(obj->map()), "APITurnOnAccessCheck");
  new_map->set_is_access_check_needed(true);
  i::JSObject::MigrateToMap(obj, new_map);
}


Local<v8::Object> v8::Object::Clone() {
  auto self = Utils::OpenHandle(this);
  auto isolate = self->GetIsolate();
  ENTER_V8(isolate);
  auto result = isolate->factory()->CopyJSObject(self);
  CHECK(!result.is_null());
  return Utils::ToLocal(result);
}


Local<v8::Context> v8::Object::CreationContext() {
  auto self = Utils::OpenHandle(this);
  auto context = handle(self->GetCreationContext());
  return Utils::ToLocal(context);
}


int v8::Object::GetIdentityHash() {
  auto isolate = Utils::OpenHandle(this)->GetIsolate();
  i::HandleScope scope(isolate);
  auto self = Utils::OpenHandle(this);
  return i::JSReceiver::GetOrCreateIdentityHash(self)->value();
}


bool v8::Object::SetHiddenValue(v8::Handle<v8::String> key,
                                v8::Handle<v8::Value> value) {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  if (value.IsEmpty()) return DeleteHiddenValue(key);
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);
  i::Handle<i::String> key_obj = Utils::OpenHandle(*key);
  i::Handle<i::String> key_string =
      isolate->factory()->InternalizeString(key_obj);
  i::Handle<i::Object> value_obj = Utils::OpenHandle(*value);
  i::Handle<i::Object> result =
      i::JSObject::SetHiddenProperty(self, key_string, value_obj);
  return *result == *self;
}


v8::Local<v8::Value> v8::Object::GetHiddenValue(v8::Handle<v8::String> key) {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);
  i::Handle<i::String> key_obj = Utils::OpenHandle(*key);
  i::Handle<i::String> key_string =
      isolate->factory()->InternalizeString(key_obj);
  i::Handle<i::Object> result(self->GetHiddenProperty(key_string), isolate);
  if (result->IsTheHole()) return v8::Local<v8::Value>();
  return Utils::ToLocal(result);
}


bool v8::Object::DeleteHiddenValue(v8::Handle<v8::String> key) {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::JSObject> self = Utils::OpenHandle(this);
  i::Handle<i::String> key_obj = Utils::OpenHandle(*key);
  i::Handle<i::String> key_string =
      isolate->factory()->InternalizeString(key_obj);
  i::JSObject::DeleteHiddenProperty(self, key_string);
  return true;
}


namespace {

static i::ElementsKind GetElementsKindFromExternalArrayType(
    ExternalArrayType array_type) {
  switch (array_type) {
#define ARRAY_TYPE_TO_ELEMENTS_KIND(Type, type, TYPE, ctype, size)            \
    case kExternal##Type##Array:                                              \
      return i::EXTERNAL_##TYPE##_ELEMENTS;

    TYPED_ARRAYS(ARRAY_TYPE_TO_ELEMENTS_KIND)
#undef ARRAY_TYPE_TO_ELEMENTS_KIND
  }
  UNREACHABLE();
  return i::DICTIONARY_ELEMENTS;
}


void PrepareExternalArrayElements(i::Handle<i::JSObject> object,
                                  void* data,
                                  ExternalArrayType array_type,
                                  int length) {
  i::Isolate* isolate = object->GetIsolate();
  i::Handle<i::ExternalArray> array =
      isolate->factory()->NewExternalArray(length, array_type, data);

  i::Handle<i::Map> external_array_map =
      i::JSObject::GetElementsTransitionMap(
          object,
          GetElementsKindFromExternalArrayType(array_type));

  i::JSObject::SetMapAndElements(object, external_array_map, array);
}

}  // namespace


void v8::Object::SetIndexedPropertiesToPixelData(uint8_t* data, int length) {
  auto self = Utils::OpenHandle(this);
  auto isolate = self->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  if (!Utils::ApiCheck(length >= 0 &&
                       length <= i::ExternalUint8ClampedArray::kMaxLength,
                       "v8::Object::SetIndexedPropertiesToPixelData()",
                       "length exceeds max acceptable value")) {
    return;
  }
  if (!Utils::ApiCheck(!self->IsJSArray(),
                       "v8::Object::SetIndexedPropertiesToPixelData()",
                       "JSArray is not supported")) {
    return;
  }
  PrepareExternalArrayElements(self, data, kExternalUint8ClampedArray, length);
}


bool v8::Object::HasIndexedPropertiesInPixelData() {
  auto self = Utils::OpenHandle(this);
  return self->HasExternalUint8ClampedElements();
}


uint8_t* v8::Object::GetIndexedPropertiesPixelData() {
  auto self = Utils::OpenHandle(this);
  if (self->HasExternalUint8ClampedElements()) {
    return i::ExternalUint8ClampedArray::cast(self->elements())->
        external_uint8_clamped_pointer();
  }
  return nullptr;
}


int v8::Object::GetIndexedPropertiesPixelDataLength() {
  auto self = Utils::OpenHandle(this);
  if (self->HasExternalUint8ClampedElements()) {
    return i::ExternalUint8ClampedArray::cast(self->elements())->length();
  }
  return -1;
}


void v8::Object::SetIndexedPropertiesToExternalArrayData(
    void* data,
    ExternalArrayType array_type,
    int length) {
  auto self = Utils::OpenHandle(this);
  auto isolate = self->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  if (!Utils::ApiCheck(length >= 0 && length <= i::ExternalArray::kMaxLength,
                       "v8::Object::SetIndexedPropertiesToExternalArrayData()",
                       "length exceeds max acceptable value")) {
    return;
  }
  if (!Utils::ApiCheck(!self->IsJSArray(),
                       "v8::Object::SetIndexedPropertiesToExternalArrayData()",
                       "JSArray is not supported")) {
    return;
  }
  PrepareExternalArrayElements(self, data, array_type, length);
}


bool v8::Object::HasIndexedPropertiesInExternalArrayData() {
  auto self = Utils::OpenHandle(this);
  return self->HasExternalArrayElements();
}


void* v8::Object::GetIndexedPropertiesExternalArrayData() {
  auto self = Utils::OpenHandle(this);
  if (self->HasExternalArrayElements()) {
    return i::ExternalArray::cast(self->elements())->external_pointer();
  }
  return nullptr;
}


ExternalArrayType v8::Object::GetIndexedPropertiesExternalArrayDataType() {
  auto self = Utils::OpenHandle(this);
  switch (self->elements()->map()->instance_type()) {
#define INSTANCE_TYPE_TO_ARRAY_TYPE(Type, type, TYPE, ctype, size)            \
    case i::EXTERNAL_##TYPE##_ARRAY_TYPE:                                     \
      return kExternal##Type##Array;
    TYPED_ARRAYS(INSTANCE_TYPE_TO_ARRAY_TYPE)
#undef INSTANCE_TYPE_TO_ARRAY_TYPE
    default:
      return static_cast<ExternalArrayType>(-1);
  }
}


int v8::Object::GetIndexedPropertiesExternalArrayDataLength() {
  auto self = Utils::OpenHandle(this);
  if (self->HasExternalArrayElements()) {
    return i::ExternalArray::cast(self->elements())->length();
  }
  return -1;
}


bool v8::Object::IsCallable() {
  auto self = Utils::OpenHandle(this);
  return self->IsCallable();
}


MaybeLocal<Value> Object::CallAsFunction(Local<Context> context,
                                         Handle<Value> recv, int argc,
                                         Handle<Value> argv[]) {
  PREPARE_FOR_EXECUTION_WITH_CALLBACK(context, "v8::Object::CallAsFunction()",
                                      Value);
  i::TimerEventScope<i::TimerEventExecute> timer_scope(isolate);
  auto self = Utils::OpenHandle(this);
  auto recv_obj = Utils::OpenHandle(*recv);
  STATIC_ASSERT(sizeof(v8::Handle<v8::Value>) == sizeof(i::Object**));
  i::Handle<i::Object>* args = reinterpret_cast<i::Handle<i::Object>*>(argv);
  i::Handle<i::JSFunction> fun;
  if (self->IsJSFunction()) {
    fun = i::Handle<i::JSFunction>::cast(self);
  } else {
    i::Handle<i::Object> delegate;
    has_pending_exception = !i::Execution::TryGetFunctionDelegate(isolate, self)
                                 .ToHandle(&delegate);
    RETURN_ON_FAILED_EXECUTION(Value);
    fun = i::Handle<i::JSFunction>::cast(delegate);
    recv_obj = self;
  }
  Local<Value> result;
  has_pending_exception =
      !ToLocal<Value>(
          i::Execution::Call(isolate, fun, recv_obj, argc, args, true),
          &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<v8::Value> Object::CallAsFunction(v8::Handle<v8::Value> recv, int argc,
                                        v8::Handle<v8::Value> argv[]) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  Local<Value>* argv_cast = reinterpret_cast<Local<Value>*>(argv);
  RETURN_TO_LOCAL_UNCHECKED(CallAsFunction(context, recv, argc, argv_cast),
                            Value);
}


MaybeLocal<Value> Object::CallAsConstructor(Local<Context> context, int argc,
                                            Local<Value> argv[]) {
  PREPARE_FOR_EXECUTION_WITH_CALLBACK(context,
                                      "v8::Object::CallAsConstructor()", Value);
  i::TimerEventScope<i::TimerEventExecute> timer_scope(isolate);
  auto self = Utils::OpenHandle(this);
  STATIC_ASSERT(sizeof(v8::Handle<v8::Value>) == sizeof(i::Object**));
  i::Handle<i::Object>* args = reinterpret_cast<i::Handle<i::Object>*>(argv);
  if (self->IsJSFunction()) {
    auto fun = i::Handle<i::JSFunction>::cast(self);
    Local<Value> result;
    has_pending_exception =
        !ToLocal<Value>(i::Execution::New(fun, argc, args), &result);
    RETURN_ON_FAILED_EXECUTION(Value);
    RETURN_ESCAPED(result);
  }
  i::Handle<i::Object> delegate;
  has_pending_exception = !i::Execution::TryGetConstructorDelegate(
                               isolate, self).ToHandle(&delegate);
  RETURN_ON_FAILED_EXECUTION(Value);
  if (!delegate->IsUndefined()) {
    auto fun = i::Handle<i::JSFunction>::cast(delegate);
    Local<Value> result;
    has_pending_exception =
        !ToLocal<Value>(i::Execution::Call(isolate, fun, self, argc, args),
                        &result);
    RETURN_ON_FAILED_EXECUTION(Value);
    DCHECK(!delegate->IsUndefined());
    RETURN_ESCAPED(result);
  }
  return MaybeLocal<Value>();
}


Local<v8::Value> Object::CallAsConstructor(int argc,
                                           v8::Handle<v8::Value> argv[]) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  Local<Value>* argv_cast = reinterpret_cast<Local<Value>*>(argv);
  RETURN_TO_LOCAL_UNCHECKED(CallAsConstructor(context, argc, argv_cast), Value);
}


Local<Function> Function::New(Isolate* v8_isolate,
                              FunctionCallback callback,
                              Local<Value> data,
                              int length) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  LOG_API(isolate, "Function::New");
  ENTER_V8(isolate);
  return FunctionTemplateNew(
      isolate, callback, data, Local<Signature>(), length, true)->
      GetFunction();
}


Local<v8::Object> Function::NewInstance() const {
  return NewInstance(0, NULL);
}


MaybeLocal<Object> Function::NewInstance(Local<Context> context, int argc,
                                         v8::Handle<v8::Value> argv[]) const {
  PREPARE_FOR_EXECUTION_WITH_CALLBACK(context, "v8::Function::NewInstance()",
                                      Object);
  i::TimerEventScope<i::TimerEventExecute> timer_scope(isolate);
  auto self = Utils::OpenHandle(this);
  STATIC_ASSERT(sizeof(v8::Handle<v8::Value>) == sizeof(i::Object**));
  i::Handle<i::Object>* args = reinterpret_cast<i::Handle<i::Object>*>(argv);
  Local<Object> result;
  has_pending_exception =
      !ToLocal<Object>(i::Execution::New(self, argc, args), &result);
  RETURN_ON_FAILED_EXECUTION(Object);
  RETURN_ESCAPED(result);
}


Local<v8::Object> Function::NewInstance(int argc,
                                        v8::Handle<v8::Value> argv[]) const {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(NewInstance(context, argc, argv), Object);
}


MaybeLocal<v8::Value> Function::Call(Local<Context> context,
                                     v8::Handle<v8::Value> recv, int argc,
                                     v8::Handle<v8::Value> argv[]) {
  PREPARE_FOR_EXECUTION_WITH_CALLBACK(context, "v8::Function::Call()", Value);
  i::TimerEventScope<i::TimerEventExecute> timer_scope(isolate);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> recv_obj = Utils::OpenHandle(*recv);
  STATIC_ASSERT(sizeof(v8::Handle<v8::Value>) == sizeof(i::Object**));
  i::Handle<i::Object>* args = reinterpret_cast<i::Handle<i::Object>*>(argv);
  Local<Value> result;
  has_pending_exception =
      !ToLocal<Value>(
          i::Execution::Call(isolate, self, recv_obj, argc, args, true),
          &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<v8::Value> Function::Call(v8::Handle<v8::Value> recv, int argc,
                                v8::Handle<v8::Value> argv[]) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(Call(context, recv, argc, argv), Value);
}


void Function::SetName(v8::Handle<v8::String> name) {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  func->shared()->set_name(*Utils::OpenHandle(*name));
}


Handle<Value> Function::GetName() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  return Utils::ToLocal(i::Handle<i::Object>(func->shared()->name(),
                                             func->GetIsolate()));
}


Handle<Value> Function::GetInferredName() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  return Utils::ToLocal(i::Handle<i::Object>(func->shared()->inferred_name(),
                                             func->GetIsolate()));
}


Handle<Value> Function::GetDisplayName() const {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  ENTER_V8(isolate);
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  i::Handle<i::String> property_name =
      isolate->factory()->NewStringFromStaticChars("displayName");
  i::Handle<i::Object> value =
      i::JSObject::GetDataProperty(func, property_name);
  if (value->IsString()) {
    i::Handle<i::String> name = i::Handle<i::String>::cast(value);
    if (name->length() > 0) return Utils::ToLocal(name);
  }
  return ToApiHandle<Primitive>(isolate->factory()->undefined_value());
}


ScriptOrigin Function::GetScriptOrigin() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  if (func->shared()->script()->IsScript()) {
    i::Handle<i::Script> script(i::Script::cast(func->shared()->script()));
    return GetScriptOriginForScript(func->GetIsolate(), script);
  }
  return v8::ScriptOrigin(Handle<Value>());
}


const int Function::kLineOffsetNotFound = -1;


int Function::GetScriptLineNumber() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  if (func->shared()->script()->IsScript()) {
    i::Handle<i::Script> script(i::Script::cast(func->shared()->script()));
    return i::Script::GetLineNumber(script, func->shared()->start_position());
  }
  return kLineOffsetNotFound;
}


int Function::GetScriptColumnNumber() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  if (func->shared()->script()->IsScript()) {
    i::Handle<i::Script> script(i::Script::cast(func->shared()->script()));
    return i::Script::GetColumnNumber(script, func->shared()->start_position());
  }
  return kLineOffsetNotFound;
}


bool Function::IsBuiltin() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  return func->IsBuiltin();
}


int Function::ScriptId() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  if (!func->shared()->script()->IsScript()) {
    return v8::UnboundScript::kNoScriptId;
  }
  i::Handle<i::Script> script(i::Script::cast(func->shared()->script()));
  return script->id()->value();
}


Local<v8::Value> Function::GetBoundFunction() const {
  i::Handle<i::JSFunction> func = Utils::OpenHandle(this);
  if (!func->shared()->bound()) {
    return v8::Undefined(reinterpret_cast<v8::Isolate*>(func->GetIsolate()));
  }
  i::Handle<i::FixedArray> bound_args = i::Handle<i::FixedArray>(
      i::FixedArray::cast(func->function_bindings()));
  i::Handle<i::Object> original(
      bound_args->get(i::JSFunction::kBoundFunctionIndex),
      func->GetIsolate());
  return Utils::ToLocal(i::Handle<i::JSFunction>::cast(original));
}


int Name::GetIdentityHash() {
  auto self = Utils::OpenHandle(this);
  return static_cast<int>(self->Hash());
}


int String::Length() const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  return str->length();
}


bool String::IsOneByte() const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  return str->HasOnlyOneByteChars();
}


// Helpers for ContainsOnlyOneByteHelper
template<size_t size> struct OneByteMask;
template<> struct OneByteMask<4> {
  static const uint32_t value = 0xFF00FF00;
};
template<> struct OneByteMask<8> {
  static const uint64_t value = V8_2PART_UINT64_C(0xFF00FF00, FF00FF00);
};
static const uintptr_t kOneByteMask = OneByteMask<sizeof(uintptr_t)>::value;
static const uintptr_t kAlignmentMask = sizeof(uintptr_t) - 1;
static inline bool Unaligned(const uint16_t* chars) {
  return reinterpret_cast<const uintptr_t>(chars) & kAlignmentMask;
}


static inline const uint16_t* Align(const uint16_t* chars) {
  return reinterpret_cast<uint16_t*>(
      reinterpret_cast<uintptr_t>(chars) & ~kAlignmentMask);
}

class ContainsOnlyOneByteHelper {
 public:
  ContainsOnlyOneByteHelper() : is_one_byte_(true) {}
  bool Check(i::String* string) {
    i::ConsString* cons_string = i::String::VisitFlat(this, string, 0);
    if (cons_string == NULL) return is_one_byte_;
    return CheckCons(cons_string);
  }
  void VisitOneByteString(const uint8_t* chars, int length) {
    // Nothing to do.
  }
  void VisitTwoByteString(const uint16_t* chars, int length) {
    // Accumulated bits.
    uintptr_t acc = 0;
    // Align to uintptr_t.
    const uint16_t* end = chars + length;
    while (Unaligned(chars) && chars != end) {
      acc |= *chars++;
    }
    // Read word aligned in blocks,
    // checking the return value at the end of each block.
    const uint16_t* aligned_end = Align(end);
    const int increment = sizeof(uintptr_t)/sizeof(uint16_t);
    const int inner_loops = 16;
    while (chars + inner_loops*increment < aligned_end) {
      for (int i = 0; i < inner_loops; i++) {
        acc |= *reinterpret_cast<const uintptr_t*>(chars);
        chars += increment;
      }
      // Check for early return.
      if ((acc & kOneByteMask) != 0) {
        is_one_byte_ = false;
        return;
      }
    }
    // Read the rest.
    while (chars != end) {
      acc |= *chars++;
    }
    // Check result.
    if ((acc & kOneByteMask) != 0) is_one_byte_ = false;
  }

 private:
  bool CheckCons(i::ConsString* cons_string) {
    while (true) {
      // Check left side if flat.
      i::String* left = cons_string->first();
      i::ConsString* left_as_cons =
          i::String::VisitFlat(this, left, 0);
      if (!is_one_byte_) return false;
      // Check right side if flat.
      i::String* right = cons_string->second();
      i::ConsString* right_as_cons =
          i::String::VisitFlat(this, right, 0);
      if (!is_one_byte_) return false;
      // Standard recurse/iterate trick.
      if (left_as_cons != NULL && right_as_cons != NULL) {
        if (left->length() < right->length()) {
          CheckCons(left_as_cons);
          cons_string = right_as_cons;
        } else {
          CheckCons(right_as_cons);
          cons_string = left_as_cons;
        }
        // Check fast return.
        if (!is_one_byte_) return false;
        continue;
      }
      // Descend left in place.
      if (left_as_cons != NULL) {
        cons_string = left_as_cons;
        continue;
      }
      // Descend right in place.
      if (right_as_cons != NULL) {
        cons_string = right_as_cons;
        continue;
      }
      // Terminate.
      break;
    }
    return is_one_byte_;
  }
  bool is_one_byte_;
  DISALLOW_COPY_AND_ASSIGN(ContainsOnlyOneByteHelper);
};


bool String::ContainsOnlyOneByte() const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  if (str->HasOnlyOneByteChars()) return true;
  ContainsOnlyOneByteHelper helper;
  return helper.Check(*str);
}


class Utf8LengthHelper : public i::AllStatic {
 public:
  enum State {
    kEndsWithLeadingSurrogate = 1 << 0,
    kStartsWithTrailingSurrogate = 1 << 1,
    kLeftmostEdgeIsCalculated = 1 << 2,
    kRightmostEdgeIsCalculated = 1 << 3,
    kLeftmostEdgeIsSurrogate = 1 << 4,
    kRightmostEdgeIsSurrogate = 1 << 5
  };

  static const uint8_t kInitialState = 0;

  static inline bool EndsWithSurrogate(uint8_t state) {
    return state & kEndsWithLeadingSurrogate;
  }

  static inline bool StartsWithSurrogate(uint8_t state) {
    return state & kStartsWithTrailingSurrogate;
  }

  class Visitor {
   public:
    Visitor() : utf8_length_(0), state_(kInitialState) {}

    void VisitOneByteString(const uint8_t* chars, int length) {
      int utf8_length = 0;
      // Add in length 1 for each non-Latin1 character.
      for (int i = 0; i < length; i++) {
        utf8_length += *chars++ >> 7;
      }
      // Add in length 1 for each character.
      utf8_length_ = utf8_length + length;
      state_ = kInitialState;
    }

    void VisitTwoByteString(const uint16_t* chars, int length) {
      int utf8_length = 0;
      int last_character = unibrow::Utf16::kNoPreviousCharacter;
      for (int i = 0; i < length; i++) {
        uint16_t c = chars[i];
        utf8_length += unibrow::Utf8::Length(c, last_character);
        last_character = c;
      }
      utf8_length_ = utf8_length;
      uint8_t state = 0;
      if (unibrow::Utf16::IsTrailSurrogate(chars[0])) {
        state |= kStartsWithTrailingSurrogate;
      }
      if (unibrow::Utf16::IsLeadSurrogate(chars[length-1])) {
        state |= kEndsWithLeadingSurrogate;
      }
      state_ = state;
    }

    static i::ConsString* VisitFlat(i::String* string,
                                    int* length,
                                    uint8_t* state) {
      Visitor visitor;
      i::ConsString* cons_string = i::String::VisitFlat(&visitor, string);
      *length = visitor.utf8_length_;
      *state = visitor.state_;
      return cons_string;
    }

   private:
    int utf8_length_;
    uint8_t state_;
    DISALLOW_COPY_AND_ASSIGN(Visitor);
  };

  static inline void MergeLeafLeft(int* length,
                                   uint8_t* state,
                                   uint8_t leaf_state) {
    bool edge_surrogate = StartsWithSurrogate(leaf_state);
    if (!(*state & kLeftmostEdgeIsCalculated)) {
      DCHECK(!(*state & kLeftmostEdgeIsSurrogate));
      *state |= kLeftmostEdgeIsCalculated
          | (edge_surrogate ? kLeftmostEdgeIsSurrogate : 0);
    } else if (EndsWithSurrogate(*state) && edge_surrogate) {
      *length -= unibrow::Utf8::kBytesSavedByCombiningSurrogates;
    }
    if (EndsWithSurrogate(leaf_state)) {
      *state |= kEndsWithLeadingSurrogate;
    } else {
      *state &= ~kEndsWithLeadingSurrogate;
    }
  }

  static inline void MergeLeafRight(int* length,
                                    uint8_t* state,
                                    uint8_t leaf_state) {
    bool edge_surrogate = EndsWithSurrogate(leaf_state);
    if (!(*state & kRightmostEdgeIsCalculated)) {
      DCHECK(!(*state & kRightmostEdgeIsSurrogate));
      *state |= (kRightmostEdgeIsCalculated
                 | (edge_surrogate ? kRightmostEdgeIsSurrogate : 0));
    } else if (edge_surrogate && StartsWithSurrogate(*state)) {
      *length -= unibrow::Utf8::kBytesSavedByCombiningSurrogates;
    }
    if (StartsWithSurrogate(leaf_state)) {
      *state |= kStartsWithTrailingSurrogate;
    } else {
      *state &= ~kStartsWithTrailingSurrogate;
    }
  }

  static inline void MergeTerminal(int* length,
                                   uint8_t state,
                                   uint8_t* state_out) {
    DCHECK((state & kLeftmostEdgeIsCalculated) &&
           (state & kRightmostEdgeIsCalculated));
    if (EndsWithSurrogate(state) && StartsWithSurrogate(state)) {
      *length -= unibrow::Utf8::kBytesSavedByCombiningSurrogates;
    }
    *state_out = kInitialState |
        (state & kLeftmostEdgeIsSurrogate ? kStartsWithTrailingSurrogate : 0) |
        (state & kRightmostEdgeIsSurrogate ? kEndsWithLeadingSurrogate : 0);
  }

  static int Calculate(i::ConsString* current, uint8_t* state_out) {
    using namespace internal;
    int total_length = 0;
    uint8_t state = kInitialState;
    while (true) {
      i::String* left = current->first();
      i::String* right = current->second();
      uint8_t right_leaf_state;
      uint8_t left_leaf_state;
      int leaf_length;
      ConsString* left_as_cons =
          Visitor::VisitFlat(left, &leaf_length, &left_leaf_state);
      if (left_as_cons == NULL) {
        total_length += leaf_length;
        MergeLeafLeft(&total_length, &state, left_leaf_state);
      }
      ConsString* right_as_cons =
          Visitor::VisitFlat(right, &leaf_length, &right_leaf_state);
      if (right_as_cons == NULL) {
        total_length += leaf_length;
        MergeLeafRight(&total_length, &state, right_leaf_state);
        if (left_as_cons != NULL) {
          // 1 Leaf node. Descend in place.
          current = left_as_cons;
          continue;
        } else {
          // Terminal node.
          MergeTerminal(&total_length, state, state_out);
          return total_length;
        }
      } else if (left_as_cons == NULL) {
        // 1 Leaf node. Descend in place.
        current = right_as_cons;
        continue;
      }
      // Both strings are ConsStrings.
      // Recurse on smallest.
      if (left->length() < right->length()) {
        total_length += Calculate(left_as_cons, &left_leaf_state);
        MergeLeafLeft(&total_length, &state, left_leaf_state);
        current = right_as_cons;
      } else {
        total_length += Calculate(right_as_cons, &right_leaf_state);
        MergeLeafRight(&total_length, &state, right_leaf_state);
        current = left_as_cons;
      }
    }
    UNREACHABLE();
    return 0;
  }

  static inline int Calculate(i::ConsString* current) {
    uint8_t state = kInitialState;
    return Calculate(current, &state);
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Utf8LengthHelper);
};


static int Utf8Length(i::String* str, i::Isolate* isolate) {
  int length = str->length();
  if (length == 0) return 0;
  uint8_t state;
  i::ConsString* cons_string =
      Utf8LengthHelper::Visitor::VisitFlat(str, &length, &state);
  if (cons_string == NULL) return length;
  return Utf8LengthHelper::Calculate(cons_string);
}


int String::Utf8Length() const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  i::Isolate* isolate = str->GetIsolate();
  return v8::Utf8Length(*str, isolate);
}


class Utf8WriterVisitor {
 public:
  Utf8WriterVisitor(
      char* buffer,
      int capacity,
      bool skip_capacity_check,
      bool replace_invalid_utf8)
    : early_termination_(false),
      last_character_(unibrow::Utf16::kNoPreviousCharacter),
      buffer_(buffer),
      start_(buffer),
      capacity_(capacity),
      skip_capacity_check_(capacity == -1 || skip_capacity_check),
      replace_invalid_utf8_(replace_invalid_utf8),
      utf16_chars_read_(0) {
  }

  static int WriteEndCharacter(uint16_t character,
                               int last_character,
                               int remaining,
                               char* const buffer,
                               bool replace_invalid_utf8) {
    using namespace unibrow;
    DCHECK(remaining > 0);
    // We can't use a local buffer here because Encode needs to modify
    // previous characters in the stream.  We know, however, that
    // exactly one character will be advanced.
    if (Utf16::IsSurrogatePair(last_character, character)) {
      int written = Utf8::Encode(buffer,
                                 character,
                                 last_character,
                                 replace_invalid_utf8);
      DCHECK(written == 1);
      return written;
    }
    // Use a scratch buffer to check the required characters.
    char temp_buffer[Utf8::kMaxEncodedSize];
    // Can't encode using last_character as gcc has array bounds issues.
    int written = Utf8::Encode(temp_buffer,
                               character,
                               Utf16::kNoPreviousCharacter,
                               replace_invalid_utf8);
    // Won't fit.
    if (written > remaining) return 0;
    // Copy over the character from temp_buffer.
    for (int j = 0; j < written; j++) {
      buffer[j] = temp_buffer[j];
    }
    return written;
  }

  // Visit writes out a group of code units (chars) of a v8::String to the
  // internal buffer_. This is done in two phases. The first phase calculates a
  // pesimistic estimate (writable_length) on how many code units can be safely
  // written without exceeding the buffer capacity and without writing the last
  // code unit (it could be a lead surrogate). The estimated number of code
  // units is then written out in one go, and the reported byte usage is used
  // to correct the estimate. This is repeated until the estimate becomes <= 0
  // or all code units have been written out. The second phase writes out code
  // units until the buffer capacity is reached, would be exceeded by the next
  // unit, or all units have been written out.
  template<typename Char>
  void Visit(const Char* chars, const int length) {
    using namespace unibrow;
    DCHECK(!early_termination_);
    if (length == 0) return;
    // Copy state to stack.
    char* buffer = buffer_;
    int last_character =
        sizeof(Char) == 1 ? Utf16::kNoPreviousCharacter : last_character_;
    int i = 0;
    // Do a fast loop where there is no exit capacity check.
    while (true) {
      int fast_length;
      if (skip_capacity_check_) {
        fast_length = length;
      } else {
        int remaining_capacity = capacity_ - static_cast<int>(buffer - start_);
        // Need enough space to write everything but one character.
        STATIC_ASSERT(Utf16::kMaxExtraUtf8BytesForOneUtf16CodeUnit == 3);
        int max_size_per_char =  sizeof(Char) == 1 ? 2 : 3;
        int writable_length =
            (remaining_capacity - max_size_per_char)/max_size_per_char;
        // Need to drop into slow loop.
        if (writable_length <= 0) break;
        fast_length = i + writable_length;
        if (fast_length > length) fast_length = length;
      }
      // Write the characters to the stream.
      if (sizeof(Char) == 1) {
        for (; i < fast_length; i++) {
          buffer +=
              Utf8::EncodeOneByte(buffer, static_cast<uint8_t>(*chars++));
          DCHECK(capacity_ == -1 || (buffer - start_) <= capacity_);
        }
      } else {
        for (; i < fast_length; i++) {
          uint16_t character = *chars++;
          buffer += Utf8::Encode(buffer,
                                 character,
                                 last_character,
                                 replace_invalid_utf8_);
          last_character = character;
          DCHECK(capacity_ == -1 || (buffer - start_) <= capacity_);
        }
      }
      // Array is fully written. Exit.
      if (fast_length == length) {
        // Write state back out to object.
        last_character_ = last_character;
        buffer_ = buffer;
        utf16_chars_read_ += length;
        return;
      }
    }
    DCHECK(!skip_capacity_check_);
    // Slow loop. Must check capacity on each iteration.
    int remaining_capacity = capacity_ - static_cast<int>(buffer - start_);
    DCHECK(remaining_capacity >= 0);
    for (; i < length && remaining_capacity > 0; i++) {
      uint16_t character = *chars++;
      // remaining_capacity is <= 3 bytes at this point, so we do not write out
      // an umatched lead surrogate.
      if (replace_invalid_utf8_ && Utf16::IsLeadSurrogate(character)) {
        early_termination_ = true;
        break;
      }
      int written = WriteEndCharacter(character,
                                      last_character,
                                      remaining_capacity,
                                      buffer,
                                      replace_invalid_utf8_);
      if (written == 0) {
        early_termination_ = true;
        break;
      }
      buffer += written;
      remaining_capacity -= written;
      last_character = character;
    }
    // Write state back out to object.
    last_character_ = last_character;
    buffer_ = buffer;
    utf16_chars_read_ += i;
  }

  inline bool IsDone() {
    return early_termination_;
  }

  inline void VisitOneByteString(const uint8_t* chars, int length) {
    Visit(chars, length);
  }

  inline void VisitTwoByteString(const uint16_t* chars, int length) {
    Visit(chars, length);
  }

  int CompleteWrite(bool write_null, int* utf16_chars_read_out) {
    // Write out number of utf16 characters written to the stream.
    if (utf16_chars_read_out != NULL) {
      *utf16_chars_read_out = utf16_chars_read_;
    }
    // Only null terminate if all of the string was written and there's space.
    if (write_null &&
        !early_termination_ &&
        (capacity_ == -1 || (buffer_ - start_) < capacity_)) {
      *buffer_++ = '\0';
    }
    return static_cast<int>(buffer_ - start_);
  }

 private:
  bool early_termination_;
  int last_character_;
  char* buffer_;
  char* const start_;
  int capacity_;
  bool const skip_capacity_check_;
  bool const replace_invalid_utf8_;
  int utf16_chars_read_;
  DISALLOW_IMPLICIT_CONSTRUCTORS(Utf8WriterVisitor);
};


static bool RecursivelySerializeToUtf8(i::String* current,
                                       Utf8WriterVisitor* writer,
                                       int recursion_budget) {
  while (!writer->IsDone()) {
    i::ConsString* cons_string = i::String::VisitFlat(writer, current);
    if (cons_string == NULL) return true;  // Leaf node.
    if (recursion_budget <= 0) return false;
    // Must write the left branch first.
    i::String* first = cons_string->first();
    bool success = RecursivelySerializeToUtf8(first,
                                              writer,
                                              recursion_budget - 1);
    if (!success) return false;
    // Inline tail recurse for right branch.
    current = cons_string->second();
  }
  return true;
}


int String::WriteUtf8(char* buffer,
                      int capacity,
                      int* nchars_ref,
                      int options) const {
  i::Isolate* isolate = Utils::OpenHandle(this)->GetIsolate();
  LOG_API(isolate, "String::WriteUtf8");
  ENTER_V8(isolate);
  i::Handle<i::String> str = Utils::OpenHandle(this);
  if (options & HINT_MANY_WRITES_EXPECTED) {
    str = i::String::Flatten(str);  // Flatten the string for efficiency.
  }
  const int string_length = str->length();
  bool write_null = !(options & NO_NULL_TERMINATION);
  bool replace_invalid_utf8 = (options & REPLACE_INVALID_UTF8);
  int max16BitCodeUnitSize = unibrow::Utf8::kMax16BitCodeUnitSize;
  // First check if we can just write the string without checking capacity.
  if (capacity == -1 || capacity / max16BitCodeUnitSize >= string_length) {
    Utf8WriterVisitor writer(buffer, capacity, true, replace_invalid_utf8);
    const int kMaxRecursion = 100;
    bool success = RecursivelySerializeToUtf8(*str, &writer, kMaxRecursion);
    if (success) return writer.CompleteWrite(write_null, nchars_ref);
  } else if (capacity >= string_length) {
    // First check that the buffer is large enough.
    int utf8_bytes = v8::Utf8Length(*str, str->GetIsolate());
    if (utf8_bytes <= capacity) {
      // one-byte fast path.
      if (utf8_bytes == string_length) {
        WriteOneByte(reinterpret_cast<uint8_t*>(buffer), 0, capacity, options);
        if (nchars_ref != NULL) *nchars_ref = string_length;
        if (write_null && (utf8_bytes+1 <= capacity)) {
          return string_length + 1;
        }
        return string_length;
      }
      if (write_null && (utf8_bytes+1 > capacity)) {
        options |= NO_NULL_TERMINATION;
      }
      // Recurse once without a capacity limit.
      // This will get into the first branch above.
      // TODO(dcarney) Check max left rec. in Utf8Length and fall through.
      return WriteUtf8(buffer, -1, nchars_ref, options);
    }
  }
  // Recursive slow path can potentially be unreasonable slow. Flatten.
  str = i::String::Flatten(str);
  Utf8WriterVisitor writer(buffer, capacity, false, replace_invalid_utf8);
  i::String::VisitFlat(&writer, *str);
  return writer.CompleteWrite(write_null, nchars_ref);
}


template<typename CharType>
static inline int WriteHelper(const String* string,
                              CharType* buffer,
                              int start,
                              int length,
                              int options) {
  i::Isolate* isolate = Utils::OpenHandle(string)->GetIsolate();
  LOG_API(isolate, "String::Write");
  ENTER_V8(isolate);
  DCHECK(start >= 0 && length >= -1);
  i::Handle<i::String> str = Utils::OpenHandle(string);
  isolate->string_tracker()->RecordWrite(str);
  if (options & String::HINT_MANY_WRITES_EXPECTED) {
    // Flatten the string for efficiency.  This applies whether we are
    // using StringCharacterStream or Get(i) to access the characters.
    str = i::String::Flatten(str);
  }
  int end = start + length;
  if ((length == -1) || (length > str->length() - start) )
    end = str->length();
  if (end < 0) return 0;
  i::String::WriteToFlat(*str, buffer, start, end);
  if (!(options & String::NO_NULL_TERMINATION) &&
      (length == -1 || end - start < length)) {
    buffer[end - start] = '\0';
  }
  return end - start;
}


int String::WriteOneByte(uint8_t* buffer,
                         int start,
                         int length,
                         int options) const {
  return WriteHelper(this, buffer, start, length, options);
}


int String::Write(uint16_t* buffer,
                  int start,
                  int length,
                  int options) const {
  return WriteHelper(this, buffer, start, length, options);
}


bool v8::String::IsExternal() const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  return i::StringShape(*str).IsExternalTwoByte();
}


bool v8::String::IsExternalOneByte() const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  return i::StringShape(*str).IsExternalOneByte();
}


void v8::String::VerifyExternalStringResource(
    v8::String::ExternalStringResource* value) const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  const v8::String::ExternalStringResource* expected;
  if (i::StringShape(*str).IsExternalTwoByte()) {
    const void* resource =
        i::Handle<i::ExternalTwoByteString>::cast(str)->resource();
    expected = reinterpret_cast<const ExternalStringResource*>(resource);
  } else {
    expected = NULL;
  }
  CHECK_EQ(expected, value);
}

void v8::String::VerifyExternalStringResourceBase(
    v8::String::ExternalStringResourceBase* value, Encoding encoding) const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  const v8::String::ExternalStringResourceBase* expected;
  Encoding expectedEncoding;
  if (i::StringShape(*str).IsExternalOneByte()) {
    const void* resource =
        i::Handle<i::ExternalOneByteString>::cast(str)->resource();
    expected = reinterpret_cast<const ExternalStringResourceBase*>(resource);
    expectedEncoding = ONE_BYTE_ENCODING;
  } else if (i::StringShape(*str).IsExternalTwoByte()) {
    const void* resource =
        i::Handle<i::ExternalTwoByteString>::cast(str)->resource();
    expected = reinterpret_cast<const ExternalStringResourceBase*>(resource);
    expectedEncoding = TWO_BYTE_ENCODING;
  } else {
    expected = NULL;
    expectedEncoding =
        str->IsOneByteRepresentation() ? ONE_BYTE_ENCODING : TWO_BYTE_ENCODING;
  }
  CHECK_EQ(expected, value);
  CHECK_EQ(expectedEncoding, encoding);
}

const v8::String::ExternalOneByteStringResource*
v8::String::GetExternalOneByteStringResource() const {
  i::Handle<i::String> str = Utils::OpenHandle(this);
  if (i::StringShape(*str).IsExternalOneByte()) {
    const void* resource =
        i::Handle<i::ExternalOneByteString>::cast(str)->resource();
    return reinterpret_cast<const ExternalOneByteStringResource*>(resource);
  } else {
    return NULL;
  }
}


Local<Value> Symbol::Name() const {
  i::Handle<i::Symbol> sym = Utils::OpenHandle(this);
  i::Handle<i::Object> name(sym->name(), sym->GetIsolate());
  return Utils::ToLocal(name);
}


Local<Value> Private::Name() const {
  return reinterpret_cast<const Symbol*>(this)->Name();
}


double Number::Value() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  return obj->Number();
}


bool Boolean::Value() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  return obj->IsTrue();
}


int64_t Integer::Value() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    return i::Smi::cast(*obj)->value();
  } else {
    return static_cast<int64_t>(obj->Number());
  }
}


int32_t Int32::Value() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    return i::Smi::cast(*obj)->value();
  } else {
    return static_cast<int32_t>(obj->Number());
  }
}


uint32_t Uint32::Value() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  if (obj->IsSmi()) {
    return i::Smi::cast(*obj)->value();
  } else {
    return static_cast<uint32_t>(obj->Number());
  }
}


int v8::Object::InternalFieldCount() {
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  return obj->GetInternalFieldCount();
}


static bool InternalFieldOK(i::Handle<i::JSObject> obj,
                            int index,
                            const char* location) {
  return Utils::ApiCheck(index < obj->GetInternalFieldCount(),
                         location,
                         "Internal field out of bounds");
}


Local<Value> v8::Object::SlowGetInternalField(int index) {
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  const char* location = "v8::Object::GetInternalField()";
  if (!InternalFieldOK(obj, index, location)) return Local<Value>();
  i::Handle<i::Object> value(obj->GetInternalField(index), obj->GetIsolate());
  return Utils::ToLocal(value);
}


void v8::Object::SetInternalField(int index, v8::Handle<Value> value) {
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  const char* location = "v8::Object::SetInternalField()";
  if (!InternalFieldOK(obj, index, location)) return;
  i::Handle<i::Object> val = Utils::OpenHandle(*value);
  obj->SetInternalField(index, *val);
  DCHECK(value->Equals(GetInternalField(index)));
}


void* v8::Object::SlowGetAlignedPointerFromInternalField(int index) {
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  const char* location = "v8::Object::GetAlignedPointerFromInternalField()";
  if (!InternalFieldOK(obj, index, location)) return NULL;
  return DecodeSmiToAligned(obj->GetInternalField(index), location);
}


void v8::Object::SetAlignedPointerInInternalField(int index, void* value) {
  i::Handle<i::JSObject> obj = Utils::OpenHandle(this);
  const char* location = "v8::Object::SetAlignedPointerInInternalField()";
  if (!InternalFieldOK(obj, index, location)) return;
  obj->SetInternalField(index, EncodeAlignedAsSmi(value, location));
  DCHECK_EQ(value, GetAlignedPointerFromInternalField(index));
}


static void* ExternalValue(i::Object* obj) {
  // Obscure semantics for undefined, but somehow checked in our unit tests...
  if (obj->IsUndefined()) return NULL;
  i::Object* foreign = i::JSObject::cast(obj)->GetInternalField(0);
  return i::Foreign::cast(foreign)->foreign_address();
}


// --- E n v i r o n m e n t ---


void v8::V8::InitializePlatform(Platform* platform) {
  i::V8::InitializePlatform(platform);
}


void v8::V8::ShutdownPlatform() {
  i::V8::ShutdownPlatform();
}


bool v8::V8::Initialize() {
  i::V8::Initialize();
#ifdef V8_USE_EXTERNAL_STARTUP_DATA
  i::ReadNatives();
#endif
  return true;
}


void v8::V8::SetEntropySource(EntropySource entropy_source) {
  base::RandomNumberGenerator::SetEntropySource(entropy_source);
}


void v8::V8::SetReturnAddressLocationResolver(
    ReturnAddressLocationResolver return_address_resolver) {
  i::V8::SetReturnAddressLocationResolver(return_address_resolver);
}

void v8::V8::SetArrayBufferAllocator(
    ArrayBuffer::Allocator* allocator) {
  if (!Utils::ApiCheck(i::V8::ArrayBufferAllocator() == NULL,
                       "v8::V8::SetArrayBufferAllocator",
                       "ArrayBufferAllocator might only be set once"))
    return;
  i::V8::SetArrayBufferAllocator(allocator);
}


bool v8::V8::Dispose() {
  i::V8::TearDown();
#ifdef V8_USE_EXTERNAL_STARTUP_DATA
  i::DisposeNatives();
#endif
  return true;
}


HeapStatistics::HeapStatistics(): total_heap_size_(0),
                                  total_heap_size_executable_(0),
                                  total_physical_size_(0),
                                  used_heap_size_(0),
                                  heap_size_limit_(0) { }


bool v8::V8::InitializeICU(const char* icu_data_file) {
  return i::InitializeICU(icu_data_file);
}


const char* v8::V8::GetVersion() {
  return i::Version::GetVersion();
}


static i::Handle<i::Context> CreateEnvironment(
    i::Isolate* isolate,
    v8::ExtensionConfiguration* extensions,
    v8::Handle<ObjectTemplate> global_template,
    v8::Handle<Value> maybe_global_proxy) {
  i::Handle<i::Context> env;

  // Enter V8 via an ENTER_V8 scope.
  {
    ENTER_V8(isolate);
    v8::Handle<ObjectTemplate> proxy_template = global_template;
    i::Handle<i::FunctionTemplateInfo> proxy_constructor;
    i::Handle<i::FunctionTemplateInfo> global_constructor;

    if (!global_template.IsEmpty()) {
      // Make sure that the global_template has a constructor.
      global_constructor = EnsureConstructor(isolate, *global_template);

      // Create a fresh template for the global proxy object.
      proxy_template = ObjectTemplate::New(
          reinterpret_cast<v8::Isolate*>(isolate));
      proxy_constructor = EnsureConstructor(isolate, *proxy_template);

      // Set the global template to be the prototype template of
      // global proxy template.
      proxy_constructor->set_prototype_template(
          *Utils::OpenHandle(*global_template));

      // Migrate security handlers from global_template to
      // proxy_template.  Temporarily removing access check
      // information from the global template.
      if (!global_constructor->access_check_info()->IsUndefined()) {
        proxy_constructor->set_access_check_info(
            global_constructor->access_check_info());
        proxy_constructor->set_needs_access_check(
            global_constructor->needs_access_check());
        global_constructor->set_needs_access_check(false);
        global_constructor->set_access_check_info(
            isolate->heap()->undefined_value());
      }
    }

    i::Handle<i::Object> proxy = Utils::OpenHandle(*maybe_global_proxy, true);
    i::MaybeHandle<i::JSGlobalProxy> maybe_proxy;
    if (!proxy.is_null()) {
      maybe_proxy = i::Handle<i::JSGlobalProxy>::cast(proxy);
    }
    // Create the environment.
    env = isolate->bootstrapper()->CreateEnvironment(
        maybe_proxy, proxy_template, extensions);

    // Restore the access check info on the global template.
    if (!global_template.IsEmpty()) {
      DCHECK(!global_constructor.is_null());
      DCHECK(!proxy_constructor.is_null());
      global_constructor->set_access_check_info(
          proxy_constructor->access_check_info());
      global_constructor->set_needs_access_check(
          proxy_constructor->needs_access_check());
    }
  }
  // Leave V8.

  return env;
}

Local<Context> v8::Context::New(
    v8::Isolate* external_isolate,
    v8::ExtensionConfiguration* extensions,
    v8::Handle<ObjectTemplate> global_template,
    v8::Handle<Value> global_object) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(external_isolate);
  LOG_API(isolate, "Context::New");
  i::HandleScope scope(isolate);
  ExtensionConfiguration no_extensions;
  if (extensions == NULL) extensions = &no_extensions;
  i::Handle<i::Context> env =
      CreateEnvironment(isolate, extensions, global_template, global_object);
  if (env.is_null()) return Local<Context>();
  return Utils::ToLocal(scope.CloseAndEscape(env));
}


void v8::Context::SetSecurityToken(Handle<Value> token) {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  i::Handle<i::Object> token_handle = Utils::OpenHandle(*token);
  env->set_security_token(*token_handle);
}


void v8::Context::UseDefaultSecurityToken() {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  env->set_security_token(env->global_object());
}


Handle<Value> v8::Context::GetSecurityToken() {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  i::Isolate* isolate = env->GetIsolate();
  i::Object* security_token = env->security_token();
  i::Handle<i::Object> token_handle(security_token, isolate);
  return Utils::ToLocal(token_handle);
}


v8::Isolate* Context::GetIsolate() {
  i::Handle<i::Context> env = Utils::OpenHandle(this);
  return reinterpret_cast<Isolate*>(env->GetIsolate());
}


v8::Local<v8::Object> Context::Global() {
  i::Handle<i::Context> context = Utils::OpenHandle(this);
  i::Isolate* isolate = context->GetIsolate();
  i::Handle<i::Object> global(context->global_proxy(), isolate);
  // TODO(dcarney): This should always return the global proxy
  // but can't presently as calls to GetProtoype will return the wrong result.
  if (i::Handle<i::JSGlobalProxy>::cast(
          global)->IsDetachedFrom(context->global_object())) {
    global = i::Handle<i::Object>(context->global_object(), isolate);
  }
  return Utils::ToLocal(i::Handle<i::JSObject>::cast(global));
}


void Context::DetachGlobal() {
  i::Handle<i::Context> context = Utils::OpenHandle(this);
  i::Isolate* isolate = context->GetIsolate();
  ENTER_V8(isolate);
  isolate->bootstrapper()->DetachGlobal(context);
}


void Context::AllowCodeGenerationFromStrings(bool allow) {
  i::Handle<i::Context> context = Utils::OpenHandle(this);
  i::Isolate* isolate = context->GetIsolate();
  ENTER_V8(isolate);
  context->set_allow_code_gen_from_strings(
      allow ? isolate->heap()->true_value() : isolate->heap()->false_value());
}


bool Context::IsCodeGenerationFromStringsAllowed() {
  i::Handle<i::Context> context = Utils::OpenHandle(this);
  return !context->allow_code_gen_from_strings()->IsFalse();
}


void Context::SetErrorMessageForCodeGenerationFromStrings(
    Handle<String> error) {
  i::Handle<i::Context> context = Utils::OpenHandle(this);
  i::Handle<i::String> error_handle = Utils::OpenHandle(*error);
  context->set_error_message_for_code_gen_from_strings(*error_handle);
}


MaybeLocal<v8::Object> ObjectTemplate::NewInstance(Local<Context> context) {
  PREPARE_FOR_EXECUTION(context, "v8::ObjectTemplate::NewInstance()", Object);
  auto self = Utils::OpenHandle(this);
  Local<Object> result;
  has_pending_exception =
      !ToLocal<Object>(i::ApiNatives::InstantiateObject(self), &result);
  RETURN_ON_FAILED_EXECUTION(Object);
  RETURN_ESCAPED(result);
}


Local<v8::Object> ObjectTemplate::NewInstance() {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(NewInstance(context), Object);
}


MaybeLocal<v8::Function> FunctionTemplate::GetFunction(Local<Context> context) {
  PREPARE_FOR_EXECUTION(context, "v8::FunctionTemplate::GetFunction()",
                        Function);
  auto self = Utils::OpenHandle(this);
  Local<Function> result;
  has_pending_exception =
      !ToLocal<Function>(i::ApiNatives::InstantiateFunction(self), &result);
  RETURN_ON_FAILED_EXECUTION(Function);
  RETURN_ESCAPED(result);
}


Local<v8::Function> FunctionTemplate::GetFunction() {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(GetFunction(context), Function);
}


bool FunctionTemplate::HasInstance(v8::Handle<v8::Value> value) {
  auto self = Utils::OpenHandle(this);
  auto obj = Utils::OpenHandle(*value);
  return self->IsTemplateFor(*obj);
}


Local<External> v8::External::New(Isolate* isolate, void* value) {
  STATIC_ASSERT(sizeof(value) == sizeof(i::Address));
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "External::New");
  ENTER_V8(i_isolate);
  i::Handle<i::JSObject> external = i_isolate->factory()->NewExternal(value);
  return Utils::ExternalToLocal(external);
}


void* External::Value() const {
  return ExternalValue(*Utils::OpenHandle(this));
}


// anonymous namespace for string creation helper functions
namespace {

inline int StringLength(const char* string) {
  return i::StrLength(string);
}


inline int StringLength(const uint8_t* string) {
  return i::StrLength(reinterpret_cast<const char*>(string));
}


inline int StringLength(const uint16_t* string) {
  int length = 0;
  while (string[length] != '\0')
    length++;
  return length;
}


MUST_USE_RESULT
inline i::MaybeHandle<i::String> NewString(i::Factory* factory,
                                           String::NewStringType type,
                                           i::Vector<const char> string) {
  if (type == String::kInternalizedString) {
    return factory->InternalizeUtf8String(string);
  }
  return factory->NewStringFromUtf8(string);
}


MUST_USE_RESULT
inline i::MaybeHandle<i::String> NewString(i::Factory* factory,
                                           String::NewStringType type,
                                           i::Vector<const uint8_t> string) {
  if (type == String::kInternalizedString) {
    return factory->InternalizeOneByteString(string);
  }
  return factory->NewStringFromOneByte(string);
}


MUST_USE_RESULT
inline i::MaybeHandle<i::String> NewString(i::Factory* factory,
                                           String::NewStringType type,
                                           i::Vector<const uint16_t> string) {
  if (type == String::kInternalizedString) {
    return factory->InternalizeTwoByteString(string);
  }
  return factory->NewStringFromTwoByte(string);
}


template<typename Char>
inline Local<String> NewString(Isolate* v8_isolate,
                               const char* location,
                               const char* env,
                               const Char* data,
                               String::NewStringType type,
                               int length) {
  i::Isolate* isolate = reinterpret_cast<internal::Isolate*>(v8_isolate);
  LOG_API(isolate, env);
  if (length == 0) {
    return String::Empty(v8_isolate);
  }
  ENTER_V8(isolate);
  if (length == -1) length = StringLength(data);
  EXCEPTION_PREAMBLE(isolate);
  i::Handle<i::String> result;
  has_pending_exception =
      !NewString(isolate->factory(), type, i::Vector<const Char>(data, length))
           .ToHandle(&result);
  EXCEPTION_BAILOUT_CHECK(isolate, Local<String>());
  return Utils::ToLocal(result);
}

}  // anonymous namespace


Local<String> String::NewFromUtf8(Isolate* isolate,
                                  const char* data,
                                  NewStringType type,
                                  int length) {
  return NewString(isolate,
                   "v8::String::NewFromUtf8()",
                   "String::NewFromUtf8",
                   data,
                   type,
                   length);
}


Local<String> String::NewFromOneByte(Isolate* isolate,
                                     const uint8_t* data,
                                     NewStringType type,
                                     int length) {
  return NewString(isolate,
                   "v8::String::NewFromOneByte()",
                   "String::NewFromOneByte",
                   data,
                   type,
                   length);
}


Local<String> String::NewFromTwoByte(Isolate* isolate,
                                     const uint16_t* data,
                                     NewStringType type,
                                     int length) {
  return NewString(isolate,
                   "v8::String::NewFromTwoByte()",
                   "String::NewFromTwoByte",
                   data,
                   type,
                   length);
}


Local<String> v8::String::Concat(Handle<String> left, Handle<String> right) {
  i::Handle<i::String> left_string = Utils::OpenHandle(*left);
  i::Isolate* isolate = left_string->GetIsolate();
  LOG_API(isolate, "String::New(char)");
  ENTER_V8(isolate);
  i::Handle<i::String> right_string = Utils::OpenHandle(*right);
  // If we are steering towards a range error, do not wait for the error to be
  // thrown, and return the null handle instead.
  if (left_string->length() + right_string->length() > i::String::kMaxLength) {
    return Local<String>();
  }
  i::Handle<i::String> result = isolate->factory()->NewConsString(
      left_string, right_string).ToHandleChecked();
  return Utils::ToLocal(result);
}


static i::MaybeHandle<i::String> NewExternalStringHandle(
    i::Isolate* isolate, v8::String::ExternalStringResource* resource) {
  return isolate->factory()->NewExternalStringFromTwoByte(resource);
}


static i::MaybeHandle<i::String> NewExternalOneByteStringHandle(
    i::Isolate* isolate, v8::String::ExternalOneByteStringResource* resource) {
  return isolate->factory()->NewExternalStringFromOneByte(resource);
}


Local<String> v8::String::NewExternal(
    Isolate* isolate,
    v8::String::ExternalStringResource* resource) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "String::NewExternal");
  ENTER_V8(i_isolate);
  CHECK(resource && resource->data());
  EXCEPTION_PREAMBLE(i_isolate);
  i::Handle<i::String> string;
  has_pending_exception =
      !NewExternalStringHandle(i_isolate, resource).ToHandle(&string);
  EXCEPTION_BAILOUT_CHECK(i_isolate, Local<String>());
  i_isolate->heap()->external_string_table()->AddString(*string);
  return Utils::ToLocal(string);
}


bool v8::String::MakeExternal(v8::String::ExternalStringResource* resource) {
  i::Handle<i::String> obj = Utils::OpenHandle(this);
  i::Isolate* isolate = obj->GetIsolate();
  if (i::StringShape(*obj).IsExternal()) {
    return false;  // Already an external string.
  }
  ENTER_V8(isolate);
  if (isolate->string_tracker()->IsFreshUnusedString(obj)) {
    return false;
  }
  if (isolate->heap()->IsInGCPostProcessing()) {
    return false;
  }
  CHECK(resource && resource->data());

  bool result = obj->MakeExternal(resource);
  // Assert that if CanMakeExternal(), then externalizing actually succeeds.
  DCHECK(!CanMakeExternal() || result);
  if (result) {
    DCHECK(obj->IsExternalString());
    isolate->heap()->external_string_table()->AddString(*obj);
  }
  return result;
}


Local<String> v8::String::NewExternal(
    Isolate* isolate, v8::String::ExternalOneByteStringResource* resource) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "String::NewExternal");
  ENTER_V8(i_isolate);
  CHECK(resource && resource->data());
  EXCEPTION_PREAMBLE(i_isolate);
  i::Handle<i::String> string;
  has_pending_exception =
      !NewExternalOneByteStringHandle(i_isolate, resource).ToHandle(&string);
  EXCEPTION_BAILOUT_CHECK(i_isolate, Local<String>());
  i_isolate->heap()->external_string_table()->AddString(*string);
  return Utils::ToLocal(string);
}


bool v8::String::MakeExternal(
    v8::String::ExternalOneByteStringResource* resource) {
  i::Handle<i::String> obj = Utils::OpenHandle(this);
  i::Isolate* isolate = obj->GetIsolate();
  if (i::StringShape(*obj).IsExternal()) {
    return false;  // Already an external string.
  }
  ENTER_V8(isolate);
  if (isolate->string_tracker()->IsFreshUnusedString(obj)) {
    return false;
  }
  if (isolate->heap()->IsInGCPostProcessing()) {
    return false;
  }
  CHECK(resource && resource->data());

  bool result = obj->MakeExternal(resource);
  // Assert that if CanMakeExternal(), then externalizing actually succeeds.
  DCHECK(!CanMakeExternal() || result);
  if (result) {
    DCHECK(obj->IsExternalString());
    isolate->heap()->external_string_table()->AddString(*obj);
  }
  return result;
}


bool v8::String::CanMakeExternal() {
  i::Handle<i::String> obj = Utils::OpenHandle(this);
  i::Isolate* isolate = obj->GetIsolate();

  if (isolate->string_tracker()->IsFreshUnusedString(obj)) return false;
  int size = obj->Size();  // Byte size of the original string.
  if (size < i::ExternalString::kShortSize) return false;
  i::StringShape shape(*obj);
  return !shape.IsExternal();
}


Isolate* v8::Object::GetIsolate() {
  i::Isolate* i_isolate = Utils::OpenHandle(this)->GetIsolate();
  return reinterpret_cast<Isolate*>(i_isolate);
}


Local<v8::Object> v8::Object::New(Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "Object::New");
  ENTER_V8(i_isolate);
  i::Handle<i::JSObject> obj =
      i_isolate->factory()->NewJSObject(i_isolate->object_function());
  return Utils::ToLocal(obj);
}


Local<v8::Value> v8::NumberObject::New(Isolate* isolate, double value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "NumberObject::New");
  ENTER_V8(i_isolate);
  i::Handle<i::Object> number = i_isolate->factory()->NewNumber(value);
  i::Handle<i::Object> obj =
      i::Object::ToObject(i_isolate, number).ToHandleChecked();
  return Utils::ToLocal(obj);
}


double v8::NumberObject::ValueOf() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::JSValue> jsvalue = i::Handle<i::JSValue>::cast(obj);
  i::Isolate* isolate = jsvalue->GetIsolate();
  LOG_API(isolate, "NumberObject::NumberValue");
  return jsvalue->value()->Number();
}


Local<v8::Value> v8::BooleanObject::New(bool value) {
  i::Isolate* isolate = i::Isolate::Current();
  LOG_API(isolate, "BooleanObject::New");
  ENTER_V8(isolate);
  i::Handle<i::Object> boolean(value
                               ? isolate->heap()->true_value()
                               : isolate->heap()->false_value(),
                               isolate);
  i::Handle<i::Object> obj =
      i::Object::ToObject(isolate, boolean).ToHandleChecked();
  return Utils::ToLocal(obj);
}


bool v8::BooleanObject::ValueOf() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::JSValue> jsvalue = i::Handle<i::JSValue>::cast(obj);
  i::Isolate* isolate = jsvalue->GetIsolate();
  LOG_API(isolate, "BooleanObject::BooleanValue");
  return jsvalue->value()->IsTrue();
}


Local<v8::Value> v8::StringObject::New(Handle<String> value) {
  i::Handle<i::String> string = Utils::OpenHandle(*value);
  i::Isolate* isolate = string->GetIsolate();
  LOG_API(isolate, "StringObject::New");
  ENTER_V8(isolate);
  i::Handle<i::Object> obj =
      i::Object::ToObject(isolate, string).ToHandleChecked();
  return Utils::ToLocal(obj);
}


Local<v8::String> v8::StringObject::ValueOf() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::JSValue> jsvalue = i::Handle<i::JSValue>::cast(obj);
  i::Isolate* isolate = jsvalue->GetIsolate();
  LOG_API(isolate, "StringObject::StringValue");
  return Utils::ToLocal(
      i::Handle<i::String>(i::String::cast(jsvalue->value())));
}


Local<v8::Value> v8::SymbolObject::New(Isolate* isolate, Handle<Symbol> value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "SymbolObject::New");
  ENTER_V8(i_isolate);
  i::Handle<i::Object> obj = i::Object::ToObject(
      i_isolate, Utils::OpenHandle(*value)).ToHandleChecked();
  return Utils::ToLocal(obj);
}


Local<v8::Symbol> v8::SymbolObject::ValueOf() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::JSValue> jsvalue = i::Handle<i::JSValue>::cast(obj);
  i::Isolate* isolate = jsvalue->GetIsolate();
  LOG_API(isolate, "SymbolObject::SymbolValue");
  return Utils::ToLocal(
      i::Handle<i::Symbol>(i::Symbol::cast(jsvalue->value())));
}


Local<v8::Value> v8::Date::New(Isolate* isolate, double time) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "Date::New");
  if (std::isnan(time)) {
    // Introduce only canonical NaN value into the VM, to avoid signaling NaNs.
    time = std::numeric_limits<double>::quiet_NaN();
  }
  ENTER_V8(i_isolate);
  EXCEPTION_PREAMBLE(i_isolate);
  i::Handle<i::Object> obj;
  has_pending_exception = !i::Execution::NewDate(
      i_isolate, time).ToHandle(&obj);
  EXCEPTION_BAILOUT_CHECK(i_isolate, Local<v8::Value>());
  return Utils::ToLocal(obj);
}


double v8::Date::ValueOf() const {
  i::Handle<i::Object> obj = Utils::OpenHandle(this);
  i::Handle<i::JSDate> jsdate = i::Handle<i::JSDate>::cast(obj);
  i::Isolate* isolate = jsdate->GetIsolate();
  LOG_API(isolate, "Date::NumberValue");
  return jsdate->value()->Number();
}


void v8::Date::DateTimeConfigurationChangeNotification(Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "Date::DateTimeConfigurationChangeNotification");
  ENTER_V8(i_isolate);
  i_isolate->date_cache()->ResetDateCache();
  if (!i_isolate->eternal_handles()->Exists(
          i::EternalHandles::DATE_CACHE_VERSION)) {
    return;
  }
  i::Handle<i::FixedArray> date_cache_version =
      i::Handle<i::FixedArray>::cast(i_isolate->eternal_handles()->GetSingleton(
          i::EternalHandles::DATE_CACHE_VERSION));
  DCHECK_EQ(1, date_cache_version->length());
  CHECK(date_cache_version->get(0)->IsSmi());
  date_cache_version->set(
      0,
      i::Smi::FromInt(i::Smi::cast(date_cache_version->get(0))->value() + 1));
}


static i::Handle<i::String> RegExpFlagsToString(RegExp::Flags flags) {
  i::Isolate* isolate = i::Isolate::Current();
  uint8_t flags_buf[3];
  int num_flags = 0;
  if ((flags & RegExp::kGlobal) != 0) flags_buf[num_flags++] = 'g';
  if ((flags & RegExp::kMultiline) != 0) flags_buf[num_flags++] = 'm';
  if ((flags & RegExp::kIgnoreCase) != 0) flags_buf[num_flags++] = 'i';
  DCHECK(num_flags <= static_cast<int>(arraysize(flags_buf)));
  return isolate->factory()->InternalizeOneByteString(
      i::Vector<const uint8_t>(flags_buf, num_flags));
}


MaybeLocal<v8::RegExp> v8::RegExp::New(Local<Context> context,
                                       Handle<String> pattern, Flags flags) {
  PREPARE_FOR_EXECUTION(context, "RegExp::New", RegExp);
  Local<v8::RegExp> result;
  has_pending_exception =
      !ToLocal<RegExp>(i::Execution::NewJSRegExp(Utils::OpenHandle(*pattern),
                                                 RegExpFlagsToString(flags)),
                       &result);
  RETURN_ON_FAILED_EXECUTION(RegExp);
  RETURN_ESCAPED(result);
}


Local<v8::RegExp> v8::RegExp::New(Handle<String> pattern, Flags flags) {
  auto isolate =
      reinterpret_cast<Isolate*>(Utils::OpenHandle(*pattern)->GetIsolate());
  auto context = isolate->GetCurrentContext();
  RETURN_TO_LOCAL_UNCHECKED(New(context, pattern, flags), RegExp);
}


Local<v8::String> v8::RegExp::GetSource() const {
  i::Handle<i::JSRegExp> obj = Utils::OpenHandle(this);
  return Utils::ToLocal(i::Handle<i::String>(obj->Pattern()));
}


// Assert that the static flags cast in GetFlags is valid.
#define REGEXP_FLAG_ASSERT_EQ(api_flag, internal_flag)          \
  STATIC_ASSERT(static_cast<int>(v8::RegExp::api_flag) ==       \
                static_cast<int>(i::JSRegExp::internal_flag))
REGEXP_FLAG_ASSERT_EQ(kNone, NONE);
REGEXP_FLAG_ASSERT_EQ(kGlobal, GLOBAL);
REGEXP_FLAG_ASSERT_EQ(kIgnoreCase, IGNORE_CASE);
REGEXP_FLAG_ASSERT_EQ(kMultiline, MULTILINE);
#undef REGEXP_FLAG_ASSERT_EQ

v8::RegExp::Flags v8::RegExp::GetFlags() const {
  i::Handle<i::JSRegExp> obj = Utils::OpenHandle(this);
  return static_cast<RegExp::Flags>(obj->GetFlags().value());
}


Local<v8::Array> v8::Array::New(Isolate* isolate, int length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "Array::New");
  ENTER_V8(i_isolate);
  int real_length = length > 0 ? length : 0;
  i::Handle<i::JSArray> obj = i_isolate->factory()->NewJSArray(real_length);
  i::Handle<i::Object> length_obj =
      i_isolate->factory()->NewNumberFromInt(real_length);
  obj->set_length(*length_obj);
  return Utils::ToLocal(obj);
}


uint32_t v8::Array::Length() const {
  i::Handle<i::JSArray> obj = Utils::OpenHandle(this);
  i::Object* length = obj->length();
  if (length->IsSmi()) {
    return i::Smi::cast(length)->value();
  } else {
    return static_cast<uint32_t>(length->Number());
  }
}


MaybeLocal<Object> Array::CloneElementAt(Local<Context> context,
                                         uint32_t index) {
  PREPARE_FOR_EXECUTION(context, "v8::Array::CloneElementAt()", Object);
  auto self = Utils::OpenHandle(this);
  if (!self->HasFastObjectElements()) return Local<Object>();
  i::FixedArray* elms = i::FixedArray::cast(self->elements());
  i::Object* paragon = elms->get(index);
  if (!paragon->IsJSObject()) return Local<Object>();
  i::Handle<i::JSObject> paragon_handle(i::JSObject::cast(paragon));
  Local<Object> result;
  has_pending_exception = ToLocal<Object>(
      isolate->factory()->CopyJSObject(paragon_handle), &result);
  RETURN_ON_FAILED_EXECUTION(Object);
  RETURN_ESCAPED(result);
}


Local<Object> Array::CloneElementAt(uint32_t index) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(CloneElementAt(context, index), Object);
}


bool Value::IsPromise() const {
  auto self = Utils::OpenHandle(this);
  if (!self->IsJSObject()) return false;
  auto js_object = i::Handle<i::JSObject>::cast(self);
  // Promises can't have access checks.
  if (js_object->map()->is_access_check_needed()) return false;
  auto isolate = js_object->GetIsolate();
  // TODO(dcarney): this should just be read from the symbol registry so as not
  // to be context dependent.
  auto key = isolate->promise_status();
  // Shouldn't be possible to throw here.
  return i::JSObject::HasRealNamedProperty(js_object, key).FromJust();
}


MaybeLocal<Promise::Resolver> Promise::Resolver::New(Local<Context> context) {
  PREPARE_FOR_EXECUTION(context, "Promise::Resolver::New", Resolver);
  i::Handle<i::Object> result;
  has_pending_exception = !i::Execution::Call(
      isolate,
      isolate->promise_create(),
      isolate->factory()->undefined_value(),
      0, NULL,
      false).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Promise::Resolver);
  RETURN_ESCAPED(Local<Promise::Resolver>::Cast(Utils::ToLocal(result)));
}


Local<Promise::Resolver> Promise::Resolver::New(Isolate* isolate) {
  RETURN_TO_LOCAL_UNCHECKED(New(isolate->GetCurrentContext()),
                            Promise::Resolver);
}


Local<Promise> Promise::Resolver::GetPromise() {
  i::Handle<i::JSObject> promise = Utils::OpenHandle(this);
  return Local<Promise>::Cast(Utils::ToLocal(promise));
}


Maybe<bool> Promise::Resolver::Resolve(Local<Context> context,
                                       Handle<Value> value) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "Promise::Resolver::Resolve", bool);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> argv[] = {self, Utils::OpenHandle(*value)};
  has_pending_exception = i::Execution::Call(
      isolate,
      isolate->promise_resolve(),
      isolate->factory()->undefined_value(),
      arraysize(argv), argv,
      false).is_null();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(true);
}


void Promise::Resolver::Resolve(Handle<Value> value) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  Resolve(context, value);
}


Maybe<bool> Promise::Resolver::Reject(Local<Context> context,
                                      Handle<Value> value) {
  PREPARE_FOR_EXECUTION_PRIMITIVE(context, "Promise::Resolver::Resolve", bool);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> argv[] = {self, Utils::OpenHandle(*value)};
  has_pending_exception = i::Execution::Call(
      isolate,
      isolate->promise_reject(),
      isolate->factory()->undefined_value(),
      arraysize(argv), argv,
      false).is_null();
  RETURN_ON_FAILED_EXECUTION_PRIMITIVE(bool);
  return Just(true);
}


void Promise::Resolver::Reject(Handle<Value> value) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  Reject(context, value);
}


MaybeLocal<Promise> Promise::Chain(Local<Context> context,
                                   Handle<Function> handler) {
  PREPARE_FOR_EXECUTION(context, "Promise::Chain", Promise);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> argv[] = {Utils::OpenHandle(*handler)};
  i::Handle<i::Object> result;
  has_pending_exception =
      !i::Execution::Call(isolate, isolate->promise_chain(), self,
                          arraysize(argv), argv, false).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Promise);
  RETURN_ESCAPED(Local<Promise>::Cast(Utils::ToLocal(result)));
}


Local<Promise> Promise::Chain(Handle<Function> handler) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(Chain(context, handler), Promise);
}


MaybeLocal<Promise> Promise::Catch(Local<Context> context,
                                   Handle<Function> handler) {
  PREPARE_FOR_EXECUTION(context, "Promise::Catch", Promise);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> argv[] = { Utils::OpenHandle(*handler) };
  i::Handle<i::Object> result;
  has_pending_exception =
      !i::Execution::Call(isolate, isolate->promise_catch(), self,
                          arraysize(argv), argv, false).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Promise);
  RETURN_ESCAPED(Local<Promise>::Cast(Utils::ToLocal(result)));
}


Local<Promise> Promise::Catch(Handle<Function> handler) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(Catch(context, handler), Promise);
}


MaybeLocal<Promise> Promise::Then(Local<Context> context,
                                  Handle<Function> handler) {
  PREPARE_FOR_EXECUTION(context, "Promise::Then", Promise);
  auto self = Utils::OpenHandle(this);
  i::Handle<i::Object> argv[] = { Utils::OpenHandle(*handler) };
  i::Handle<i::Object> result;
  has_pending_exception =
      !i::Execution::Call(isolate, isolate->promise_then(), self,
                          arraysize(argv), argv, false).ToHandle(&result);
  RETURN_ON_FAILED_EXECUTION(Promise);
  RETURN_ESCAPED(Local<Promise>::Cast(Utils::ToLocal(result)));
}


Local<Promise> Promise::Then(Handle<Function> handler) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(this));
  RETURN_TO_LOCAL_UNCHECKED(Then(context, handler), Promise);
}


bool Promise::HasHandler() {
  i::Handle<i::JSObject> promise = Utils::OpenHandle(this);
  i::Isolate* isolate = promise->GetIsolate();
  LOG_API(isolate, "Promise::HasRejectHandler");
  ENTER_V8(isolate);
  i::Handle<i::Symbol> key = isolate->factory()->promise_has_handler_symbol();
  return i::JSObject::GetDataProperty(promise, key)->IsTrue();
}


bool v8::ArrayBuffer::IsExternal() const {
  return Utils::OpenHandle(this)->is_external();
}


bool v8::ArrayBuffer::IsNeuterable() const {
  return Utils::OpenHandle(this)->is_neuterable();
}


v8::ArrayBuffer::Contents v8::ArrayBuffer::Externalize() {
  i::Handle<i::JSArrayBuffer> obj = Utils::OpenHandle(this);
  Utils::ApiCheck(!obj->is_external(),
                  "v8::ArrayBuffer::Externalize",
                  "ArrayBuffer already externalized");
  obj->set_is_external(true);
  size_t byte_length = static_cast<size_t>(obj->byte_length()->Number());
  Contents contents;
  contents.data_ = obj->backing_store();
  contents.byte_length_ = byte_length;
  return contents;
}


void v8::ArrayBuffer::Neuter() {
  i::Handle<i::JSArrayBuffer> obj = Utils::OpenHandle(this);
  i::Isolate* isolate = obj->GetIsolate();
  Utils::ApiCheck(obj->is_external(),
                  "v8::ArrayBuffer::Neuter",
                  "Only externalized ArrayBuffers can be neutered");
  Utils::ApiCheck(obj->is_neuterable(), "v8::ArrayBuffer::Neuter",
                  "Only neuterable ArrayBuffers can be neutered");
  LOG_API(obj->GetIsolate(), "v8::ArrayBuffer::Neuter()");
  ENTER_V8(isolate);
  i::Runtime::NeuterArrayBuffer(obj);
}


size_t v8::ArrayBuffer::ByteLength() const {
  i::Handle<i::JSArrayBuffer> obj = Utils::OpenHandle(this);
  return static_cast<size_t>(obj->byte_length()->Number());
}


Local<ArrayBuffer> v8::ArrayBuffer::New(Isolate* isolate, size_t byte_length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "v8::ArrayBuffer::New(size_t)");
  ENTER_V8(i_isolate);
  i::Handle<i::JSArrayBuffer> obj =
      i_isolate->factory()->NewJSArrayBuffer();
  i::Runtime::SetupArrayBufferAllocatingData(i_isolate, obj, byte_length);
  return Utils::ToLocal(obj);
}


Local<ArrayBuffer> v8::ArrayBuffer::New(Isolate* isolate, void* data,
                                        size_t byte_length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "v8::ArrayBuffer::New(void*, size_t)");
  ENTER_V8(i_isolate);
  i::Handle<i::JSArrayBuffer> obj =
      i_isolate->factory()->NewJSArrayBuffer();
  i::Runtime::SetupArrayBuffer(i_isolate, obj, true, data, byte_length);
  return Utils::ToLocal(obj);
}


Local<ArrayBuffer> v8::ArrayBufferView::Buffer() {
  i::Handle<i::JSArrayBufferView> obj = Utils::OpenHandle(this);
  i::Handle<i::JSArrayBuffer> buffer;
  if (obj->IsJSDataView()) {
    i::Handle<i::JSDataView> data_view(i::JSDataView::cast(*obj));
    DCHECK(data_view->buffer()->IsJSArrayBuffer());
    buffer = i::handle(i::JSArrayBuffer::cast(data_view->buffer()));
  } else {
    DCHECK(obj->IsJSTypedArray());
    buffer = i::JSTypedArray::cast(*obj)->GetBuffer();
  }
  return Utils::ToLocal(buffer);
}


size_t v8::ArrayBufferView::ByteOffset() {
  i::Handle<i::JSArrayBufferView> obj = Utils::OpenHandle(this);
  return static_cast<size_t>(obj->byte_offset()->Number());
}


size_t v8::ArrayBufferView::ByteLength() {
  i::Handle<i::JSArrayBufferView> obj = Utils::OpenHandle(this);
  return static_cast<size_t>(obj->byte_length()->Number());
}


size_t v8::TypedArray::Length() {
  i::Handle<i::JSTypedArray> obj = Utils::OpenHandle(this);
  return static_cast<size_t>(obj->length()->Number());
}


#define TYPED_ARRAY_NEW(Type, type, TYPE, ctype, size)                       \
  Local<Type##Array> Type##Array::New(Handle<ArrayBuffer> array_buffer,      \
                                      size_t byte_offset, size_t length) {   \
    i::Isolate* isolate = Utils::OpenHandle(*array_buffer)->GetIsolate();    \
    LOG_API(isolate,                                                         \
            "v8::" #Type "Array::New(Handle<ArrayBuffer>, size_t, size_t)"); \
    ENTER_V8(isolate);                                                       \
    if (!Utils::ApiCheck(length <= static_cast<size_t>(i::Smi::kMaxValue),   \
                         "v8::" #Type                                        \
                         "Array::New(Handle<ArrayBuffer>, size_t, size_t)",  \
                         "length exceeds max allowed value")) {              \
      return Local<Type##Array>();                                           \
    }                                                                        \
    i::Handle<i::JSArrayBuffer> buffer = Utils::OpenHandle(*array_buffer);   \
    i::Handle<i::JSTypedArray> obj = isolate->factory()->NewJSTypedArray(    \
        v8::kExternal##Type##Array, buffer, byte_offset, length);            \
    return Utils::ToLocal##Type##Array(obj);                                 \
  }


TYPED_ARRAYS(TYPED_ARRAY_NEW)
#undef TYPED_ARRAY_NEW

Local<DataView> DataView::New(Handle<ArrayBuffer> array_buffer,
                              size_t byte_offset, size_t byte_length) {
  i::Handle<i::JSArrayBuffer> buffer = Utils::OpenHandle(*array_buffer);
  i::Isolate* isolate = buffer->GetIsolate();
  LOG_API(isolate, "v8::DataView::New(void*, size_t, size_t)");
  ENTER_V8(isolate);
  i::Handle<i::JSDataView> obj =
      isolate->factory()->NewJSDataView(buffer, byte_offset, byte_length);
  return Utils::ToLocal(obj);
}


Local<Symbol> v8::Symbol::New(Isolate* isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "Symbol::New()");
  ENTER_V8(i_isolate);
  i::Handle<i::Symbol> result = i_isolate->factory()->NewSymbol();
  if (!name.IsEmpty()) result->set_name(*Utils::OpenHandle(*name));
  return Utils::ToLocal(result);
}


static i::Handle<i::Symbol> SymbolFor(i::Isolate* isolate,
                                      i::Handle<i::String> name,
                                      i::Handle<i::String> part) {
  i::Handle<i::JSObject> registry = isolate->GetSymbolRegistry();
  i::Handle<i::JSObject> symbols =
      i::Handle<i::JSObject>::cast(
          i::Object::GetPropertyOrElement(registry, part).ToHandleChecked());
  i::Handle<i::Object> symbol =
      i::Object::GetPropertyOrElement(symbols, name).ToHandleChecked();
  if (!symbol->IsSymbol()) {
    DCHECK(symbol->IsUndefined());
    symbol = isolate->factory()->NewSymbol();
    i::Handle<i::Symbol>::cast(symbol)->set_name(*name);
    i::JSObject::SetProperty(symbols, name, symbol, i::STRICT).Assert();
  }
  return i::Handle<i::Symbol>::cast(symbol);
}


Local<Symbol> v8::Symbol::For(Isolate* isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::Handle<i::String> i_name = Utils::OpenHandle(*name);
  i::Handle<i::String> part = i_isolate->factory()->for_string();
  return Utils::ToLocal(SymbolFor(i_isolate, i_name, part));
}


Local<Symbol> v8::Symbol::ForApi(Isolate* isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::Handle<i::String> i_name = Utils::OpenHandle(*name);
  i::Handle<i::String> part = i_isolate->factory()->for_api_string();
  return Utils::ToLocal(SymbolFor(i_isolate, i_name, part));
}


Local<Symbol> v8::Symbol::GetIterator(Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return Utils::ToLocal(i_isolate->factory()->iterator_symbol());
}


Local<Symbol> v8::Symbol::GetUnscopables(Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return Utils::ToLocal(i_isolate->factory()->unscopables_symbol());
}


Local<Symbol> v8::Symbol::GetToStringTag(Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return Utils::ToLocal(i_isolate->factory()->to_string_tag_symbol());
}


Local<Private> v8::Private::New(Isolate* isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  LOG_API(i_isolate, "Private::New()");
  ENTER_V8(i_isolate);
  i::Handle<i::Symbol> symbol = i_isolate->factory()->NewPrivateSymbol();
  if (!name.IsEmpty()) symbol->set_name(*Utils::OpenHandle(*name));
  Local<Symbol> result = Utils::ToLocal(symbol);
  return v8::Handle<Private>(reinterpret_cast<Private*>(*result));
}


Local<Private> v8::Private::ForApi(Isolate* isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::Handle<i::String> i_name = Utils::OpenHandle(*name);
  i::Handle<i::JSObject> registry = i_isolate->GetSymbolRegistry();
  i::Handle<i::String> part = i_isolate->factory()->private_api_string();
  i::Handle<i::JSObject> privates =
      i::Handle<i::JSObject>::cast(
          i::Object::GetPropertyOrElement(registry, part).ToHandleChecked());
  i::Handle<i::Object> symbol =
      i::Object::GetPropertyOrElement(privates, i_name).ToHandleChecked();
  if (!symbol->IsSymbol()) {
    DCHECK(symbol->IsUndefined());
    symbol = i_isolate->factory()->NewPrivateSymbol();
    i::Handle<i::Symbol>::cast(symbol)->set_name(*i_name);
    i::JSObject::SetProperty(privates, i_name, symbol, i::STRICT).Assert();
  }
  Local<Symbol> result = Utils::ToLocal(i::Handle<i::Symbol>::cast(symbol));
  return v8::Handle<Private>(reinterpret_cast<Private*>(*result));
}


Local<Number> v8::Number::New(Isolate* isolate, double value) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  if (std::isnan(value)) {
    // Introduce only canonical NaN value into the VM, to avoid signaling NaNs.
    value = std::numeric_limits<double>::quiet_NaN();
  }
  ENTER_V8(internal_isolate);
  i::Handle<i::Object> result = internal_isolate->factory()->NewNumber(value);
  return Utils::NumberToLocal(result);
}


Local<Integer> v8::Integer::New(Isolate* isolate, int32_t value) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  if (i::Smi::IsValid(value)) {
    return Utils::IntegerToLocal(i::Handle<i::Object>(i::Smi::FromInt(value),
                                                      internal_isolate));
  }
  ENTER_V8(internal_isolate);
  i::Handle<i::Object> result = internal_isolate->factory()->NewNumber(value);
  return Utils::IntegerToLocal(result);
}


Local<Integer> v8::Integer::NewFromUnsigned(Isolate* isolate, uint32_t value) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  bool fits_into_int32_t = (value & (1 << 31)) == 0;
  if (fits_into_int32_t) {
    return Integer::New(isolate, static_cast<int32_t>(value));
  }
  ENTER_V8(internal_isolate);
  i::Handle<i::Object> result = internal_isolate->factory()->NewNumber(value);
  return Utils::IntegerToLocal(result);
}


void Isolate::CollectAllGarbage(const char* gc_reason) {
  reinterpret_cast<i::Isolate*>(this)->heap()->CollectAllGarbage(
      i::Heap::kNoGCFlags, gc_reason);
}


HeapProfiler* Isolate::GetHeapProfiler() {
  i::HeapProfiler* heap_profiler =
      reinterpret_cast<i::Isolate*>(this)->heap_profiler();
  return reinterpret_cast<HeapProfiler*>(heap_profiler);
}


CpuProfiler* Isolate::GetCpuProfiler() {
  i::CpuProfiler* cpu_profiler =
      reinterpret_cast<i::Isolate*>(this)->cpu_profiler();
  return reinterpret_cast<CpuProfiler*>(cpu_profiler);
}


bool Isolate::InContext() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  return isolate->context() != NULL;
}


v8::Local<v8::Context> Isolate::GetCurrentContext() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::Context* context = isolate->context();
  if (context == NULL) return Local<Context>();
  i::Context* native_context = context->native_context();
  if (native_context == NULL) return Local<Context>();
  return Utils::ToLocal(i::Handle<i::Context>(native_context));
}


v8::Local<v8::Context> Isolate::GetCallingContext() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::Handle<i::Object> calling = isolate->GetCallingNativeContext();
  if (calling.is_null()) return Local<Context>();
  return Utils::ToLocal(i::Handle<i::Context>::cast(calling));
}


v8::Local<v8::Context> Isolate::GetEnteredContext() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::Handle<i::Object> last =
      isolate->handle_scope_implementer()->LastEnteredContext();
  if (last.is_null()) return Local<Context>();
  return Utils::ToLocal(i::Handle<i::Context>::cast(last));
}


v8::Local<Value> Isolate::ThrowException(v8::Local<v8::Value> value) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  ENTER_V8(isolate);
  // If we're passed an empty handle, we throw an undefined exception
  // to deal more gracefully with out of memory situations.
  if (value.IsEmpty()) {
    isolate->ScheduleThrow(isolate->heap()->undefined_value());
  } else {
    isolate->ScheduleThrow(*Utils::OpenHandle(*value));
  }
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
}


void Isolate::SetObjectGroupId(internal::Object** object, UniqueId id) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(this);
  internal_isolate->global_handles()->SetObjectGroupId(
      v8::internal::Handle<v8::internal::Object>(object).location(),
      id);
}


void Isolate::SetReferenceFromGroup(UniqueId id, internal::Object** object) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(this);
  internal_isolate->global_handles()->SetReferenceFromGroup(
      id,
      v8::internal::Handle<v8::internal::Object>(object).location());
}


void Isolate::SetReference(internal::Object** parent,
                           internal::Object** child) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(this);
  i::Object** parent_location =
      v8::internal::Handle<v8::internal::Object>(parent).location();
  internal_isolate->global_handles()->SetReference(
      reinterpret_cast<i::HeapObject**>(parent_location),
      v8::internal::Handle<v8::internal::Object>(child).location());
}


void Isolate::AddGCPrologueCallback(GCPrologueCallback callback,
                                    GCType gc_type) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->heap()->AddGCPrologueCallback(callback, gc_type);
}


void Isolate::RemoveGCPrologueCallback(GCPrologueCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->heap()->RemoveGCPrologueCallback(callback);
}


void Isolate::AddGCEpilogueCallback(GCEpilogueCallback callback,
                                    GCType gc_type) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->heap()->AddGCEpilogueCallback(callback, gc_type);
}


void Isolate::RemoveGCEpilogueCallback(GCEpilogueCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->heap()->RemoveGCEpilogueCallback(callback);
}


void V8::AddGCPrologueCallback(GCPrologueCallback callback, GCType gc_type) {
  i::Isolate* isolate = i::Isolate::Current();
  isolate->heap()->AddGCPrologueCallback(
      reinterpret_cast<v8::Isolate::GCPrologueCallback>(callback),
      gc_type,
      false);
}


void V8::AddGCEpilogueCallback(GCEpilogueCallback callback, GCType gc_type) {
  i::Isolate* isolate = i::Isolate::Current();
  isolate->heap()->AddGCEpilogueCallback(
      reinterpret_cast<v8::Isolate::GCEpilogueCallback>(callback),
      gc_type,
      false);
}


void Isolate::AddMemoryAllocationCallback(MemoryAllocationCallback callback,
                                          ObjectSpace space,
                                          AllocationAction action) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->memory_allocator()->AddMemoryAllocationCallback(
      callback, space, action);
}


void Isolate::RemoveMemoryAllocationCallback(
    MemoryAllocationCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->memory_allocator()->RemoveMemoryAllocationCallback(
      callback);
}


void Isolate::TerminateExecution() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->stack_guard()->RequestTerminateExecution();
}


bool Isolate::IsExecutionTerminating() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  return IsExecutionTerminatingCheck(isolate);
}


void Isolate::CancelTerminateExecution() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->stack_guard()->ClearTerminateExecution();
  isolate->CancelTerminateExecution();
}


void Isolate::RequestInterrupt(InterruptCallback callback, void* data) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->RequestInterrupt(callback, data);
}


void Isolate::ClearInterrupt() {
}


void Isolate::RequestGarbageCollectionForTesting(GarbageCollectionType type) {
  CHECK(i::FLAG_expose_gc);
  if (type == kMinorGarbageCollection) {
    reinterpret_cast<i::Isolate*>(this)->heap()->CollectGarbage(
        i::NEW_SPACE, "Isolate::RequestGarbageCollection",
        kGCCallbackFlagForced);
  } else {
    DCHECK_EQ(kFullGarbageCollection, type);
    reinterpret_cast<i::Isolate*>(this)->heap()->CollectAllGarbage(
        i::Heap::kAbortIncrementalMarkingMask,
        "Isolate::RequestGarbageCollection", kGCCallbackFlagForced);
  }
}


Isolate* Isolate::GetCurrent() {
  i::Isolate* isolate = i::Isolate::Current();
  return reinterpret_cast<Isolate*>(isolate);
}


Isolate* Isolate::New(const Isolate::CreateParams& params) {
  i::Isolate* isolate = new i::Isolate(false);
  Isolate* v8_isolate = reinterpret_cast<Isolate*>(isolate);
  if (params.snapshot_blob != NULL) {
    isolate->set_snapshot_blob(params.snapshot_blob);
  } else {
    isolate->set_snapshot_blob(i::Snapshot::DefaultSnapshotBlob());
  }
  if (params.entry_hook) {
    isolate->set_function_entry_hook(params.entry_hook);
  }
  if (params.code_event_handler) {
    isolate->InitializeLoggingAndCounters();
    isolate->logger()->SetCodeEventHandler(kJitCodeEventDefault,
                                           params.code_event_handler);
  }
  SetResourceConstraints(isolate, params.constraints);
  // TODO(jochen): Once we got rid of Isolate::Current(), we can remove this.
  Isolate::Scope isolate_scope(v8_isolate);
  if (params.entry_hook || !i::Snapshot::Initialize(isolate)) {
    // If the isolate has a function entry hook, it needs to re-build all its
    // code stubs with entry hooks embedded, so don't deserialize a snapshot.
    if (i::Snapshot::EmbedsScript(isolate)) {
      // If the snapshot embeds a script, we cannot initialize the isolate
      // without the snapshot as a fallback. This is unlikely to happen though.
      V8_Fatal(__FILE__, __LINE__,
               "Initializing isolate from custom startup snapshot failed");
    }
    isolate->Init(NULL);
  }
  return v8_isolate;
}


void Isolate::Dispose() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  if (!Utils::ApiCheck(!isolate->IsInUse(),
                       "v8::Isolate::Dispose()",
                       "Disposing the isolate that is entered by a thread.")) {
    return;
  }
  isolate->TearDown();
}


void Isolate::Enter() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->Enter();
}


void Isolate::Exit() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->Exit();
}


Isolate::DisallowJavascriptExecutionScope::DisallowJavascriptExecutionScope(
    Isolate* isolate,
    Isolate::DisallowJavascriptExecutionScope::OnFailure on_failure)
    : on_failure_(on_failure) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  if (on_failure_ == CRASH_ON_FAILURE) {
    internal_ = reinterpret_cast<void*>(
        new i::DisallowJavascriptExecution(i_isolate));
  } else {
    DCHECK_EQ(THROW_ON_FAILURE, on_failure);
    internal_ = reinterpret_cast<void*>(
        new i::ThrowOnJavascriptExecution(i_isolate));
  }
}


Isolate::DisallowJavascriptExecutionScope::~DisallowJavascriptExecutionScope() {
  if (on_failure_ == CRASH_ON_FAILURE) {
    delete reinterpret_cast<i::DisallowJavascriptExecution*>(internal_);
  } else {
    delete reinterpret_cast<i::ThrowOnJavascriptExecution*>(internal_);
  }
}


Isolate::AllowJavascriptExecutionScope::AllowJavascriptExecutionScope(
    Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  internal_assert_ = reinterpret_cast<void*>(
      new i::AllowJavascriptExecution(i_isolate));
  internal_throws_ = reinterpret_cast<void*>(
      new i::NoThrowOnJavascriptExecution(i_isolate));
}


Isolate::AllowJavascriptExecutionScope::~AllowJavascriptExecutionScope() {
  delete reinterpret_cast<i::AllowJavascriptExecution*>(internal_assert_);
  delete reinterpret_cast<i::NoThrowOnJavascriptExecution*>(internal_throws_);
}


Isolate::SuppressMicrotaskExecutionScope::SuppressMicrotaskExecutionScope(
    Isolate* isolate)
    : isolate_(reinterpret_cast<i::Isolate*>(isolate)) {
  isolate_->handle_scope_implementer()->IncrementCallDepth();
}


Isolate::SuppressMicrotaskExecutionScope::~SuppressMicrotaskExecutionScope() {
  isolate_->handle_scope_implementer()->DecrementCallDepth();
}


void Isolate::GetHeapStatistics(HeapStatistics* heap_statistics) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::Heap* heap = isolate->heap();
  heap_statistics->total_heap_size_ = heap->CommittedMemory();
  heap_statistics->total_heap_size_executable_ =
      heap->CommittedMemoryExecutable();
  heap_statistics->total_physical_size_ = heap->CommittedPhysicalMemory();
  heap_statistics->used_heap_size_ = heap->SizeOfObjects();
  heap_statistics->heap_size_limit_ = heap->MaxReserved();
}


void Isolate::GetStackSample(const RegisterState& state, void** frames,
                             size_t frames_limit, SampleInfo* sample_info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::TickSample::GetStackSample(isolate, state, i::TickSample::kSkipCEntryFrame,
                                frames, frames_limit, sample_info);
}


void Isolate::SetEventLogger(LogEventCallback that) {
  // Do not overwrite the event logger if we want to log explicitly.
  if (i::FLAG_log_internal_timer_events) return;
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->set_event_logger(that);
}


void Isolate::AddCallCompletedCallback(CallCompletedCallback callback) {
  if (callback == NULL) return;
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->AddCallCompletedCallback(callback);
}


void Isolate::RemoveCallCompletedCallback(CallCompletedCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->RemoveCallCompletedCallback(callback);
}


void Isolate::SetPromiseRejectCallback(PromiseRejectCallback callback) {
  if (callback == NULL) return;
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->SetPromiseRejectCallback(callback);
}


void Isolate::RunMicrotasks() {
  reinterpret_cast<i::Isolate*>(this)->RunMicrotasks();
}


void Isolate::EnqueueMicrotask(Handle<Function> microtask) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->EnqueueMicrotask(Utils::OpenHandle(*microtask));
}


void Isolate::EnqueueMicrotask(MicrotaskCallback microtask, void* data) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::HandleScope scope(isolate);
  i::Handle<i::CallHandlerInfo> callback_info =
      i::Handle<i::CallHandlerInfo>::cast(
          isolate->factory()->NewStruct(i::CALL_HANDLER_INFO_TYPE));
  SET_FIELD_WRAPPED(callback_info, set_callback, microtask);
  SET_FIELD_WRAPPED(callback_info, set_data, data);
  isolate->EnqueueMicrotask(callback_info);
}


void Isolate::SetAutorunMicrotasks(bool autorun) {
  reinterpret_cast<i::Isolate*>(this)->set_autorun_microtasks(autorun);
}


bool Isolate::WillAutorunMicrotasks() const {
  return reinterpret_cast<const i::Isolate*>(this)->autorun_microtasks();
}


void Isolate::SetUseCounterCallback(UseCounterCallback callback) {
  reinterpret_cast<i::Isolate*>(this)->SetUseCounterCallback(callback);
}


void Isolate::SetCounterFunction(CounterLookupCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->stats_table()->SetCounterFunction(callback);
  isolate->InitializeLoggingAndCounters();
  isolate->counters()->ResetCounters();
}


void Isolate::SetCreateHistogramFunction(CreateHistogramCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->stats_table()->SetCreateHistogramFunction(callback);
  isolate->InitializeLoggingAndCounters();
  isolate->counters()->ResetHistograms();
}


void Isolate::SetAddHistogramSampleFunction(
    AddHistogramSampleCallback callback) {
  reinterpret_cast<i::Isolate*>(this)
      ->stats_table()
      ->SetAddHistogramSampleFunction(callback);
}


bool Isolate::IdleNotification(int idle_time_in_ms) {
  // Returning true tells the caller that it need not
  // continue to call IdleNotification.
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  if (!i::FLAG_use_idle_notification) return true;
  return isolate->heap()->IdleNotification(idle_time_in_ms);
}


bool Isolate::IdleNotificationDeadline(double deadline_in_seconds) {
  // Returning true tells the caller that it need not
  // continue to call IdleNotification.
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  if (!i::FLAG_use_idle_notification) return true;
  return isolate->heap()->IdleNotification(deadline_in_seconds);
}


void Isolate::LowMemoryNotification() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  {
    i::HistogramTimerScope idle_notification_scope(
        isolate->counters()->gc_low_memory_notification());
    isolate->heap()->CollectAllAvailableGarbage("low memory notification");
  }
}


int Isolate::ContextDisposedNotification(bool dependant_context) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  return isolate->heap()->NotifyContextDisposed(dependant_context);
}


void Isolate::SetJitCodeEventHandler(JitCodeEventOptions options,
                                     JitCodeEventHandler event_handler) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  // Ensure that logging is initialized for our isolate.
  isolate->InitializeLoggingAndCounters();
  isolate->logger()->SetCodeEventHandler(options, event_handler);
}


void Isolate::SetStackLimit(uintptr_t stack_limit) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  CHECK(stack_limit);
  isolate->stack_guard()->SetStackLimit(stack_limit);
}


void Isolate::GetCodeRange(void** start, size_t* length_in_bytes) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  if (isolate->code_range()->valid()) {
    *start = isolate->code_range()->start();
    *length_in_bytes = isolate->code_range()->size();
  } else {
    *start = NULL;
    *length_in_bytes = 0;
  }
}


void Isolate::SetFatalErrorHandler(FatalErrorCallback that) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->set_exception_behavior(that);
}


void Isolate::SetAllowCodeGenerationFromStringsCallback(
    AllowCodeGenerationFromStringsCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->set_allow_code_gen_callback(callback);
}


bool Isolate::IsDead() {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  return isolate->IsDead();
}


bool Isolate::AddMessageListener(MessageCallback that, Handle<Value> data) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  NeanderArray listeners(isolate->factory()->message_listeners());
  NeanderObject obj(isolate, 2);
  obj.set(0, *isolate->factory()->NewForeign(FUNCTION_ADDR(that)));
  obj.set(1, data.IsEmpty() ? isolate->heap()->undefined_value()
                            : *Utils::OpenHandle(*data));
  listeners.add(isolate, obj.value());
  return true;
}


void Isolate::RemoveMessageListeners(MessageCallback that) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  NeanderArray listeners(isolate->factory()->message_listeners());
  for (int i = 0; i < listeners.length(); i++) {
    if (listeners.get(i)->IsUndefined()) continue;  // skip deleted ones

    NeanderObject listener(i::JSObject::cast(listeners.get(i)));
    i::Handle<i::Foreign> callback_obj(i::Foreign::cast(listener.get(0)));
    if (callback_obj->foreign_address() == FUNCTION_ADDR(that)) {
      listeners.set(i, isolate->heap()->undefined_value());
    }
  }
}


void Isolate::SetFailedAccessCheckCallbackFunction(
    FailedAccessCheckCallback callback) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->SetFailedAccessCheckCallback(callback);
}


void Isolate::SetCaptureStackTraceForUncaughtExceptions(
    bool capture, int frame_limit, StackTrace::StackTraceOptions options) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->SetCaptureStackTraceForUncaughtExceptions(capture, frame_limit,
                                                     options);
}


void Isolate::VisitExternalResources(ExternalResourceVisitor* visitor) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  isolate->heap()->VisitExternalResources(visitor);
}


class VisitorAdapter : public i::ObjectVisitor {
 public:
  explicit VisitorAdapter(PersistentHandleVisitor* visitor)
      : visitor_(visitor) {}
  virtual void VisitPointers(i::Object** start, i::Object** end) {
    UNREACHABLE();
  }
  virtual void VisitEmbedderReference(i::Object** p, uint16_t class_id) {
    Value* value = ToApi<Value>(i::Handle<i::Object>(p));
    visitor_->VisitPersistentHandle(
        reinterpret_cast<Persistent<Value>*>(&value), class_id);
  }

 private:
  PersistentHandleVisitor* visitor_;
};


void Isolate::VisitHandlesWithClassIds(PersistentHandleVisitor* visitor) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::DisallowHeapAllocation no_allocation;
  VisitorAdapter visitor_adapter(visitor);
  isolate->global_handles()->IterateAllRootsWithClassIds(&visitor_adapter);
}


void Isolate::VisitHandlesForPartialDependence(
    PersistentHandleVisitor* visitor) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(this);
  i::DisallowHeapAllocation no_allocation;
  VisitorAdapter visitor_adapter(visitor);
  isolate->global_handles()->IterateAllRootsInNewSpaceWithClassIds(
      &visitor_adapter);
}


String::Utf8Value::Utf8Value(v8::Handle<v8::Value> obj)
    : str_(NULL), length_(0) {
  i::Isolate* isolate = i::Isolate::Current();
  if (obj.IsEmpty()) return;
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  TryCatch try_catch;
  Handle<String> str = obj->ToString(reinterpret_cast<v8::Isolate*>(isolate));
  if (str.IsEmpty()) return;
  i::Handle<i::String> i_str = Utils::OpenHandle(*str);
  length_ = v8::Utf8Length(*i_str, isolate);
  str_ = i::NewArray<char>(length_ + 1);
  str->WriteUtf8(str_);
}


String::Utf8Value::~Utf8Value() {
  i::DeleteArray(str_);
}


String::Value::Value(v8::Handle<v8::Value> obj)
    : str_(NULL), length_(0) {
  i::Isolate* isolate = i::Isolate::Current();
  if (obj.IsEmpty()) return;
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  TryCatch try_catch;
  Handle<String> str = obj->ToString(reinterpret_cast<v8::Isolate*>(isolate));
  if (str.IsEmpty()) return;
  length_ = str->Length();
  str_ = i::NewArray<uint16_t>(length_ + 1);
  str->Write(str_);
}


String::Value::~Value() {
  i::DeleteArray(str_);
}


#define DEFINE_ERROR(NAME)                                                    \
  Local<Value> Exception::NAME(v8::Handle<v8::String> raw_message) {          \
    i::Isolate* isolate = i::Isolate::Current();                              \
    LOG_API(isolate, #NAME);                                                  \
    ENTER_V8(isolate);                                                        \
    i::Object* error;                                                         \
    {                                                                         \
      i::HandleScope scope(isolate);                                          \
      i::Handle<i::String> message = Utils::OpenHandle(*raw_message);         \
      error = *isolate->factory()->New##NAME(message);                        \
    }                                                                         \
    i::Handle<i::Object> result(error, isolate);                              \
    return Utils::ToLocal(result);                                            \
  }

DEFINE_ERROR(RangeError)
DEFINE_ERROR(ReferenceError)
DEFINE_ERROR(SyntaxError)
DEFINE_ERROR(TypeError)
DEFINE_ERROR(Error)

#undef DEFINE_ERROR


Local<Message> Exception::CreateMessage(Handle<Value> exception) {
  i::Handle<i::Object> obj = Utils::OpenHandle(*exception);
  if (!obj->IsHeapObject()) return Local<Message>();
  i::Isolate* isolate = i::HeapObject::cast(*obj)->GetIsolate();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  return Utils::MessageToLocal(
      scope.CloseAndEscape(isolate->CreateMessage(obj, NULL)));
}


Local<StackTrace> Exception::GetStackTrace(Handle<Value> exception) {
  i::Handle<i::Object> obj = Utils::OpenHandle(*exception);
  if (!obj->IsJSObject()) return Local<StackTrace>();
  i::Handle<i::JSObject> js_obj = i::Handle<i::JSObject>::cast(obj);
  i::Isolate* isolate = js_obj->GetIsolate();
  ENTER_V8(isolate);
  return Utils::StackTraceToLocal(isolate->GetDetailedStackTrace(js_obj));
}


// --- D e b u g   S u p p o r t ---

bool Debug::SetDebugEventListener(EventCallback that, Handle<Value> data) {
  i::Isolate* isolate = i::Isolate::Current();
  ENTER_V8(isolate);
  i::HandleScope scope(isolate);
  i::Handle<i::Object> foreign = isolate->factory()->undefined_value();
  if (that != NULL) {
    foreign = isolate->factory()->NewForeign(FUNCTION_ADDR(that));
  }
  isolate->debug()->SetEventListener(foreign,
                                     Utils::OpenHandle(*data, true));
  return true;
}


void Debug::DebugBreak(Isolate* isolate) {
  reinterpret_cast<i::Isolate*>(isolate)->stack_guard()->RequestDebugBreak();
}


void Debug::CancelDebugBreak(Isolate* isolate) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  internal_isolate->stack_guard()->ClearDebugBreak();
}


bool Debug::CheckDebugBreak(Isolate* isolate) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return internal_isolate->stack_guard()->CheckDebugBreak();
}


void Debug::DebugBreakForCommand(Isolate* isolate, ClientData* data) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  internal_isolate->debug()->EnqueueDebugCommand(data);
}


void Debug::SetMessageHandler(v8::Debug::MessageHandler handler) {
  i::Isolate* isolate = i::Isolate::Current();
  ENTER_V8(isolate);
  isolate->debug()->SetMessageHandler(handler);
}


void Debug::SendCommand(Isolate* isolate,
                        const uint16_t* command,
                        int length,
                        ClientData* client_data) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  internal_isolate->debug()->EnqueueCommandMessage(
      i::Vector<const uint16_t>(command, length), client_data);
}


MaybeLocal<Value> Debug::Call(Local<Context> context,
                              v8::Handle<v8::Function> fun,
                              v8::Handle<v8::Value> data) {
  PREPARE_FOR_EXECUTION(context, "v8::Debug::Call()", Value);
  i::Handle<i::Object> data_obj;
  if (data.IsEmpty()) {
    data_obj = isolate->factory()->undefined_value();
  } else {
    data_obj = Utils::OpenHandle(*data);
  }
  Local<Value> result;
  has_pending_exception =
      !ToLocal<Value>(isolate->debug()->Call(Utils::OpenHandle(*fun), data_obj),
                      &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<Value> Debug::Call(v8::Handle<v8::Function> fun,
                         v8::Handle<v8::Value> data) {
  auto context = ContextFromHeapObject(Utils::OpenHandle(*fun));
  RETURN_TO_LOCAL_UNCHECKED(Call(context, fun, data), Value);
}


MaybeLocal<Value> Debug::GetMirror(Local<Context> context,
                                   v8::Handle<v8::Value> obj) {
  PREPARE_FOR_EXECUTION(context, "v8::Debug::GetMirror()", Value);
  i::Debug* isolate_debug = isolate->debug();
  has_pending_exception = !isolate_debug->Load();
  RETURN_ON_FAILED_EXECUTION(Value);
  i::Handle<i::JSObject> debug(isolate_debug->debug_context()->global_object());
  auto name = isolate->factory()->NewStringFromStaticChars("MakeMirror");
  auto fun_obj = i::Object::GetProperty(debug, name).ToHandleChecked();
  auto v8_fun = Utils::ToLocal(i::Handle<i::JSFunction>::cast(fun_obj));
  const int kArgc = 1;
  v8::Handle<v8::Value> argv[kArgc] = {obj};
  Local<Value> result;
  has_pending_exception = !v8_fun->Call(context, Utils::ToLocal(debug), kArgc,
                                        argv).ToLocal(&result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}


Local<Value> Debug::GetMirror(v8::Handle<v8::Value> obj) {
  RETURN_TO_LOCAL_UNCHECKED(GetMirror(Local<Context>(), obj), Value);
}


void Debug::ProcessDebugMessages() {
  i::Isolate::Current()->debug()->ProcessDebugMessages(true);
}


Local<Context> Debug::GetDebugContext() {
  i::Isolate* isolate = i::Isolate::Current();
  ENTER_V8(isolate);
  return Utils::ToLocal(i::Isolate::Current()->debug()->GetDebugContext());
}


void Debug::SetLiveEditEnabled(Isolate* isolate, bool enable) {
  i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
  internal_isolate->debug()->set_live_edit_enabled(enable);
}


Handle<String> CpuProfileNode::GetFunctionName() const {
  i::Isolate* isolate = i::Isolate::Current();
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  const i::CodeEntry* entry = node->entry();
  i::Handle<i::String> name =
      isolate->factory()->InternalizeUtf8String(entry->name());
  if (!entry->has_name_prefix()) {
    return ToApiHandle<String>(name);
  } else {
    // We do not expect this to fail. Change this if it does.
    i::Handle<i::String> cons = isolate->factory()->NewConsString(
        isolate->factory()->InternalizeUtf8String(entry->name_prefix()),
        name).ToHandleChecked();
    return ToApiHandle<String>(cons);
  }
}


int CpuProfileNode::GetScriptId() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  const i::CodeEntry* entry = node->entry();
  return entry->script_id();
}


Handle<String> CpuProfileNode::GetScriptResourceName() const {
  i::Isolate* isolate = i::Isolate::Current();
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return ToApiHandle<String>(isolate->factory()->InternalizeUtf8String(
      node->entry()->resource_name()));
}


int CpuProfileNode::GetLineNumber() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->entry()->line_number();
}


int CpuProfileNode::GetColumnNumber() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->
      entry()->column_number();
}


unsigned int CpuProfileNode::GetHitLineCount() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->GetHitLineCount();
}


bool CpuProfileNode::GetLineTicks(LineTick* entries,
                                  unsigned int length) const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->GetLineTicks(entries, length);
}


const char* CpuProfileNode::GetBailoutReason() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->entry()->bailout_reason();
}


unsigned CpuProfileNode::GetHitCount() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->self_ticks();
}


unsigned CpuProfileNode::GetCallUid() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->function_id();
}


unsigned CpuProfileNode::GetNodeId() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->id();
}


int CpuProfileNode::GetChildrenCount() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->children()->length();
}


const CpuProfileNode* CpuProfileNode::GetChild(int index) const {
  const i::ProfileNode* child =
      reinterpret_cast<const i::ProfileNode*>(this)->children()->at(index);
  return reinterpret_cast<const CpuProfileNode*>(child);
}


void CpuProfile::Delete() {
  i::Isolate* isolate = i::Isolate::Current();
  i::CpuProfiler* profiler = isolate->cpu_profiler();
  DCHECK(profiler != NULL);
  profiler->DeleteProfile(reinterpret_cast<i::CpuProfile*>(this));
}


Handle<String> CpuProfile::GetTitle() const {
  i::Isolate* isolate = i::Isolate::Current();
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return ToApiHandle<String>(isolate->factory()->InternalizeUtf8String(
      profile->title()));
}


const CpuProfileNode* CpuProfile::GetTopDownRoot() const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return reinterpret_cast<const CpuProfileNode*>(profile->top_down()->root());
}


const CpuProfileNode* CpuProfile::GetSample(int index) const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return reinterpret_cast<const CpuProfileNode*>(profile->sample(index));
}


int64_t CpuProfile::GetSampleTimestamp(int index) const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return (profile->sample_timestamp(index) - base::TimeTicks())
      .InMicroseconds();
}


int64_t CpuProfile::GetStartTime() const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return (profile->start_time() - base::TimeTicks()).InMicroseconds();
}


int64_t CpuProfile::GetEndTime() const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return (profile->end_time() - base::TimeTicks()).InMicroseconds();
}


int CpuProfile::GetSamplesCount() const {
  return reinterpret_cast<const i::CpuProfile*>(this)->samples_count();
}


void CpuProfiler::SetSamplingInterval(int us) {
  DCHECK(us >= 0);
  return reinterpret_cast<i::CpuProfiler*>(this)->set_sampling_interval(
      base::TimeDelta::FromMicroseconds(us));
}


void CpuProfiler::StartProfiling(Handle<String> title, bool record_samples) {
  reinterpret_cast<i::CpuProfiler*>(this)->StartProfiling(
      *Utils::OpenHandle(*title), record_samples);
}


CpuProfile* CpuProfiler::StopProfiling(Handle<String> title) {
  return reinterpret_cast<CpuProfile*>(
      reinterpret_cast<i::CpuProfiler*>(this)->StopProfiling(
          *Utils::OpenHandle(*title)));
}


void CpuProfiler::SetIdle(bool is_idle) {
  i::Isolate* isolate = reinterpret_cast<i::CpuProfiler*>(this)->isolate();
  v8::StateTag state = isolate->current_vm_state();
  DCHECK(state == v8::EXTERNAL || state == v8::IDLE);
  if (isolate->js_entry_sp() != NULL) return;
  if (is_idle) {
    isolate->set_current_vm_state(v8::IDLE);
  } else if (state == v8::IDLE) {
    isolate->set_current_vm_state(v8::EXTERNAL);
  }
}


static i::HeapGraphEdge* ToInternal(const HeapGraphEdge* edge) {
  return const_cast<i::HeapGraphEdge*>(
      reinterpret_cast<const i::HeapGraphEdge*>(edge));
}


HeapGraphEdge::Type HeapGraphEdge::GetType() const {
  return static_cast<HeapGraphEdge::Type>(ToInternal(this)->type());
}


Handle<Value> HeapGraphEdge::GetName() const {
  i::Isolate* isolate = i::Isolate::Current();
  i::HeapGraphEdge* edge = ToInternal(this);
  switch (edge->type()) {
    case i::HeapGraphEdge::kContextVariable:
    case i::HeapGraphEdge::kInternal:
    case i::HeapGraphEdge::kProperty:
    case i::HeapGraphEdge::kShortcut:
    case i::HeapGraphEdge::kWeak:
      return ToApiHandle<String>(
          isolate->factory()->InternalizeUtf8String(edge->name()));
    case i::HeapGraphEdge::kElement:
    case i::HeapGraphEdge::kHidden:
      return ToApiHandle<Number>(
          isolate->factory()->NewNumberFromInt(edge->index()));
    default: UNREACHABLE();
  }
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(isolate));
}


const HeapGraphNode* HeapGraphEdge::GetFromNode() const {
  const i::HeapEntry* from = ToInternal(this)->from();
  return reinterpret_cast<const HeapGraphNode*>(from);
}


const HeapGraphNode* HeapGraphEdge::GetToNode() const {
  const i::HeapEntry* to = ToInternal(this)->to();
  return reinterpret_cast<const HeapGraphNode*>(to);
}


static i::HeapEntry* ToInternal(const HeapGraphNode* entry) {
  return const_cast<i::HeapEntry*>(
      reinterpret_cast<const i::HeapEntry*>(entry));
}


HeapGraphNode::Type HeapGraphNode::GetType() const {
  return static_cast<HeapGraphNode::Type>(ToInternal(this)->type());
}


Handle<String> HeapGraphNode::GetName() const {
  i::Isolate* isolate = i::Isolate::Current();
  return ToApiHandle<String>(
      isolate->factory()->InternalizeUtf8String(ToInternal(this)->name()));
}


SnapshotObjectId HeapGraphNode::GetId() const {
  return ToInternal(this)->id();
}


size_t HeapGraphNode::GetShallowSize() const {
  return ToInternal(this)->self_size();
}


int HeapGraphNode::GetChildrenCount() const {
  return ToInternal(this)->children().length();
}


const HeapGraphEdge* HeapGraphNode::GetChild(int index) const {
  return reinterpret_cast<const HeapGraphEdge*>(
      ToInternal(this)->children()[index]);
}


static i::HeapSnapshot* ToInternal(const HeapSnapshot* snapshot) {
  return const_cast<i::HeapSnapshot*>(
      reinterpret_cast<const i::HeapSnapshot*>(snapshot));
}


void HeapSnapshot::Delete() {
  i::Isolate* isolate = i::Isolate::Current();
  if (isolate->heap_profiler()->GetSnapshotsCount() > 1) {
    ToInternal(this)->Delete();
  } else {
    // If this is the last snapshot, clean up all accessory data as well.
    isolate->heap_profiler()->DeleteAllSnapshots();
  }
}


const HeapGraphNode* HeapSnapshot::GetRoot() const {
  return reinterpret_cast<const HeapGraphNode*>(ToInternal(this)->root());
}


const HeapGraphNode* HeapSnapshot::GetNodeById(SnapshotObjectId id) const {
  return reinterpret_cast<const HeapGraphNode*>(
      ToInternal(this)->GetEntryById(id));
}


int HeapSnapshot::GetNodesCount() const {
  return ToInternal(this)->entries().length();
}


const HeapGraphNode* HeapSnapshot::GetNode(int index) const {
  return reinterpret_cast<const HeapGraphNode*>(
      &ToInternal(this)->entries().at(index));
}


SnapshotObjectId HeapSnapshot::GetMaxSnapshotJSObjectId() const {
  return ToInternal(this)->max_snapshot_js_object_id();
}


void HeapSnapshot::Serialize(OutputStream* stream,
                             HeapSnapshot::SerializationFormat format) const {
  Utils::ApiCheck(format == kJSON,
                  "v8::HeapSnapshot::Serialize",
                  "Unknown serialization format");
  Utils::ApiCheck(stream->GetChunkSize() > 0,
                  "v8::HeapSnapshot::Serialize",
                  "Invalid stream chunk size");
  i::HeapSnapshotJSONSerializer serializer(ToInternal(this));
  serializer.Serialize(stream);
}


// static
STATIC_CONST_MEMBER_DEFINITION const SnapshotObjectId
    HeapProfiler::kUnknownObjectId;


int HeapProfiler::GetSnapshotCount() {
  return reinterpret_cast<i::HeapProfiler*>(this)->GetSnapshotsCount();
}


const HeapSnapshot* HeapProfiler::GetHeapSnapshot(int index) {
  return reinterpret_cast<const HeapSnapshot*>(
      reinterpret_cast<i::HeapProfiler*>(this)->GetSnapshot(index));
}


SnapshotObjectId HeapProfiler::GetObjectId(Handle<Value> value) {
  i::Handle<i::Object> obj = Utils::OpenHandle(*value);
  return reinterpret_cast<i::HeapProfiler*>(this)->GetSnapshotObjectId(obj);
}


Handle<Value> HeapProfiler::FindObjectById(SnapshotObjectId id) {
  i::Handle<i::Object> obj =
      reinterpret_cast<i::HeapProfiler*>(this)->FindHeapObjectById(id);
  if (obj.is_null()) return Local<Value>();
  return Utils::ToLocal(obj);
}


void HeapProfiler::ClearObjectIds() {
  reinterpret_cast<i::HeapProfiler*>(this)->ClearHeapObjectMap();
}


const HeapSnapshot* HeapProfiler::TakeHeapSnapshot(
    Handle<String> title,
    ActivityControl* control,
    ObjectNameResolver* resolver) {
  return TakeHeapSnapshot(control, resolver);
}


const HeapSnapshot* HeapProfiler::TakeHeapSnapshot(
    ActivityControl* control, ObjectNameResolver* resolver) {
  return reinterpret_cast<const HeapSnapshot*>(
      reinterpret_cast<i::HeapProfiler*>(this)
          ->TakeSnapshot(control, resolver));
}


void HeapProfiler::StartTrackingHeapObjects(bool track_allocations) {
  reinterpret_cast<i::HeapProfiler*>(this)->StartHeapObjectsTracking(
      track_allocations);
}


void HeapProfiler::StopTrackingHeapObjects() {
  reinterpret_cast<i::HeapProfiler*>(this)->StopHeapObjectsTracking();
}


SnapshotObjectId HeapProfiler::GetHeapStats(OutputStream* stream) {
  return reinterpret_cast<i::HeapProfiler*>(this)->PushHeapObjectsStats(stream);
}


void HeapProfiler::DeleteAllHeapSnapshots() {
  reinterpret_cast<i::HeapProfiler*>(this)->DeleteAllSnapshots();
}


void HeapProfiler::SetWrapperClassInfoProvider(uint16_t class_id,
                                               WrapperInfoCallback callback) {
  reinterpret_cast<i::HeapProfiler*>(this)->DefineWrapperClass(class_id,
                                                               callback);
}


size_t HeapProfiler::GetProfilerMemorySize() {
  return reinterpret_cast<i::HeapProfiler*>(this)->
      GetMemorySizeUsedByProfiler();
}


void HeapProfiler::SetRetainedObjectInfo(UniqueId id,
                                         RetainedObjectInfo* info) {
  reinterpret_cast<i::HeapProfiler*>(this)->SetRetainedObjectInfo(id, info);
}


v8::Testing::StressType internal::Testing::stress_type_ =
    v8::Testing::kStressTypeOpt;


void Testing::SetStressRunType(Testing::StressType type) {
  internal::Testing::set_stress_type(type);
}


int Testing::GetStressRuns() {
  if (internal::FLAG_stress_runs != 0) return internal::FLAG_stress_runs;
#ifdef DEBUG
  // In debug mode the code runs much slower so stressing will only make two
  // runs.
  return 2;
#else
  return 5;
#endif
}


static void SetFlagsFromString(const char* flags) {
  V8::SetFlagsFromString(flags, i::StrLength(flags));
}


void Testing::PrepareStressRun(int run) {
  static const char* kLazyOptimizations =
      "--prepare-always-opt "
      "--max-inlined-source-size=999999 "
      "--max-inlined-nodes=999999 "
      "--max-inlined-nodes-cumulative=999999 "
      "--noalways-opt";
  static const char* kForcedOptimizations = "--always-opt";

  // If deoptimization stressed turn on frequent deoptimization. If no value
  // is spefified through --deopt-every-n-times use a default default value.
  static const char* kDeoptEvery13Times = "--deopt-every-n-times=13";
  if (internal::Testing::stress_type() == Testing::kStressTypeDeopt &&
      internal::FLAG_deopt_every_n_times == 0) {
    SetFlagsFromString(kDeoptEvery13Times);
  }

#ifdef DEBUG
  // As stressing in debug mode only make two runs skip the deopt stressing
  // here.
  if (run == GetStressRuns() - 1) {
    SetFlagsFromString(kForcedOptimizations);
  } else {
    SetFlagsFromString(kLazyOptimizations);
  }
#else
  if (run == GetStressRuns() - 1) {
    SetFlagsFromString(kForcedOptimizations);
  } else if (run != GetStressRuns() - 2) {
    SetFlagsFromString(kLazyOptimizations);
  }
#endif
}


// TODO(svenpanne) Deprecate this.
void Testing::DeoptimizeAll() {
  i::Isolate* isolate = i::Isolate::Current();
  i::HandleScope scope(isolate);
  internal::Deoptimizer::DeoptimizeAll(isolate);
}


namespace internal {


void HandleScopeImplementer::FreeThreadResources() {
  Free();
}


char* HandleScopeImplementer::ArchiveThread(char* storage) {
  HandleScopeData* current = isolate_->handle_scope_data();
  handle_scope_data_ = *current;
  MemCopy(storage, this, sizeof(*this));

  ResetAfterArchive();
  current->Initialize();

  return storage + ArchiveSpacePerThread();
}


int HandleScopeImplementer::ArchiveSpacePerThread() {
  return sizeof(HandleScopeImplementer);
}


char* HandleScopeImplementer::RestoreThread(char* storage) {
  MemCopy(this, storage, sizeof(*this));
  *isolate_->handle_scope_data() = handle_scope_data_;
  return storage + ArchiveSpacePerThread();
}


void HandleScopeImplementer::IterateThis(ObjectVisitor* v) {
#ifdef DEBUG
  bool found_block_before_deferred = false;
#endif
  // Iterate over all handles in the blocks except for the last.
  for (int i = blocks()->length() - 2; i >= 0; --i) {
    Object** block = blocks()->at(i);
    if (last_handle_before_deferred_block_ != NULL &&
        (last_handle_before_deferred_block_ <= &block[kHandleBlockSize]) &&
        (last_handle_before_deferred_block_ >= block)) {
      v->VisitPointers(block, last_handle_before_deferred_block_);
      DCHECK(!found_block_before_deferred);
#ifdef DEBUG
      found_block_before_deferred = true;
#endif
    } else {
      v->VisitPointers(block, &block[kHandleBlockSize]);
    }
  }

  DCHECK(last_handle_before_deferred_block_ == NULL ||
         found_block_before_deferred);

  // Iterate over live handles in the last block (if any).
  if (!blocks()->is_empty()) {
    v->VisitPointers(blocks()->last(), handle_scope_data_.next);
  }

  List<Context*>* context_lists[2] = { &saved_contexts_, &entered_contexts_};
  for (unsigned i = 0; i < arraysize(context_lists); i++) {
    if (context_lists[i]->is_empty()) continue;
    Object** start = reinterpret_cast<Object**>(&context_lists[i]->first());
    v->VisitPointers(start, start + context_lists[i]->length());
  }
}


void HandleScopeImplementer::Iterate(ObjectVisitor* v) {
  HandleScopeData* current = isolate_->handle_scope_data();
  handle_scope_data_ = *current;
  IterateThis(v);
}


char* HandleScopeImplementer::Iterate(ObjectVisitor* v, char* storage) {
  HandleScopeImplementer* scope_implementer =
      reinterpret_cast<HandleScopeImplementer*>(storage);
  scope_implementer->IterateThis(v);
  return storage + ArchiveSpacePerThread();
}


DeferredHandles* HandleScopeImplementer::Detach(Object** prev_limit) {
  DeferredHandles* deferred =
      new DeferredHandles(isolate()->handle_scope_data()->next, isolate());

  while (!blocks_.is_empty()) {
    Object** block_start = blocks_.last();
    Object** block_limit = &block_start[kHandleBlockSize];
    // We should not need to check for SealHandleScope here. Assert this.
    DCHECK(prev_limit == block_limit ||
           !(block_start <= prev_limit && prev_limit <= block_limit));
    if (prev_limit == block_limit) break;
    deferred->blocks_.Add(blocks_.last());
    blocks_.RemoveLast();
  }

  // deferred->blocks_ now contains the blocks installed on the
  // HandleScope stack since BeginDeferredScope was called, but in
  // reverse order.

  DCHECK(prev_limit == NULL || !blocks_.is_empty());

  DCHECK(!blocks_.is_empty() && prev_limit != NULL);
  DCHECK(last_handle_before_deferred_block_ != NULL);
  last_handle_before_deferred_block_ = NULL;
  return deferred;
}


void HandleScopeImplementer::BeginDeferredScope() {
  DCHECK(last_handle_before_deferred_block_ == NULL);
  last_handle_before_deferred_block_ = isolate()->handle_scope_data()->next;
}


DeferredHandles::~DeferredHandles() {
  isolate_->UnlinkDeferredHandles(this);

  for (int i = 0; i < blocks_.length(); i++) {
#ifdef ENABLE_HANDLE_ZAPPING
    HandleScope::ZapRange(blocks_[i], &blocks_[i][kHandleBlockSize]);
#endif
    isolate_->handle_scope_implementer()->ReturnBlock(blocks_[i]);
  }
}


void DeferredHandles::Iterate(ObjectVisitor* v) {
  DCHECK(!blocks_.is_empty());

  DCHECK((first_block_limit_ >= blocks_.first()) &&
         (first_block_limit_ <= &(blocks_.first())[kHandleBlockSize]));

  v->VisitPointers(blocks_.first(), first_block_limit_);

  for (int i = 1; i < blocks_.length(); i++) {
    v->VisitPointers(blocks_[i], &blocks_[i][kHandleBlockSize]);
  }
}


void InvokeAccessorGetterCallback(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info,
    v8::AccessorNameGetterCallback getter) {
  // Leaving JavaScript.
  Isolate* isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
  Address getter_address = reinterpret_cast<Address>(reinterpret_cast<intptr_t>(
      getter));
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, getter_address);
  getter(property, info);
}


void InvokeFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>& info,
                            v8::FunctionCallback callback) {
  Isolate* isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
  Address callback_address =
      reinterpret_cast<Address>(reinterpret_cast<intptr_t>(callback));
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, callback_address);
  callback(info);
}


} }  // namespace v8::internal
