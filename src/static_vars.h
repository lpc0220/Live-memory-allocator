// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Ken Ashcraft <opensource@google.com>
//
// Static variables shared by multiple classes.

#ifndef TCMALLOC_STATIC_VARS_H_
#define TCMALLOC_STATIC_VARS_H_

#include <config.h>
#include "base/spinlock.h"
#include "central_freelist.h"
#include "common.h"
#include "page_heap.h"
#include "page_heap_allocator.h"
#include "span.h"

namespace tcmalloc {

class Static {
 public:
  // Linker initialized, so this lock can be accessed at any time.
  static SpinLock* pageheap_lock() { return &pageheap_lock_; }

  // Must be called before calling any of the accessors below.
  static void InitStaticVars();

  // Central cache -- an array of free-lists, one per size-class.
  // We have a separate lock per free-list to reduce contention.
  static CentralFreeListPadded* central_cache() { return central_cache_; }

  static SizeMap* sizemap() { return &sizemap_; }

  //////////////////////////////////////////////////////////////////////
  // In addition to the explicit initialization comment, the variables below
  // must be protected by pageheap_lock.

  // Page-level allocator.
  static PageHeap* pageheap() { return pageheap_; }

  static PageHeapAllocator<Span>* span_allocator() { return &span_allocator_; }

  // Check if InitStaticVars() has been run.
  static bool IsInited() { return pageheap() != NULL; }

 private:
  static SpinLock pageheap_lock_;

  // These static variables require explicit initialization.  We cannot
  // count on their constructors to do any initialization because other
  // static variables may try to allocate memory before these variables
  // can run their constructors.

  static SizeMap sizemap_;
  static CentralFreeListPadded central_cache_[kNumClasses];
  static PageHeapAllocator<Span> span_allocator_;
  static Span sampled_objects_;

  static PageHeap* pageheap_;
};

}  // namespace tcmalloc

#endif  // TCMALLOC_STATIC_VARS_H_
