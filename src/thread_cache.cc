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

#include <config.h>
#include "thread_cache.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>                     // for memcpy
#include <algorithm>                    // for max, min
#include "base/commandlineflags.h"      // for SpinLockHolder
#include "base/spinlock.h"              // for SpinLockHolder
#include "getenv_safe.h"                // for TCMallocGetenvSafe
#include "central_freelist.h"           // for CentralFreeListPadded
#include "maybe_threads.h"

// add by lpc
//#include "globals.h"
#include <iostream>
#include "lmmmap.h"
// end lpc

using std::min;
using std::max;

// Note: this is initialized manually in InitModule to ensure that
// it's configured at right time
//
// DEFINE_int64(tcmalloc_max_total_thread_cache_bytes,
//              EnvToInt64("TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES",
//                         kDefaultOverallThreadCacheSize),
//              "Bound on the total amount of bytes allocated to "
//              "thread caches. This bound is not strict, so it is possible "
//              "for the cache to go over this bound in certain circumstances. "
//              "Maximum value of this flag is capped to 1 GB.");

// add by lpc
#define LOCK_PREFIX     "lock ; "
inline unsigned long long atmc_fetch_and_add(volatile unsigned long long *address, unsigned long long value)
{
        int prev = value;
        asm volatile(
                LOCK_PREFIX "xaddq %0, %1"
                : "+r" (value), "+m" (*address)
                : : "memory");

        return prev + value;
}
extern unsigned long long mem_gap;
extern unsigned long long total_syncs;
extern unsigned long long total_fetch_syncs;
extern unsigned long long total_return_syncs;
extern unsigned long long total_memory_cost;
// end lpc

namespace tcmalloc {

static bool phinited = false;

volatile size_t ThreadCache::per_thread_cache_size_ = kMaxThreadCacheSize; /* 4M */
size_t ThreadCache::overall_thread_cache_size_ = kDefaultOverallThreadCacheSize; /* 32M or small_slow 8M */
ssize_t ThreadCache::unclaimed_cache_space_ = kDefaultOverallThreadCacheSize; /* 32M */
PageHeapAllocator<ThreadCache> threadcache_allocator;
ThreadCache* ThreadCache::thread_heaps_ = NULL;
int ThreadCache::thread_heap_count_ = 0;
ThreadCache* ThreadCache::next_memory_steal_ = NULL;
#ifdef HAVE_TLS
__thread ThreadCache::ThreadLocalData ThreadCache::threadlocal_data_
    ATTR_INITIAL_EXEC
    = {0, 0};
#endif
bool ThreadCache::tsd_inited_ = false;
pthread_key_t ThreadCache::heap_key_;

#if defined(HAVE_TLS)
bool kernel_supports_tls = false;      // be conservative
# if defined(_WIN32)    // windows has supported TLS since winnt, I think.
    void CheckIfKernelSupportsTLS() {
      kernel_supports_tls = true;
    }
# elif !HAVE_DECL_UNAME    // if too old for uname, probably too old for TLS
    void CheckIfKernelSupportsTLS() {
      kernel_supports_tls = false;
    }
# else
#   include <sys/utsname.h>    // DECL_UNAME checked for <sys/utsname.h> too
    void CheckIfKernelSupportsTLS() {
      struct utsname buf;
      if (uname(&buf) < 0) {   // should be impossible
        Log(kLog, __FILE__, __LINE__,
            "uname failed assuming no TLS support (errno)", errno);
        kernel_supports_tls = false;
      } else if (strcasecmp(buf.sysname, "linux") == 0) {
        // The linux case: the first kernel to support TLS was 2.6.0
        if (buf.release[0] < '2' && buf.release[1] == '.')    // 0.x or 1.x
          kernel_supports_tls = false;
        else if (buf.release[0] == '2' && buf.release[1] == '.' &&
                 buf.release[2] >= '0' && buf.release[2] < '6' &&
                 buf.release[3] == '.')                       // 2.0 - 2.5
          kernel_supports_tls = false;
        else
          kernel_supports_tls = true;
      } else if (strcasecmp(buf.sysname, "CYGWIN_NT-6.1-WOW64") == 0) {
        // In my testing, this version of cygwin, at least, would hang
        // when using TLS.
        kernel_supports_tls = false;
      } else {        // some other kernel, we'll be optimisitic
        kernel_supports_tls = true;
      }
      // TODO(csilvers): VLOG(1) the tls status once we support RAW_VLOG
    }
#  endif  // HAVE_DECL_UNAME
#endif    // HAVE_TLS

void ThreadCache::Init(pthread_t tid) {
//  printf("calling Init, thread id : %u\n", tid_);
//  fprintf(stderr, "2calling Init, thread id : %u\n", tid_);
//  fflush(stdout);
//  fflush(stderr);
  size_ = 0;

  max_size_ = 0;
  // where is the lock held?
  IncreaseCacheLimitLocked(); //
  if (max_size_ == 0) {
    // There isn't enough memory to go around.  Just give the minimum to
    // this thread.
    max_size_ = kMinThreadCacheSize; /* 512K bytes */

    // Take unclaimed_cache_space_ negative.
    unclaimed_cache_space_ -= kMinThreadCacheSize;
    ASSERT(unclaimed_cache_space_ < 0);
  }

  next_ = NULL;
  prev_ = NULL;
  tid_  = tid;
  in_setspecific_ = false;
  
  // add by lpc
  const char* env = NULL;
  env = getenv("APF_PARAMETER");
  if ( env != NULL ) {
    preset_apf = atoi(env);
  }
  else {
    preset_apf = DEFAULT_APF;
  }
  // end lpc

  apf_stat = 0;
  memory_cost = 0;
  bursty_period_len = TRACE_LENGTH;
  hibernate_period_len = HIBERNATE_LENGTH;
  X_1 = (long long *) lmmmap ( sizeof(long long) * (TRACE_LENGTH+1) );
  X_2 = (long long *) lmmmap ( sizeof(long long) * (TRACE_LENGTH+1) );
  Y_1 = (long long *) lmmmap ( sizeof(long long) * (TRACE_LENGTH+1) );
  Y_2 = (long long *) lmmmap ( sizeof(long long) * (TRACE_LENGTH+1) );
  Z_1 = (long long *) lmmmap ( sizeof(long long) * (TRACE_LENGTH+1) );
  Z_2 = (long long *) lmmmap ( sizeof(long long) * (TRACE_LENGTH+1) );
  for (size_t cl = 0; cl < kNumClasses; ++cl) {
    list_[cl].Init();

    // add by lpc
#if defined(APF_STATS)
    total_gap[cl] = 0;
    current_deaths[cl] = 0;
    total_synchronized[cl] = 0;
    return_syncs[cl] = 0;
#endif 
    fetched_syncs[cl] = 0;

    time_cnt[cl] = 0;
    last_reserved[cl] = 0;

    reuse_cnt[cl] = 0;
    cb_next[cl] = 0;
    bursty_in_progress[cl] = true;
    bursty_start[cl] = 0;
    hibernate_counter[cl] = 1;
    effective_trace_len[cl] = 0;

    reuse[cl] = (double *) lmmmap ( sizeof(double) * (TRACE_LENGTH+1) );
    reserved[cl] = (double *) lmmmap (sizeof(double) * (TRACE_LENGTH+1));
    reuse_s[cl] = (int *) lmmmap ( sizeof(int) * (TRACE_LENGTH+1) );
    reuse_e[cl] = (int *) lmmmap ( sizeof(int) * (TRACE_LENGTH+1) );
    circle_buffer[cl] = (int *) lmmmap ( sizeof(int) * TRACE_LENGTH );

    for (int i=0;i<TRACE_LENGTH+1;i++) {
      reuse[cl][i] = 0;
      reserved[cl][i] = 0;
      reuse_s[cl][i] = -1;
      reuse_e[cl][i] = -1;
    }
    for (int i=0;i<TRACE_LENGTH;i++) {
      circle_buffer[cl][i] = -1;
    }
 // end lpc
  }

}

void ThreadCache::Cleanup() {
//  printf("calling Cleanup, thread id : %u\n", tid_);

  // add by lpc

#if defined(APF_STATS)
  unsigned long long one_total_syncs, one_total_synchronized;
  unsigned long long one_fetched_syncs, one_return_syncs;

  one_total_syncs = 0;
  one_total_synchronized = 0;
  one_fetched_syncs = 0;
  one_return_syncs = 0;
  int sc;
  for( sc = 0; sc < kNumClasses; ++sc ) {
      one_total_syncs += total_gap[sc];
      one_total_synchronized += total_synchronized[sc];
      one_fetched_syncs += fetched_syncs[sc];
      one_return_syncs += return_syncs[sc];
  }
  atmc_fetch_and_add(&mem_gap, one_total_syncs);
  atmc_fetch_and_add(&total_syncs, one_total_synchronized);
  atmc_fetch_and_add(&total_fetch_syncs, one_fetched_syncs);
  atmc_fetch_and_add(&total_return_syncs, one_return_syncs);
  atmc_fetch_and_add(&total_memory_cost, memory_cost);
#endif
  
  lmmumap((void *)X_1, sizeof(long long)*(TRACE_LENGTH+1) );
  lmmumap((void *)X_2, sizeof(long long)*(TRACE_LENGTH+1) );
  lmmumap((void *)Y_1, sizeof(long long)*(TRACE_LENGTH+1) );
  lmmumap((void *)Y_2, sizeof(long long)*(TRACE_LENGTH+1) );
  lmmumap((void *)Z_1, sizeof(long long)*(TRACE_LENGTH+1) );
  lmmumap((void *)Z_2, sizeof(long long)*(TRACE_LENGTH+1) );

  for (int cl = 0; cl < kNumClasses; ++cl) {
    if (list_[cl].length() > 0) {
      ReleaseToCentralCache(&list_[cl], cl, list_[cl].length());
#if defined(APF_STATS)
      total_synchronized[cl] ++;
      return_syncs[cl] ++;
#endif
    }

    lmmumap((void *)reuse[cl], sizeof(double)*(TRACE_LENGTH+1));
    lmmumap((void *)reserved[cl], sizeof(double)*(TRACE_LENGTH+1));
    lmmumap((void *)reuse_s[cl], sizeof(int)*(TRACE_LENGTH+1));
    lmmumap((void *)reuse_e[cl], sizeof(int)*(TRACE_LENGTH+1));
    lmmumap((void *)circle_buffer[cl], sizeof(int)*(TRACE_LENGTH));

  }
}

// add by lpc
// Should optimized by fetching memory objects in a unit of batch_size.
int ThreadCache::FetchFromCentralCachePeriodicallyNoReturn(size_t cl, size_t byte_size, long n)
{
  FreeList* list = &list_[cl];

  void *start, *end;
  int fetch_count = Static::central_cache()[cl].RemoveRange(
      &start, &end, n);

  ASSERT((start == NULL) == (fetch_count == 0));
  if ( fetch_count >= 0 ) {
    size_ += byte_size * fetch_count;
    list->PushRange(fetch_count, start, end);
  }

  return fetch_count;
}
// end lpc

// Remove some objects of class "cl" from thread heap and add to central cache
void ThreadCache::ReleaseToCentralCache(FreeList* src, size_t cl, int N) {
  // add by lpc
  //atmc_fetch_and_add(&comm_times, 1);
  //printf("communicate times : %lu\n", comm_times);
  // end lpc

  ASSERT(src == &list_[cl]);
  if (N > src->length()) N = src->length();
  size_t delta_bytes = N * Static::sizemap()->ByteSizeForClass(cl);

  // We return prepackaged chains of the correct size to the central cache.
  // TODO: Use the same format internally in the thread caches?
  int batch_size = Static::sizemap()->num_objects_to_move(cl);
  while (N > batch_size) {
    void *tail, *head;
    src->PopRange(batch_size, &head, &tail);
    Static::central_cache()[cl].InsertRange(head, tail, batch_size);
    N -= batch_size;
  }
  void *tail, *head;
  src->PopRange(N, &head, &tail);
  Static::central_cache()[cl].InsertRange(head, tail, N);
  size_ -= delta_bytes;
}

// steal virtual memory size from other threads.
//
void ThreadCache::IncreaseCacheLimitLocked() {
  if (unclaimed_cache_space_ > 0) {
    // Possibly make unclaimed_cache_space_ negative.
    unclaimed_cache_space_ -= kStealAmount /* 64K bytes */ ;
    max_size_ += kStealAmount;
    return;
  }
  // Don't hold pageheap_lock too long.  Try to steal from 10 other
  // threads before giving up.  The i < 10 condition also prevents an
  // infinite loop in case none of the existing thread heaps are
  // suitable places to steal from.
  for (int i = 0; i < 10;
       ++i, next_memory_steal_ = next_memory_steal_->next_) {
    // Reached the end of the linked list.  Start at the beginning.
    if (next_memory_steal_ == NULL) {
      ASSERT(thread_heaps_ != NULL);
      next_memory_steal_ = thread_heaps_;
    }
    if (next_memory_steal_ == this ||
        next_memory_steal_->max_size_ <= kMinThreadCacheSize) {
      continue;
    }
    next_memory_steal_->max_size_ -= kStealAmount;
    max_size_ += kStealAmount;

    next_memory_steal_ = next_memory_steal_->next_;
    return;
  }
}

void ThreadCache::InitModule() {
  SpinLockHolder h(Static::pageheap_lock());
  if (!phinited) {
    const char *tcb = TCMallocGetenvSafe("TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES");
    if (tcb) {
      set_overall_thread_cache_size(strtoll(tcb, NULL, 10));
    }
    Static::InitStaticVars();
    threadcache_allocator.Init();
    phinited = 1;
  }
}

void ThreadCache::InitTSD() {
  ASSERT(!tsd_inited_);
  perftools_pthread_key_create(&heap_key_, DestroyThreadCache);
  tsd_inited_ = true;

#ifdef PTHREADS_CRASHES_IF_RUN_TOO_EARLY
  // We may have used a fake pthread_t for the main thread.  Fix it.
  pthread_t zero;
  memset(&zero, 0, sizeof(zero));
  SpinLockHolder h(Static::pageheap_lock());
  for (ThreadCache* h = thread_heaps_; h != NULL; h = h->next_) {
    if (h->tid_ == zero) {
      h->tid_ = pthread_self();
    }
  }
#endif
}

ThreadCache* ThreadCache::CreateCacheIfNecessary() {
  // Initialize per-thread data if necessary
  ThreadCache* heap = NULL;
  {
    SpinLockHolder h(Static::pageheap_lock());
    // On some old glibc's, and on freebsd's libc (as of freebsd 8.1),
    // calling pthread routines (even pthread_self) too early could
    // cause a segfault.  Since we can call pthreads quite early, we
    // have to protect against that in such situations by making a
    // 'fake' pthread.  This is not ideal since it doesn't work well
    // when linking tcmalloc statically with apps that create threads
    // before main, so we only do it if we have to.
#ifdef PTHREADS_CRASHES_IF_RUN_TOO_EARLY
    pthread_t me;
    if (!tsd_inited_) {
      memset(&me, 0, sizeof(me));
    } else {
      me = pthread_self();
    }
#else
    const pthread_t me = pthread_self();
#endif

    // This may be a recursive malloc call from pthread_setspecific()
    // In that case, the heap for this thread has already been created
    // and added to the linked list.  So we search for that first.
    for (ThreadCache* h = thread_heaps_; h != NULL; h = h->next_) {
      if (h->tid_ == me) {
        heap = h;
        break;
      }
    }

    if (heap == NULL) heap = NewHeap(me);
  }

  // We call pthread_setspecific() outside the lock because it may
  // call malloc() recursively.  We check for the recursive call using
  // the "in_setspecific_" flag so that we can avoid calling
  // pthread_setspecific() if we are already inside pthread_setspecific().
  if (!heap->in_setspecific_ && tsd_inited_) {
    heap->in_setspecific_ = true;
    perftools_pthread_setspecific(heap_key_, heap);
#ifdef HAVE_TLS
    // Also keep a copy in __thread for faster retrieval
    threadlocal_data_.heap = heap;
    SetMinSizeForSlowPath(kMaxSize + 1);
#endif
    heap->in_setspecific_ = false;
  }
  return heap;
}

ThreadCache* ThreadCache::NewHeap(pthread_t tid) {
  // Create the heap and add it to the linked list
  ThreadCache *heap = threadcache_allocator.New();
  heap->Init(tid);
  heap->next_ = thread_heaps_;
  heap->prev_ = NULL;
  if (thread_heaps_ != NULL) {
    thread_heaps_->prev_ = heap;
  } else {
    // This is the only thread heap at the momment.
    ASSERT(next_memory_steal_ == NULL);
    next_memory_steal_ = heap;
  }
  thread_heaps_ = heap;
  thread_heap_count_++;
  return heap;
}

void ThreadCache::BecomeIdle() {
  if (!tsd_inited_) return;              // No caches yet
  ThreadCache* heap = GetThreadHeap();
  if (heap == NULL) return;             // No thread cache to remove
  if (heap->in_setspecific_) return;    // Do not disturb the active caller

  heap->in_setspecific_ = true;
  perftools_pthread_setspecific(heap_key_, NULL);
#ifdef HAVE_TLS
  // Also update the copy in __thread
  threadlocal_data_.heap = NULL;
  SetMinSizeForSlowPath(0);
#endif
  heap->in_setspecific_ = false;
  if (GetThreadHeap() == heap) {
    // Somehow heap got reinstated by a recursive call to malloc
    // from pthread_setspecific.  We give up in this case.
    return;
  }

  // We can now get rid of the heap
  DeleteCache(heap);
}

void ThreadCache::DestroyThreadCache(void* ptr) {
  // Note that "ptr" cannot be NULL since pthread promises not
  // to invoke the destructor on NULL values, but for safety,
  // we check anyway.
  if (ptr == NULL) return;
#ifdef HAVE_TLS
  // Prevent fast path of GetThreadHeap() from returning heap.
  threadlocal_data_.heap = NULL;
  SetMinSizeForSlowPath(0);
#endif
  DeleteCache(reinterpret_cast<ThreadCache*>(ptr));
}

void ThreadCache::DeleteCache(ThreadCache* heap) {
  // Remove all memory from heap
  heap->Cleanup();

  // Remove from linked list
  SpinLockHolder h(Static::pageheap_lock());

  // add by lpc
#if 0
  static int i=0;
  printf("------ thread %d ------\n", i++);
  for (int cl = 0; cl < kNumClasses; ++cl) {
    printf("Class [%d] : %d allocations\n", cl, heap->time_cnt[cl]);
  }
#endif
  // end lpc

  if (heap->next_ != NULL) heap->next_->prev_ = heap->prev_;
  if (heap->prev_ != NULL) heap->prev_->next_ = heap->next_;
  if (thread_heaps_ == heap) thread_heaps_ = heap->next_;
  thread_heap_count_--;

  if (next_memory_steal_ == heap) next_memory_steal_ = heap->next_;
  if (next_memory_steal_ == NULL) next_memory_steal_ = thread_heaps_;
  unclaimed_cache_space_ += heap->max_size_;

  threadcache_allocator.Delete(heap);
}

void ThreadCache::RecomputePerThreadCacheSize() {
  // Divide available space across threads
  int n = thread_heap_count_ > 0 ? thread_heap_count_ : 1;
  size_t space = overall_thread_cache_size_ / n;

  // Limit to allowed range
  if (space < kMinThreadCacheSize) space = kMinThreadCacheSize;
  if (space > kMaxThreadCacheSize) space = kMaxThreadCacheSize;

  double ratio = space / max<double>(1, per_thread_cache_size_);
  size_t claimed = 0;
  for (ThreadCache* h = thread_heaps_; h != NULL; h = h->next_) {
    // Increasing the total cache size should not circumvent the
    // slow-start growth of max_size_.
    if (ratio < 1.0) {
        h->max_size_ = static_cast<size_t>(h->max_size_ * ratio);
    }
    claimed += h->max_size_;
  }
  unclaimed_cache_space_ = overall_thread_cache_size_ - claimed;
  per_thread_cache_size_ = space;
}

void ThreadCache::GetThreadStats(uint64_t* total_bytes, uint64_t* class_count) {
  for (ThreadCache* h = thread_heaps_; h != NULL; h = h->next_) {
    *total_bytes += h->Size();
    if (class_count) {
      for (int cl = 0; cl < kNumClasses; ++cl) {
        class_count[cl] += h->freelist_length(cl);
      }
    }
  }
}

// add by lpc
void ThreadCache::PrintTotalAllocationsPerSizeClass()
{
  int i=0;
  for (ThreadCache* h = thread_heaps_; h != NULL; h = h->next_) {
    printf("------ thread %d ------\n", i++);
    for (int cl = 0; cl < kNumClasses; ++cl) {
      printf("Class [%d] : %d allocations\n", cl, h->time_cnt[cl]);
    }
  }
}
// end lpc

void ThreadCache::set_overall_thread_cache_size(size_t new_size) {
  // Clip the value to a reasonable range
  if (new_size < kMinThreadCacheSize) new_size = kMinThreadCacheSize; /* 512K bytes */
  if (new_size > (1<<30)) new_size = (1<<30);     // Limit to 1GB
  overall_thread_cache_size_ = new_size;

  RecomputePerThreadCacheSize();
}

}  // namespace tcmalloc
