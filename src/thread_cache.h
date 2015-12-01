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
// Author: Sanjay Ghemawat <opensource@google.com>

#ifndef TCMALLOC_THREAD_CACHE_H_
#define TCMALLOC_THREAD_CACHE_H_

#include <config.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>                    // for pthread_t, pthread_key_t
#endif
#include <stddef.h>                     // for size_t, NULL
#ifdef HAVE_STDINT_H
#include <stdint.h>                     // for uint32_t, uint64_t
#endif
#include <sys/types.h>                  // for ssize_t
#include "common.h"
#include "linked_list.h"
#include "maybe_threads.h"
#include "page_heap_allocator.h"
#include "static_vars.h"

#include "common.h"            // for SizeMap, kMaxSize, etc
#include "internal_logging.h"  // for ASSERT, etc
#include "linked_list.h"       // for SLL_Pop, SLL_PopRange, etc
#include "page_heap_allocator.h"  // for PageHeapAllocator
#include "static_vars.h"       // for Static
#include "getenv_safe.h"

// add by lpc
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <string>
#include <limits.h>

#include "lmstack.h"
#include <stdio.h>
#include <unistd.h>

// end lpc

namespace tcmalloc {

// Even if we have support for thread-local storage in the compiler
// and linker, the OS may not support it.  We need to check that at
// runtime.  Right now, we have to keep a manual set of "bad" OSes.
#if defined(HAVE_TLS)
extern bool kernel_supports_tls;   // defined in thread_cache.cc
void CheckIfKernelSupportsTLS();
inline bool KernelSupportsTLS() {
  return kernel_supports_tls;
}
#endif    // HAVE_TLS

//-------------------------------------------------------------------
// Data kept per thread
//-------------------------------------------------------------------

class ThreadCache {
 public:
#ifdef HAVE_TLS
  enum { have_tls = true };
#else
  enum { have_tls = false };
#endif

  // add by lpc
#define TRACE_LENGTH (1000)
#define HIBERNATE_LENGTH (-1)
#define DEFAULT_APF (400)
#define APF_STATS (0)

  unsigned long long time_cnt[kNumClasses];
  int preset_apf;

#if defined(APF_STATS)
  unsigned long long total_gap[kNumClasses];
  unsigned long long memory_cost;
  int current_deaths[kNumClasses];
  int total_synchronized[kNumClasses];
  int return_syncs[kNumClasses];
#endif 
  int fetched_syncs[kNumClasses];

  int last_reserved[kNumClasses];

  /* for reuse calculation */
  long long *X_1;
  long long *X_2;
  long long *Y_1;
  long long *Y_2;
  long long *Z_1;
  long long *Z_2;
  double *reuse[kNumClasses];  // <= TRACE_LENGTH + 1
  double *reserved[kNumClasses]; // <= TRACE_LENGTH + 1
  int reuse_cnt[kNumClasses]; // <= TRACE_LENGTH + 1
  int *reuse_s[kNumClasses]; // <= TRACE_LENGTH + 1
  int *reuse_e[kNumClasses]; // <= TRACE_LENGTH + 1
  int *circle_buffer[kNumClasses]; // <= TRACE_LENGTH, initialize with -1;
  int cb_next[kNumClasses];

  /* for bursty sampling */
  int bursty_period_len;
  int hibernate_period_len;
  bool bursty_in_progress[kNumClasses];
  int bursty_start[kNumClasses];
  int hibernate_counter[kNumClasses];
  int effective_trace_len[kNumClasses];

  int apf_stat;
  // end lpc

  // All ThreadCache objects are kept in a linked list (for stats collection)
  ThreadCache* next_;
  ThreadCache* prev_;

  void Init(pthread_t tid);
  void Cleanup();

  // Accessors (mostly just for printing stats)
  int freelist_length(size_t cl) const { return list_[cl].length(); }

  // Total byte size in cache
  size_t Size() const { return size_; }

  // Allocate an object of the given size and class. The size given
  // must be the same as the size of the class in the size map.
  void* Allocate(size_t size, size_t cl);
  void Deallocate(void* ptr, size_t size_class);
  void UpdateReuse(size_t cl);
  void CalculateReuse(size_t cl, int trace_len);
  void ApfStats();


  static void         InitModule();
  static void         InitTSD();
  static ThreadCache* GetThreadHeap();
  static ThreadCache* GetCache();
  static ThreadCache* GetCacheIfPresent();
  static ThreadCache* GetCacheWhichMustBePresent();
  static ThreadCache* CreateCacheIfNecessary();
  static void         BecomeIdle();
  static size_t       MinSizeForSlowPath();
  static void         SetMinSizeForSlowPath(size_t size);

  static bool IsFastPathAllowed() { return MinSizeForSlowPath() != 0; }

  // Return the number of thread heaps in use.
  static inline int HeapsInUse();

  // Adds to *total_bytes the total number of bytes used by all thread heaps.
  // Also, if class_count is not NULL, it must be an array of size kNumClasses,
  // and this function will increment each element of class_count by the number
  // of items in all thread-local freelists of the corresponding size class.
  // REQUIRES: Static::pageheap_lock is held.
  static void GetThreadStats(uint64_t* total_bytes, uint64_t* class_count);

  // add by lpc
  static void PrintTotalAllocationsPerSizeClass();
  // end lpc

  // Sets the total thread cache size to new_size, recomputing the
  // individual thread cache sizes as necessary.
  // REQUIRES: Static::pageheap lock is held.
  static void set_overall_thread_cache_size(size_t new_size);
  static size_t overall_thread_cache_size() {
    return overall_thread_cache_size_;
  }

 private:
  class FreeList {
   private:
    void*    list_;       // Linked list of nodes

#ifdef _LP64
    // On 64-bit hardware, manipulating 16-bit values may be slightly slow.
    uint32_t length_;      // Current length.
    uint32_t lowater_;     // Low water mark for list length.
    uint32_t max_length_;  // Dynamic max list length based on usage.
    // Tracks the number of times a deallocation has caused
    // length_ > max_length_.  After the kMaxOverages'th time, max_length_
    // shrinks and length_overages_ is reset to zero.
    uint32_t length_overages_;
#else
    // If we aren't using 64-bit pointers then pack these into less space.
    uint16_t length_;
    uint16_t lowater_;
    uint16_t max_length_;
    uint16_t length_overages_;
#endif

   public:
    void Init() {
      list_ = NULL;
      length_ = 0;
      lowater_ = 0;
      max_length_ = 1;
      length_overages_ = 0;
    }

    // Return current length of list
    size_t length() const {
      return length_;
    }

    // Return the maximum length of the list.
    size_t max_length() const {
      return max_length_;
    }

    // Set the maximum length of the list.  If 'new_max' > length(), the
    // client is responsible for removing objects from the list.
    void set_max_length(size_t new_max) {
      max_length_ = new_max;
    }

    // Return the number of times that length() has gone over max_length().
    size_t length_overages() const {
      return length_overages_;
    }

    void set_length_overages(size_t new_count) {
      length_overages_ = new_count;
    }

    // Is list empty?
    bool empty() const {
      return list_ == NULL;
    }

    // Low-water mark management
    int lowwatermark() const { return lowater_; }
    void clear_lowwatermark() { lowater_ = length_; }

    void Push(void* ptr) {
      SLL_Push(&list_, ptr);
      length_++;
    }

    void* Pop() {
      ASSERT(list_ != NULL);
      length_--;
      if (length_ < lowater_) lowater_ = length_;
      return SLL_Pop(&list_);
    }

    void* Next() {
      return SLL_Next(&list_);
    }

    void PushRange(int N, void *start, void *end) {
      SLL_PushRange(&list_, start, end);
      length_ += N;
    }

    void PopRange(int N, void **start, void **end) {
      SLL_PopRange(&list_, N, start, end);
      ASSERT(length_ >= N);
      length_ -= N;
      if (length_ < lowater_) lowater_ = length_;
    }
  };

  // Gets and returns an object from the central cache, and, if possible,
  // also adds some objects of that size class to this thread cache.
  int FetchFromCentralCachePeriodicallyNoReturn(size_t cl, size_t byte_size, long n);

  // Releases N items from this thread cache.
  void ReleaseToCentralCache(FreeList* src, size_t cl, int N);

  // Same as above but requires Static::pageheap_lock() is held.
  void IncreaseCacheLimitLocked();

  // If TLS is available, we also store a copy of the per-thread object
  // in a __thread variable since __thread variables are faster to read
  // than pthread_getspecific().  We still need pthread_setspecific()
  // because __thread variables provide no way to run cleanup code when
  // a thread is destroyed.
  // We also give a hint to the compiler to use the "initial exec" TLS
  // model.  This is faster than the default TLS model, at the cost that
  // you cannot dlopen this library.  (To see the difference, look at
  // the CPU use of __tls_get_addr with and without this attribute.)
  // Since we don't really use dlopen in google code -- and using dlopen
  // on a malloc replacement is asking for trouble in any case -- that's
  // a good tradeoff for us.
#ifdef HAVE___ATTRIBUTE__
#define ATTR_INITIAL_EXEC __attribute__ ((tls_model ("initial-exec")))
#else
#define ATTR_INITIAL_EXEC
#endif

#ifdef HAVE_TLS
  struct ThreadLocalData {
    ThreadCache* heap;
    // min_size_for_slow_path is 0 if heap is NULL or kMaxSize + 1 otherwise.
    // The latter is the common case and allows allocation to be faster
    // than it would be otherwise: typically a single branch will
    // determine that the requested allocation is no more than kMaxSize
    // and we can then proceed, knowing that global and thread-local tcmalloc
    // state is initialized.
    size_t min_size_for_slow_path;
  };
  static __thread ThreadLocalData threadlocal_data_ ATTR_INITIAL_EXEC;
#endif

  // Thread-specific key.  Initialization here is somewhat tricky
  // because some Linux startup code invokes malloc() before it
  // is in a good enough state to handle pthread_keycreate().
  // Therefore, we use TSD keys only after tsd_inited is set to true.
  // Until then, we use a slow path to get the heap object.
  static bool tsd_inited_;
  static pthread_key_t heap_key_;

  // Linked list of heap objects.  Protected by Static::pageheap_lock.
  static ThreadCache* thread_heaps_;
  static int thread_heap_count_;

  // A pointer to one of the objects in thread_heaps_.  Represents
  // the next ThreadCache from which a thread over its max_size_ should
  // steal memory limit.  Round-robin through all of the objects in
  // thread_heaps_.  Protected by Static::pageheap_lock.
  static ThreadCache* next_memory_steal_;

  // Overall thread cache size.  Protected by Static::pageheap_lock.
  static size_t overall_thread_cache_size_;

  // Global per-thread cache size.  Writes are protected by
  // Static::pageheap_lock.  Reads are done without any locking, which should be
  // fine as long as size_t can be written atomically and we don't place
  // invariants between this variable and other pieces of state.
  static volatile size_t per_thread_cache_size_;

  // Represents overall_thread_cache_size_ minus the sum of max_size_
  // across all ThreadCaches.  Protected by Static::pageheap_lock.
  static ssize_t unclaimed_cache_space_;

  // This class is laid out with the most frequently used fields
  // first so that hot elements are placed on the same cache line.

  size_t        size_;                  // Combined size of data
  size_t        max_size_;              // size_ > max_size_ --> Scavenge()

  // We sample allocations, biased by the size of the allocation

  FreeList      list_[kNumClasses];     // Array indexed by size-class

  pthread_t     tid_;                   // Which thread owns it
  bool          in_setspecific_;        // In call to pthread_setspecific?

  // Allocate a new heap. REQUIRES: Static::pageheap_lock is held.
  static ThreadCache* NewHeap(pthread_t tid);

  // Use only as pthread thread-specific destructor function.
  static void DestroyThreadCache(void* ptr);

  static void DeleteCache(ThreadCache* heap);
  static void RecomputePerThreadCacheSize();

  // Ensure that this class is cacheline-aligned. This is critical for
  // performance, as false sharing would negate many of the benefits
  // of a per-thread cache.
} CACHELINE_ALIGNED;

// Allocator for thread heaps
// This is logically part of the ThreadCache class, but MSVC, at
// least, does not like using ThreadCache as a template argument
// before the class is fully defined.  So we put it outside the class.
extern PageHeapAllocator<ThreadCache> threadcache_allocator;

inline int ThreadCache::HeapsInUse() {
  return threadcache_allocator.inuse();
}

// add by lpc
static unsigned long long comm_times2 = 0;
#define LOCK_PREFIX     "lock ; "
inline long long atmc_fetch_and_add(volatile unsigned long long *address, long long value)
{
        int prev = value;
        asm volatile(
                LOCK_PREFIX "xaddq %0, %1"
                : "+r" (value), "+m" (*address)
                : : "memory");

        return prev + value;
}
// end lpc
//

#define lmax(a,b) \
({ __typeof__ (a) _a = (a); \
__typeof__ (b) _b = (b); \
_a > _b ? _a : _b; })

#define lmin(a,b) \
({ __typeof__ (a) _a = (a); \
__typeof__ (b) _b = (b); \
_a < _b ? _a : _b; })


inline void ThreadCache::CalculateReuse(size_t sc, int trace_len)
{
  assert(trace_len <= TRACE_LENGTH);
  int i, w, k;
  long long X, Y, Z, XX_1, YY_1, ZZ_1;
  //for (sc=1; sc<kNumClasses; sc++) {
    X=0; Y=0; Z=0;
    memset(X_1, 0, sizeof(long long)*(TRACE_LENGTH+1));
    memset(X_2, 0, sizeof(long long)*(TRACE_LENGTH+1));
    memset(Y_1, 0, sizeof(long long)*(TRACE_LENGTH+1));
    memset(Y_2, 0, sizeof(long long)*(TRACE_LENGTH+1));
    memset(Z_1, 0, sizeof(long long)*(TRACE_LENGTH+1));
    memset(Z_2, 0, sizeof(long long)*(TRACE_LENGTH+1));
    for (i=0; i<reuse_cnt[sc]; i++) {
      if ( reuse_s[sc][i] == reuse_e[sc][i] ) {
        X += reuse_s[sc][i];
        Y += reuse_e[sc][i];
        Z += 1;
      }
      w = reuse_e[sc][i] - reuse_s[sc][i] + 1;

      X_1[reuse_s[sc][i]] ++;
      X_2[w] += lmin(trace_len - w, reuse_s[sc][i]);

      Y_1[reuse_e[sc][i]] ++;
      Y_2[w] += lmax(w-1, reuse_e[sc][i]);

      Z_1[reuse_e[sc][i] - reuse_s[sc][i]] ++;
      Z_2[w] += w;
    }
    
    reuse[sc][1] = ((double) X - Y + Z) / (trace_len);
    reserved[sc][1] = 1;
    int ever_max_reserved = 1;
    XX_1 = 0;
    YY_1 = 0;
    ZZ_1 = 0;
    for (k=2; k<=trace_len; k++) {
      XX_1 += X_1[trace_len - (k-1) ];
      X = X - XX_1 + X_2[k];
      YY_1 += Y_1[k-2];
      Y = Y + YY_1 + Y_2[k];
      ZZ_1 += Z_1[k-2];
      Z = Z + ZZ_1 + Z_2[k];
      reuse[sc][k] = ((double) X - Y + Z) / (trace_len - k + 1);
      reserved[sc][k] = (int)( k - reuse[sc][k] );
      if ( reserved[sc][k] < ever_max_reserved ) 
        reserved[sc][k] = ever_max_reserved;
      else 
        ever_max_reserved = reserved[sc][k];
    }

    /* clear the reuse counter for preparation of next hibernate */
    if (trace_len == TRACE_LENGTH) {
      reuse_cnt[sc] = 0;
      bursty_in_progress[sc] = false;
      hibernate_counter[sc] = hibernate_period_len; 
      assert(hibernate_counter[sc] > 0);
    }
  //}

}

inline void ThreadCache::UpdateReuse(size_t cl)
{
  int s_time = circle_buffer[cl][((cb_next[cl]-1+TRACE_LENGTH)%TRACE_LENGTH)];
  /* less than bursty_start means no freed slot that can be reused */
  if ( s_time >= bursty_start[cl]) {
    circle_buffer[cl][((cb_next[cl]-1+TRACE_LENGTH)%TRACE_LENGTH)] = -1;
    cb_next[cl] = (cb_next[cl]-1+TRACE_LENGTH) % TRACE_LENGTH;
    reuse_s[cl][reuse_cnt[cl]] = s_time - bursty_start[cl];
    reuse_e[cl][reuse_cnt[cl]] = time_cnt[cl] - bursty_start[cl];
    reuse_cnt[cl] ++;
  }

  int update_time_point;
  update_time_point = time_cnt[cl] - bursty_start[cl] + 1;
  if ( update_time_point == 100  || update_time_point == 200  || update_time_point == 500 
    || update_time_point == 1000 || update_time_point == 5000 || update_time_point == 10000
    || update_time_point == TRACE_LENGTH ) {
    effective_trace_len[cl] = update_time_point;
    CalculateReuse(cl, update_time_point);
  }
}

inline void ThreadCache::ApfStats()
{
#if 0
  //fprintf(stderr, "###LPC: outside allocation! %d\n", apf_stat);
  //const char * env = NULL;
  bool env = false;
  char file_nm[1024] = {'\0'};
  sprintf(file_nm, "/localdisk/pli/apf_stats");
  int fd = open(file_nm, O_RDONLY);
  if ( fd < 0 ) {
    fprintf(stderr, "open %s fails : %s\n", file_nm, strerror(errno));
  }
  else {
    struct stat s;
    int status;
    int size;
    char * mapped;
    status = fstat(fd, &s);
    size = s.st_size;
    mapped = (char *)mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
    if ( mapped == MAP_FAILED ) { fprintf(stderr, "mmap fails %s\n", file_nm); }
    else {
      char buf[1024] = {'\0'};
      memcpy(buf, mapped, size);
      int val = atoi(buf);
      if ( val == 1 ) env = true;
      if ( val == 0 ) env = false;
      munmap(mapped, size);
      close(fd);
    }
    //if ( env) fprintf(stderr, "pid: %d, env: %d\n", (int)getpid(), 1);
  }
  //env = getenv("APF_STATS");
  //if ( env != NULL && apf_stat == 0 ) {
  if ( env && apf_stat == 0 ) {
    //fprintf(stderr, "###LPC: allocation!\n");
    //fflush(stderr);
    apf_stat = 1;
   #endif 
    unsigned long long one_total_gap, one_total_synchronized;
    unsigned long long one_fetched_syncs, one_return_syncs;
    unsigned long long time_cnts;
  
    one_total_gap = 0;
    one_total_synchronized = 0;
    one_fetched_syncs = 0; 
    one_return_syncs = 0;
    time_cnts = 0;
    int sc;
    for( sc = 0; sc < kNumClasses; ++sc ) {
      one_total_gap += total_gap[sc];
      one_total_synchronized += total_synchronized[sc];
      one_fetched_syncs += fetched_syncs[sc];
      one_return_syncs += return_syncs[sc];
      time_cnts += time_cnt[sc];
    }

   // fprintf(stderr, "###LPC: start to output apf stats ...\n");
   // fflush(stderr);
    fprintf(stderr, "###LPC: ppid: %d, pid: %d, tid: %llu, total time: %llu, total gap: %llu, total_syncs: %llu, fetch_syncs: %llu, return_syncs: %llu, total_cost: %llu\n",  (int)getppid(), (int)getpid(),tid_, time_cnts, one_total_gap, one_total_synchronized, one_fetched_syncs, one_return_syncs, memory_cost);
    fflush(stderr);
 // }
  
  return;
}

inline void* ThreadCache::Allocate(size_t size, size_t cl) {
  ASSERT(size <= kMaxSize);
  ASSERT(size == Static::sizemap()->ByteSizeForClass(cl));

  FreeList* list = &list_[cl];

#if defined(APF_STATS)
  int added =  list->length() - current_deaths[cl];
  if ( added < 0) added = 0;
  total_gap[cl] += added;
  current_deaths[cl] = 0;
#endif

#if defined(APF_STATS)
  // ApfStats();
#endif

  // accumulate meta-data of reuse calculation
  // initially, bursty_start = 0 ;
  if ( unlikely(bursty_in_progress[cl]) )  { 
    UpdateReuse(cl);
  }
  else {
    /* -1 means only sampling once to begin with */
    if ( likely( hibernate_period_len != -1 ) ) {
      if ( (--hibernate_counter[cl]) == 0 ) {
        bursty_in_progress[cl] = true;
        bursty_start[cl] = time_cnt[cl]+1;
      }
    }
  }

  if ( unlikely( list->empty() ) ) {
    /* Chen's approach : to present */
    int target_apf = (fetched_syncs[cl]+1) * preset_apf - time_cnt[cl];
    /* Pengcheng's approach: to present */
    //int target_apf = (total_synchronized[cl]+1) * preset_apf - time_cnt[cl];
    if ( target_apf <= 0 ) target_apf = preset_apf;
    int target_reserved;
    if ( effective_trace_len[cl] != 0 ) {
      if ( target_apf <= effective_trace_len[cl] ) {
        target_reserved = reserved[cl][target_apf];
      }
      else {
        float ratio = (float)target_apf / 100;
        target_reserved = reserved[cl][100] * ratio;
      }
    }
    else {
      target_reserved = (int)(0.618 * (float)target_apf + 1); //round up by increasing 1.
    }
    assert(target_reserved > 0);
    last_reserved[cl] = target_reserved;
    FetchFromCentralCachePeriodicallyNoReturn(cl, size, target_reserved);
#if defined(APF_STATS)
    total_synchronized[cl] ++;
#endif
    fetched_syncs[cl] ++;
  }

  time_cnt[cl] ++;

  void * res;
  size_ -= size;
  res = list->Pop();

#if defined(APF_STATS)
  memory_cost += size_;
#endif

  return res;
}

inline void ThreadCache::Deallocate(void* ptr, size_t cl) {
  FreeList* list = &list_[cl];
  size_ += Static::sizemap()->ByteSizeForClass(cl);

  // This catches back-to-back frees of allocs in the same size
  // class. A more comprehensive (and expensive) test would be to walk
  // the entire freelist. But this might be enough to find some bugs.
  ASSERT(ptr != list->Next());

  list->Push(ptr);

#if defined(APF_STATS)
  current_deaths[cl] ++;
#endif

  /* for reuse */
  if ( unlikely( bursty_in_progress[cl] ) ) {
    circle_buffer[cl][cb_next[cl]] = time_cnt[cl];
    cb_next[cl] = (cb_next[cl]+1) % TRACE_LENGTH;
  }

  //int expectation = Static::sizemap()->win_live[cl][preset_apf];
  int expectation = last_reserved[cl]; 
/*
 * try 4:
 * 1. threshold
 * 2. + 1000
 * 2.1 num_to_move(cl)
 * 3. x 2, x 3
 * 4. periodical checking
 * */
  //if ( list->length() > expectation * 2 + 100 ) {
  if ( unlikely( list->length() > expectation * 2 + 1 ) ) {
#if 0
    int target_apf = (fetched_syncs[cl]+1) * preset_apf - time_cnt[cl];
    if ( target_apf <= 0 ) target_apf = preset_apf;
    int reserved;
    if ( target_apf < 20000 ) {
      reserved = Static::sizemap()->win_live[cl][target_apf];
    }
    else {
      float ratio = (float)target_apf / 100;
      reserved = Static::sizemap()->win_live[cl][100] * ratio;
    }
    int fetched = reserved - num_of_live_objs[cl];
    int returned = list->length() - fetched;
#endif
    if ( expectation > 0 )
      ReleaseToCentralCache(list, cl, list->length() - expectation);
    else
      ReleaseToCentralCache(list, cl, list->length() / 2 );

#if defined(APF_STATS)
    return_syncs[cl] ++;
    total_synchronized[cl] ++;
#endif
  } 

}

inline ThreadCache* ThreadCache::GetThreadHeap() {
#ifdef HAVE_TLS
  // __thread is faster, but only when the kernel supports it
  if (KernelSupportsTLS())
    return threadlocal_data_.heap;
#endif
  return reinterpret_cast<ThreadCache *>(
      perftools_pthread_getspecific(heap_key_));
}

inline ThreadCache* ThreadCache::GetCacheWhichMustBePresent() {
#ifdef HAVE_TLS
  ASSERT(threadlocal_data_.heap);
  return threadlocal_data_.heap;
#else
  ASSERT(perftools_pthread_getspecific(heap_key_));
  return reinterpret_cast<ThreadCache *>(
      perftools_pthread_getspecific(heap_key_));
#endif
}

inline ThreadCache* ThreadCache::GetCache() {
  ThreadCache* ptr = NULL;
  if (!tsd_inited_) {
    InitModule();
  } else {
    ptr = GetThreadHeap();
  }
  if (ptr == NULL) ptr = CreateCacheIfNecessary();
  return ptr;
}

// In deletion paths, we do not try to create a thread-cache.  This is
// because we may be in the thread destruction code and may have
// already cleaned up the cache for this thread.
inline ThreadCache* ThreadCache::GetCacheIfPresent() {
  if (!tsd_inited_) return NULL;
  return GetThreadHeap();
}

inline size_t ThreadCache::MinSizeForSlowPath() {
#ifdef HAVE_TLS
  return threadlocal_data_.min_size_for_slow_path;
#else
  return 0;
#endif
}

inline void ThreadCache::SetMinSizeForSlowPath(size_t size) {
#ifdef HAVE_TLS
  threadlocal_data_.min_size_for_slow_path = size;
#endif
}

}  // namespace tcmalloc

#endif  // TCMALLOC_THREAD_CACHE_H_
