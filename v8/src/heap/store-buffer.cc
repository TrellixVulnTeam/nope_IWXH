// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "src/v8.h"

#include "src/base/atomicops.h"
#include "src/counters.h"
#include "src/heap/store-buffer-inl.h"

namespace v8 {
namespace internal {

StoreBuffer::StoreBuffer(Heap* heap)
    : heap_(heap),
      start_(NULL),
      limit_(NULL),
      old_start_(NULL),
      old_limit_(NULL),
      old_top_(NULL),
      old_reserved_limit_(NULL),
      old_buffer_is_sorted_(false),
      old_buffer_is_filtered_(false),
      during_gc_(false),
      store_buffer_rebuilding_enabled_(false),
      callback_(NULL),
      may_move_store_buffer_entries_(true),
      virtual_memory_(NULL),
      hash_set_1_(NULL),
      hash_set_2_(NULL),
      hash_sets_are_empty_(true) {}


void StoreBuffer::SetUp() {
  virtual_memory_ = new base::VirtualMemory(kStoreBufferSize * 3);
  uintptr_t start_as_int =
      reinterpret_cast<uintptr_t>(virtual_memory_->address());
  start_ =
      reinterpret_cast<Address*>(RoundUp(start_as_int, kStoreBufferSize * 2));
  limit_ = start_ + (kStoreBufferSize / kPointerSize);

  old_virtual_memory_ =
      new base::VirtualMemory(kOldStoreBufferLength * kPointerSize);
  old_top_ = old_start_ =
      reinterpret_cast<Address*>(old_virtual_memory_->address());
  // Don't know the alignment requirements of the OS, but it is certainly not
  // less than 0xfff.
  DCHECK((reinterpret_cast<uintptr_t>(old_start_) & 0xfff) == 0);
  int initial_length =
      static_cast<int>(base::OS::CommitPageSize() / kPointerSize);
  DCHECK(initial_length > 0);
  DCHECK(initial_length <= kOldStoreBufferLength);
  old_limit_ = old_start_ + initial_length;
  old_reserved_limit_ = old_start_ + kOldStoreBufferLength;

  if (!old_virtual_memory_->Commit(reinterpret_cast<void*>(old_start_),
                                   (old_limit_ - old_start_) * kPointerSize,
                                   false)) {
    V8::FatalProcessOutOfMemory("StoreBuffer::SetUp");
  }

  DCHECK(reinterpret_cast<Address>(start_) >= virtual_memory_->address());
  DCHECK(reinterpret_cast<Address>(limit_) >= virtual_memory_->address());
  Address* vm_limit = reinterpret_cast<Address*>(
      reinterpret_cast<char*>(virtual_memory_->address()) +
      virtual_memory_->size());
  DCHECK(start_ <= vm_limit);
  DCHECK(limit_ <= vm_limit);
  USE(vm_limit);
  DCHECK((reinterpret_cast<uintptr_t>(limit_) & kStoreBufferOverflowBit) != 0);
  DCHECK((reinterpret_cast<uintptr_t>(limit_ - 1) & kStoreBufferOverflowBit) ==
         0);

  if (!virtual_memory_->Commit(reinterpret_cast<Address>(start_),
                               kStoreBufferSize,
                               false)) {  // Not executable.
    V8::FatalProcessOutOfMemory("StoreBuffer::SetUp");
  }
  heap_->public_set_store_buffer_top(start_);

  hash_set_1_ = new uintptr_t[kHashSetLength];
  hash_set_2_ = new uintptr_t[kHashSetLength];
  hash_sets_are_empty_ = false;

  ClearFilteringHashSets();

  heap_->isolate()->set_store_buffer_hash_set_1_address(hash_set_1_);
  heap_->isolate()->set_store_buffer_hash_set_2_address(hash_set_2_);
}


void StoreBuffer::TearDown() {
  delete virtual_memory_;
  delete old_virtual_memory_;
  delete[] hash_set_1_;
  delete[] hash_set_2_;
  old_start_ = old_top_ = old_limit_ = old_reserved_limit_ = NULL;
  start_ = limit_ = NULL;
  heap_->public_set_store_buffer_top(start_);
}


void StoreBuffer::StoreBufferOverflow(Isolate* isolate) {
  isolate->heap()->store_buffer()->Compact();
  isolate->counters()->store_buffer_overflows()->Increment();
}


void StoreBuffer::Uniq() {
  // Remove adjacent duplicates and cells that do not point at new space.
  Address previous = NULL;
  Address* write = old_start_;
  DCHECK(may_move_store_buffer_entries_);
  for (Address* read = old_start_; read < old_top_; read++) {
    Address current = *read;
    if (current != previous) {
      Object* object = reinterpret_cast<Object*>(
          base::NoBarrier_Load(reinterpret_cast<base::AtomicWord*>(current)));
      if (heap_->InNewSpace(object)) {
        *write++ = current;
      }
    }
    previous = current;
  }
  old_top_ = write;
}


bool StoreBuffer::SpaceAvailable(intptr_t space_needed) {
  return old_limit_ - old_top_ >= space_needed;
}


void StoreBuffer::EnsureSpace(intptr_t space_needed) {
  while (old_limit_ - old_top_ < space_needed &&
         old_limit_ < old_reserved_limit_) {
    size_t grow = old_limit_ - old_start_;  // Double size.
    if (!old_virtual_memory_->Commit(reinterpret_cast<void*>(old_limit_),
                                     grow * kPointerSize, false)) {
      V8::FatalProcessOutOfMemory("StoreBuffer::EnsureSpace");
    }
    old_limit_ += grow;
  }

  if (SpaceAvailable(space_needed)) return;

  if (old_buffer_is_filtered_) return;
  DCHECK(may_move_store_buffer_entries_);
  Compact();

  old_buffer_is_filtered_ = true;
  bool page_has_scan_on_scavenge_flag = false;

  PointerChunkIterator it(heap_);
  MemoryChunk* chunk;
  while ((chunk = it.next()) != NULL) {
    if (chunk->scan_on_scavenge()) {
      page_has_scan_on_scavenge_flag = true;
      break;
    }
  }

  if (page_has_scan_on_scavenge_flag) {
    Filter(MemoryChunk::SCAN_ON_SCAVENGE);
  }

  if (SpaceAvailable(space_needed)) return;

  // Sample 1 entry in 97 and filter out the pages where we estimate that more
  // than 1 in 8 pointers are to new space.
  static const int kSampleFinenesses = 5;
  static const struct Samples {
    int prime_sample_step;
    int threshold;
  } samples[kSampleFinenesses] = {
        {97, ((Page::kPageSize / kPointerSize) / 97) / 8},
        {23, ((Page::kPageSize / kPointerSize) / 23) / 16},
        {7, ((Page::kPageSize / kPointerSize) / 7) / 32},
        {3, ((Page::kPageSize / kPointerSize) / 3) / 256},
        {1, 0}};
  for (int i = 0; i < kSampleFinenesses; i++) {
    ExemptPopularPages(samples[i].prime_sample_step, samples[i].threshold);
    // As a last resort we mark all pages as being exempt from the store buffer.
    DCHECK(i != (kSampleFinenesses - 1) || old_top_ == old_start_);
    if (SpaceAvailable(space_needed)) return;
  }
  UNREACHABLE();
}


// Sample the store buffer to see if some pages are taking up a lot of space
// in the store buffer.
void StoreBuffer::ExemptPopularPages(int prime_sample_step, int threshold) {
  PointerChunkIterator it(heap_);
  MemoryChunk* chunk;
  while ((chunk = it.next()) != NULL) {
    chunk->set_store_buffer_counter(0);
  }
  bool created_new_scan_on_scavenge_pages = false;
  MemoryChunk* previous_chunk = NULL;
  for (Address* p = old_start_; p < old_top_; p += prime_sample_step) {
    Address addr = *p;
    MemoryChunk* containing_chunk = NULL;
    if (previous_chunk != NULL && previous_chunk->Contains(addr)) {
      containing_chunk = previous_chunk;
    } else {
      containing_chunk = MemoryChunk::FromAnyPointerAddress(heap_, addr);
    }
    int old_counter = containing_chunk->store_buffer_counter();
    if (old_counter >= threshold) {
      containing_chunk->set_scan_on_scavenge(true);
      created_new_scan_on_scavenge_pages = true;
    }
    containing_chunk->set_store_buffer_counter(old_counter + 1);
    previous_chunk = containing_chunk;
  }
  if (created_new_scan_on_scavenge_pages) {
    Filter(MemoryChunk::SCAN_ON_SCAVENGE);
  }
  old_buffer_is_filtered_ = true;
}


void StoreBuffer::Filter(int flag) {
  Address* new_top = old_start_;
  MemoryChunk* previous_chunk = NULL;
  for (Address* p = old_start_; p < old_top_; p++) {
    Address addr = *p;
    MemoryChunk* containing_chunk = NULL;
    if (previous_chunk != NULL && previous_chunk->Contains(addr)) {
      containing_chunk = previous_chunk;
    } else {
      containing_chunk = MemoryChunk::FromAnyPointerAddress(heap_, addr);
      previous_chunk = containing_chunk;
    }
    if (!containing_chunk->IsFlagSet(flag)) {
      *new_top++ = addr;
    }
  }
  old_top_ = new_top;

  // Filtering hash sets are inconsistent with the store buffer after this
  // operation.
  ClearFilteringHashSets();
}


void StoreBuffer::RemoveSlots(Address start_address, Address end_address) {
  struct IsValueInRangePredicate {
    Address start_address_;
    Address end_address_;

    IsValueInRangePredicate(Address start_address, Address end_address)
        : start_address_(start_address), end_address_(end_address) {}

    bool operator()(Address addr) {
      return start_address_ <= addr && addr < end_address_;
    }
  };

  IsValueInRangePredicate predicate(start_address, end_address);
  // Some address in old space that does not move.
  const Address kRemovedSlot = heap_->undefined_value()->address();
  DCHECK(Page::FromAddress(kRemovedSlot)->NeverEvacuate());

  {
    Address* top = reinterpret_cast<Address*>(heap_->store_buffer_top());
    std::replace_if(start_, top, predicate, kRemovedSlot);
  }

  if (old_buffer_is_sorted_) {
    // Remove slots from an old buffer preserving the order.
    Address* lower = std::lower_bound(old_start_, old_top_, start_address);
    if (lower != old_top_) {
      // [lower, old_top_) range contain elements that are >= |start_address|.
      Address* upper = std::lower_bound(lower, old_top_, end_address);
      // Remove [lower, upper) from the buffer.
      if (upper == old_top_) {
        // All elements in [lower, old_top_) range are < |end_address|.
        old_top_ = lower;
      } else if (lower != upper) {
        // [upper, old_top_) range contain elements that are >= |end_address|,
        // move [upper, old_top_) range to [lower, ...) and update old_top_.
        Address* new_top = lower;
        for (Address* p = upper; p < old_top_; p++) {
          *new_top++ = *p;
        }
        old_top_ = new_top;
      }
    }
  } else {
    std::replace_if(old_start_, old_top_, predicate, kRemovedSlot);
  }
}


void StoreBuffer::SortUniq() {
  Compact();
  if (old_buffer_is_sorted_) return;
  std::sort(old_start_, old_top_);
  Uniq();

  old_buffer_is_sorted_ = true;

  // Filtering hash sets are inconsistent with the store buffer after this
  // operation.
  ClearFilteringHashSets();
}


bool StoreBuffer::PrepareForIteration() {
  Compact();
  PointerChunkIterator it(heap_);
  MemoryChunk* chunk;
  bool page_has_scan_on_scavenge_flag = false;
  while ((chunk = it.next()) != NULL) {
    if (chunk->scan_on_scavenge()) {
      page_has_scan_on_scavenge_flag = true;
      break;
    }
  }

  if (page_has_scan_on_scavenge_flag) {
    Filter(MemoryChunk::SCAN_ON_SCAVENGE);
  }

  // Filtering hash sets are inconsistent with the store buffer after
  // iteration.
  ClearFilteringHashSets();

  return page_has_scan_on_scavenge_flag;
}


#ifdef DEBUG
void StoreBuffer::Clean() {
  ClearFilteringHashSets();
  Uniq();  // Also removes things that no longer point to new space.
  EnsureSpace(kStoreBufferSize / 2);
}


static Address* in_store_buffer_1_element_cache = NULL;


bool StoreBuffer::CellIsInStoreBuffer(Address cell_address) {
  DCHECK_NOT_NULL(cell_address);
  Address* top = reinterpret_cast<Address*>(heap_->store_buffer_top());
  if (in_store_buffer_1_element_cache != NULL &&
      *in_store_buffer_1_element_cache == cell_address) {
    // Check if the cache still points into the active part of the buffer.
    if ((start_ <= in_store_buffer_1_element_cache &&
         in_store_buffer_1_element_cache < top) ||
        (old_start_ <= in_store_buffer_1_element_cache &&
         in_store_buffer_1_element_cache < old_top_)) {
      return true;
    }
  }
  for (Address* current = top - 1; current >= start_; current--) {
    if (*current == cell_address) {
      in_store_buffer_1_element_cache = current;
      return true;
    }
  }
  for (Address* current = old_top_ - 1; current >= old_start_; current--) {
    if (*current == cell_address) {
      in_store_buffer_1_element_cache = current;
      return true;
    }
  }
  return false;
}
#endif


void StoreBuffer::ClearFilteringHashSets() {
  if (!hash_sets_are_empty_) {
    memset(reinterpret_cast<void*>(hash_set_1_), 0,
           sizeof(uintptr_t) * kHashSetLength);
    memset(reinterpret_cast<void*>(hash_set_2_), 0,
           sizeof(uintptr_t) * kHashSetLength);
    hash_sets_are_empty_ = true;
  }
}


void StoreBuffer::GCPrologue() {
  ClearFilteringHashSets();
  during_gc_ = true;
}


#ifdef VERIFY_HEAP
void StoreBuffer::VerifyPointers(LargeObjectSpace* space) {
  LargeObjectIterator it(space);
  for (HeapObject* object = it.Next(); object != NULL; object = it.Next()) {
    if (object->IsFixedArray()) {
      Address slot_address = object->address();
      Address end = object->address() + object->Size();

      while (slot_address < end) {
        HeapObject** slot = reinterpret_cast<HeapObject**>(slot_address);
        // When we are not in GC the Heap::InNewSpace() predicate
        // checks that pointers which satisfy predicate point into
        // the active semispace.
        Object* object = *slot;
        heap_->InNewSpace(object);
        slot_address += kPointerSize;
      }
    }
  }
}
#endif


void StoreBuffer::Verify() {
#ifdef VERIFY_HEAP
  VerifyPointers(heap_->lo_space());
#endif
}


void StoreBuffer::GCEpilogue() {
  during_gc_ = false;
#ifdef VERIFY_HEAP
  if (FLAG_verify_heap) {
    Verify();
  }
#endif
}


void StoreBuffer::ProcessOldToNewSlot(Address slot_address,
                                      ObjectSlotCallback slot_callback) {
  Object** slot = reinterpret_cast<Object**>(slot_address);
  Object* object = *slot;

  // If the object is not in from space, it must be a duplicate store buffer
  // entry and the slot was already updated.
  if (heap_->InFromSpace(object)) {
    HeapObject* heap_object = reinterpret_cast<HeapObject*>(object);
    DCHECK(heap_object->IsHeapObject());
    slot_callback(reinterpret_cast<HeapObject**>(slot), heap_object);
    object = *slot;
    // If the object was in from space before and is after executing the
    // callback in to space, the object is still live.
    // Unfortunately, we do not know about the slot. It could be in a
    // just freed free space object.
    if (heap_->InToSpace(object)) {
      EnterDirectlyIntoStoreBuffer(reinterpret_cast<Address>(slot));
    }
  }
}


void StoreBuffer::FindPointersToNewSpaceInRegion(
    Address start, Address end, ObjectSlotCallback slot_callback) {
  for (Address slot_address = start; slot_address < end;
       slot_address += kPointerSize) {
    ProcessOldToNewSlot(slot_address, slot_callback);
  }
}


void StoreBuffer::IteratePointersInStoreBuffer(
    ObjectSlotCallback slot_callback) {
  Address* limit = old_top_;
  old_top_ = old_start_;
  {
    DontMoveStoreBufferEntriesScope scope(this);
    for (Address* current = old_start_; current < limit; current++) {
#ifdef DEBUG
      Address* saved_top = old_top_;
#endif
      ProcessOldToNewSlot(*current, slot_callback);
      DCHECK(old_top_ == saved_top + 1 || old_top_ == saved_top);
    }
  }
}


void StoreBuffer::ClearInvalidStoreBufferEntries() {
  Compact();
  Address* new_top = old_start_;
  for (Address* current = old_start_; current < old_top_; current++) {
    Address addr = *current;
    Object** slot = reinterpret_cast<Object**>(*current);
    // Use a NoBarrier_Load here since the slot can be in a dead object
    // which may be touched by the concurrent sweeper thread.
    Object* object = reinterpret_cast<Object*>(
        base::NoBarrier_Load(reinterpret_cast<base::AtomicWord*>(slot)));
    if (heap_->InNewSpace(object)) {
      if (heap_->mark_compact_collector()->IsSlotInLiveObject(
              reinterpret_cast<HeapObject**>(slot),
              reinterpret_cast<HeapObject*>(object))) {
        *new_top++ = addr;
      }
    }
  }
  old_top_ = new_top;
  ClearFilteringHashSets();

  // Don't scan on scavenge dead large objects.
  LargeObjectIterator it(heap_->lo_space());
  for (HeapObject* object = it.Next(); object != NULL; object = it.Next()) {
    MemoryChunk* chunk = MemoryChunk::FromAddress(object->address());
    if (chunk->scan_on_scavenge() && !Marking::MarkBitFrom(object).Get()) {
      chunk->set_scan_on_scavenge(false);
    }
  }
}


void StoreBuffer::VerifyValidStoreBufferEntries() {
  for (Address* current = old_start_; current < old_top_; current++) {
    Object** slot = reinterpret_cast<Object**>(*current);
    Object* object = *slot;
    CHECK(heap_->InNewSpace(object));
    heap_->mark_compact_collector()->VerifyIsSlotInLiveObject(
        reinterpret_cast<HeapObject**>(slot),
        reinterpret_cast<HeapObject*>(object));
  }
}


void StoreBuffer::IteratePointersToNewSpace(ObjectSlotCallback slot_callback) {
  // We do not sort or remove duplicated entries from the store buffer because
  // we expect that callback will rebuild the store buffer thus removing
  // all duplicates and pointers to old space.
  bool some_pages_to_scan = PrepareForIteration();

  // TODO(gc): we want to skip slots on evacuation candidates
  // but we can't simply figure that out from slot address
  // because slot can belong to a large object.
  IteratePointersInStoreBuffer(slot_callback);

  // We are done scanning all the pointers that were in the store buffer, but
  // there may be some pages marked scan_on_scavenge that have pointers to new
  // space that are not in the store buffer.  We must scan them now.  As we
  // scan, the surviving pointers to new space will be added to the store
  // buffer.  If there are still a lot of pointers to new space then we will
  // keep the scan_on_scavenge flag on the page and discard the pointers that
  // were added to the store buffer.  If there are not many pointers to new
  // space left on the page we will keep the pointers in the store buffer and
  // remove the flag from the page.
  if (some_pages_to_scan) {
    if (callback_ != NULL) {
      (*callback_)(heap_, NULL, kStoreBufferStartScanningPagesEvent);
    }
    PointerChunkIterator it(heap_);
    MemoryChunk* chunk;
    while ((chunk = it.next()) != NULL) {
      if (chunk->scan_on_scavenge()) {
        chunk->set_scan_on_scavenge(false);
        if (callback_ != NULL) {
          (*callback_)(heap_, chunk, kStoreBufferScanningPageEvent);
        }
        if (chunk->owner() == heap_->lo_space()) {
          LargePage* large_page = reinterpret_cast<LargePage*>(chunk);
          HeapObject* array = large_page->GetObject();
          DCHECK(array->IsFixedArray());
          Address start = array->address();
          Address end = start + array->Size();
          FindPointersToNewSpaceInRegion(start, end, slot_callback);
        } else {
          Page* page = reinterpret_cast<Page*>(chunk);
          PagedSpace* owner = reinterpret_cast<PagedSpace*>(page->owner());
          if (owner == heap_->map_space()) {
            DCHECK(page->WasSwept());
            HeapObjectIterator iterator(page, NULL);
            for (HeapObject* heap_object = iterator.Next(); heap_object != NULL;
                 heap_object = iterator.Next()) {
              // We skip free space objects.
              if (!heap_object->IsFiller()) {
                DCHECK(heap_object->IsMap());
                FindPointersToNewSpaceInRegion(
                    heap_object->address() + Map::kPointerFieldsBeginOffset,
                    heap_object->address() + Map::kPointerFieldsEndOffset,
                    slot_callback);
              }
            }
          } else {
            if (!page->SweepingCompleted()) {
              heap_->mark_compact_collector()->SweepInParallel(page, owner);
              if (!page->SweepingCompleted()) {
                // We were not able to sweep that page, i.e., a concurrent
                // sweeper thread currently owns this page.
                // TODO(hpayer): This may introduce a huge pause here. We
                // just care about finish sweeping of the scan on scavenge page.
                heap_->mark_compact_collector()->EnsureSweepingCompleted();
              }
            }
            CHECK(page->owner() == heap_->old_pointer_space());
            HeapObjectIterator iterator(page, NULL);
            for (HeapObject* heap_object = iterator.Next(); heap_object != NULL;
                 heap_object = iterator.Next()) {
              // We iterate over objects that contain new space pointers only.
              bool may_contain_raw_values = heap_object->MayContainRawValues();
              if (!may_contain_raw_values) {
                Address obj_address = heap_object->address();
                const int start_offset = HeapObject::kHeaderSize;
                const int end_offset = heap_object->Size();
#if V8_DOUBLE_FIELDS_UNBOXING
                LayoutDescriptorHelper helper(heap_object->map());
                bool has_only_tagged_fields = helper.all_fields_tagged();

                if (!has_only_tagged_fields) {
                  for (int offset = start_offset; offset < end_offset;) {
                    int end_of_region_offset;
                    if (helper.IsTagged(offset, end_offset,
                                        &end_of_region_offset)) {
                      FindPointersToNewSpaceInRegion(
                          obj_address + offset,
                          obj_address + end_of_region_offset, slot_callback);
                    }
                    offset = end_of_region_offset;
                  }
                } else {
#endif
                  Address start_address = obj_address + start_offset;
                  Address end_address = obj_address + end_offset;
                  // Object has only tagged fields.
                  FindPointersToNewSpaceInRegion(start_address, end_address,
                                                 slot_callback);
#if V8_DOUBLE_FIELDS_UNBOXING
                }
#endif
              }
            }
          }
        }
      }
    }
    if (callback_ != NULL) {
      (*callback_)(heap_, NULL, kStoreBufferScanningPageEvent);
    }
  }
}


void StoreBuffer::Compact() {
  CHECK(hash_set_1_ == heap_->isolate()->store_buffer_hash_set_1_address());
  CHECK(hash_set_2_ == heap_->isolate()->store_buffer_hash_set_2_address());

  Address* top = reinterpret_cast<Address*>(heap_->store_buffer_top());

  if (top == start_) return;

  // There's no check of the limit in the loop below so we check here for
  // the worst case (compaction doesn't eliminate any pointers).
  DCHECK(top <= limit_);
  heap_->public_set_store_buffer_top(start_);
  EnsureSpace(top - start_);
  DCHECK(may_move_store_buffer_entries_);
  // Goes through the addresses in the store buffer attempting to remove
  // duplicates.  In the interest of speed this is a lossy operation.  Some
  // duplicates will remain.  We have two hash sets with different hash
  // functions to reduce the number of unnecessary clashes.
  hash_sets_are_empty_ = false;  // Hash sets are in use.
  for (Address* current = start_; current < top; current++) {
    DCHECK(!heap_->cell_space()->Contains(*current));
    DCHECK(!heap_->code_space()->Contains(*current));
    DCHECK(!heap_->old_data_space()->Contains(*current));
    uintptr_t int_addr = reinterpret_cast<uintptr_t>(*current);
    // Shift out the last bits including any tags.
    int_addr >>= kPointerSizeLog2;
    // The upper part of an address is basically random because of ASLR and OS
    // non-determinism, so we use only the bits within a page for hashing to
    // make v8's behavior (more) deterministic.
    uintptr_t hash_addr =
        int_addr & (Page::kPageAlignmentMask >> kPointerSizeLog2);
    int hash1 = ((hash_addr ^ (hash_addr >> kHashSetLengthLog2)) &
                 (kHashSetLength - 1));
    if (hash_set_1_[hash1] == int_addr) continue;
    uintptr_t hash2 = (hash_addr - (hash_addr >> kHashSetLengthLog2));
    hash2 ^= hash2 >> (kHashSetLengthLog2 * 2);
    hash2 &= (kHashSetLength - 1);
    if (hash_set_2_[hash2] == int_addr) continue;
    if (hash_set_1_[hash1] == 0) {
      hash_set_1_[hash1] = int_addr;
    } else if (hash_set_2_[hash2] == 0) {
      hash_set_2_[hash2] = int_addr;
    } else {
      // Rather than slowing down we just throw away some entries.  This will
      // cause some duplicates to remain undetected.
      hash_set_1_[hash1] = int_addr;
      hash_set_2_[hash2] = 0;
    }
    old_buffer_is_sorted_ = false;
    old_buffer_is_filtered_ = false;
    *old_top_++ = reinterpret_cast<Address>(int_addr << kPointerSizeLog2);
    DCHECK(old_top_ <= old_limit_);
  }
  heap_->isolate()->counters()->store_buffer_compactions()->Increment();
}
}
}  // namespace v8::internal
