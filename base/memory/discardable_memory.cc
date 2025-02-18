// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/discardable_memory.h"

#include "base/memory/discardable_memory_shmem.h"

namespace base {

// static
scoped_ptr<DiscardableMemory> DiscardableMemory::CreateLockedMemory(
    size_t size) {
  return make_scoped_ptr(new internal::DiscardableMemoryShmem(size));
}

}  // namespace base
