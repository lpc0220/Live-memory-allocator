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

#include <stdlib.h> // for getenv and strtol
#include "config.h"
#include "common.h"
#include "system-alloc.h"
#include "base/spinlock.h"
#include "getenv_safe.h" // TCMallocGetenvSafe

#include <stdio.h>
#include <fstream>
#include <sys/mman.h>
#include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <err.h>
#include <string>
#include <limits.h>
#include <unistd.h>

#include <iostream>

namespace tcmalloc {

// Define the maximum number of object per classe type to transfer between
// thread and central caches.
static int32 FLAGS_tcmalloc_transfer_num_objects;

static const int32 kDefaultTransferNumObjecs = 32768;

// The init function is provided to explicit initialize the variable value
// from the env. var to avoid C++ global construction that might defer its
// initialization after a malloc/new call.
static inline void InitTCMallocTransferNumObjects()
{
  if (UNLIKELY(FLAGS_tcmalloc_transfer_num_objects == 0)) {
    const char *envval = TCMallocGetenvSafe("TCMALLOC_TRANSFER_NUM_OBJ");
    FLAGS_tcmalloc_transfer_num_objects = !envval ? kDefaultTransferNumObjecs :
      strtol(envval, NULL, 10);
  }
}

// Note: the following only works for "n"s that fit in 32-bits, but
// that is fine since we only use it for small sizes.
static inline int LgFloor(size_t n) {
  int log = 0;
  for (int i = 4; i >= 0; --i) {
    int shift = (1 << i);
    size_t x = n >> shift;
    if (x != 0) {
      n = x;
      log += shift;
    }
  }
  ASSERT(n == 1);
  return log;
}

int AlignmentForSize(size_t size) {
  int alignment = kAlignment;
  if (size > kMaxSize) {
    // Cap alignment at kPageSize for large sizes.
    alignment = kPageSize;
  } else if (size >= 128) {
    // Space wasted due to alignment is at most 1/8, i.e., 12.5%.
    alignment = (1 << LgFloor(size)) / 8;
  } else if (size >= kMinAlign) {
    // We need an alignment of at least 16 bytes to satisfy
    // requirements for some SSE types.
    alignment = kMinAlign;
  }
  // Maximum alignment allowed is page size alignment.
  if (alignment > kPageSize) {
    alignment = kPageSize;
  }
  CHECK_CONDITION(size < kMinAlign || alignment >= kMinAlign);
  CHECK_CONDITION((alignment & (alignment - 1)) == 0);
  return alignment;
}

int SizeMap::NumMoveSize(size_t size) {
  if (size == 0) return 0;
  // Use approx 64k transfers between thread and central caches.
  int num = static_cast<int>(64.0 * 1024.0 / size);
  if (num < 2) num = 2;

  // Avoid bringing too many objects into small object free lists.
  // If this value is too large:
  // - We waste memory with extra objects sitting in the thread caches.
  // - The central freelist holds its lock for too long while
  //   building a linked list of objects, slowing down the allocations
  //   of other threads.
  // If this value is too small:
  // - We go to the central freelist too often and we have to acquire
  //   its lock each time.
  // This value strikes a balance between the constraints above.
  if (num > FLAGS_tcmalloc_transfer_num_objects)
    num = FLAGS_tcmalloc_transfer_num_objects;

  return num;
}


// Initialize the mapping arrays
void SizeMap::Init() {
  InitTCMallocTransferNumObjects();

  // Do some sanity checking on add_amount[]/shift_amount[]/class_array[]
  if (ClassIndex(0) != 0) {
    Log(kCrash, __FILE__, __LINE__,
        "Invalid class index for size 0", ClassIndex(0));
  }
  if (ClassIndex(kMaxSize) >= sizeof(class_array_)) {
    Log(kCrash, __FILE__, __LINE__,
        "Invalid class index for kMaxSize", ClassIndex(kMaxSize));
  }

  // Compute the size classes we want to use
  int sc = 1;   // Next size class to assign
  int alignment = kAlignment;
  CHECK_CONDITION(kAlignment <= kMinAlign);
  for (size_t size = kAlignment; size <= kMaxSize; size += alignment) {
    alignment = AlignmentForSize(size);
    CHECK_CONDITION((size % alignment) == 0);

    int blocks_to_move = NumMoveSize(size) / 4;
    size_t psize = 0;
    do {
      psize += kPageSize;
      // Allocate enough pages so leftover is less than 1/8 of total.
      // This bounds wasted space to at most 12.5%.
      while ((psize % size) > (psize >> 3)) {
        psize += kPageSize;
      }
      // Continue to add pages until there are at least as many objects in
      // the span as are needed when moving objects from the central
      // freelists and spans to the thread caches.
    } while ((psize / size) < (blocks_to_move));
    const size_t my_pages = psize >> kPageShift;

    if (sc > 1 && my_pages == class_to_pages_[sc-1]) {
      // See if we can merge this into the previous class without
      // increasing the fragmentation of the previous class.
      const size_t my_objects = (my_pages << kPageShift) / size;
      const size_t prev_objects = (class_to_pages_[sc-1] << kPageShift)
                                  / class_to_size_[sc-1];
      if (my_objects == prev_objects) {
        // Adjust last class to include this size
        class_to_size_[sc-1] = size;
        continue;
      }
    }

    // Add new class
    class_to_pages_[sc] = my_pages;
    class_to_size_[sc] = size;
    sc++;
  }
  if (sc != kNumClasses) {
    Log(kCrash, __FILE__, __LINE__,
        "wrong number of size classes: (found vs. expected )", sc, kNumClasses);
  }

  // Initialize the mapping arrays
  int next_size = 0;
  for (int c = 1; c < kNumClasses; c++) {
    const int max_size_in_class = class_to_size_[c];
    for (int s = next_size; s <= max_size_in_class; s += kAlignment) {
      class_array_[ClassIndex(s)] = c;
    }
    next_size = max_size_in_class + kAlignment;
  }

  // Double-check sizes just to be safe
  for (size_t size = 0; size <= kMaxSize;) {
    const int sc = SizeClass(size);
    if (sc <= 0 || sc >= kNumClasses) {
      Log(kCrash, __FILE__, __LINE__,
          "Bad size class (class, size)", sc, size);
    }
    if (sc > 1 && size <= class_to_size_[sc-1]) {
      Log(kCrash, __FILE__, __LINE__,
          "Allocating unnecessarily large class (class, size)", sc, size);
    }
    const size_t s = class_to_size_[sc];
    if (size > s || s == 0) {
      Log(kCrash, __FILE__, __LINE__,
          "Bad (class, size, requested)", sc, s, size);
    }
    if (size <= kMaxSmallSize) {
      size += 8;
    } else {
      size += 128;
    }
  }

  // Initialize the num_objects_to_move array.
//  printf("num_objects_to_move_:\n");
  for (size_t cl = 1; cl  < kNumClasses; ++cl) {
    num_objects_to_move_[cl] = NumMoveSize(ByteSizeForClass(cl));
//    printf("%d, ", num_objects_to_move_[cl]);
  }

  // add by lpc
#if 0 
  printf("read in win live\n");
  int temp_win_live;
  for (int cl = 0; cl < kNumClasses; ++cl) {
    char file_nm[1024] = {'\0'};
    sprintf(file_nm, "/localdisk/pli/MySQL/data/locallive_data/locallive_%d.dat", cl);

    struct stat s;
    int status;
    int size;
    char * mapped;
 
    int fd = open(file_nm, O_RDONLY);
    if (fd < 0) {
      printf( "open %s failed: %s", file_nm, strerror (errno));
      abort();
    }
    printf("read in %s\n", file_nm);
    status = fstat (fd, &s);
    if (status < 0) {
      printf("stat %s failed: %s", file_nm, strerror (errno));
      char result[1024];
      ssize_t count = readlink( "/proc/self/exe", result, 1024 );
      std::cerr << std::string( result, (count > 0) ? count : 0 ) << std::endl;
      std::cerr << "lpclpclpc\n"; 
      abort();
    }
    size = s.st_size;

    mapped = (char *)mmap (0, size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) { 
       printf("mmap %s failed: %s",
           file_nm, strerror (errno));
       abort();
    }

    char *buf_end;
    char * begin, * end;
    char buf[1024] = {'\0'};

    buf_end = mapped + size;
    begin = end = mapped;

    int i = 0;
    while (1) {
                if (! (*end == '\r' || *end == '\n')) {
                        if (++end < buf_end) continue;
                } else if (1 + end < buf_end) {
                        /* see if we got "\r\n" or "\n\r" here */
                        char c = *(1 + end);
                        if ( (c == '\r' || c == '\n') && c != *end)
                                ++end;
                }

          memcpy(buf, begin, end-begin);
          buf[end-begin] = '\0'; 
          temp_win_live = atoi(buf);
          win_live[cl][i] = temp_win_live;
          //if ( cl == 26 ) {
          //  printf("%d: %d\n", i, win_live[cl][i]);
          //}
          i++;
 
                if ((begin = ++end) >= buf_end)
                        break;
   }

   munmap(mapped, size);
   close(fd);

 }

  printf("after read in win live\n");
#endif
  // end lpc
//  printf("\n");
}

// Metadata allocator -- keeps stats about how many bytes allocated.
static uint64_t metadata_system_bytes_ = 0;
static const size_t kMetadataAllocChunkSize = 8*1024*1024;
static const size_t kMetadataBigAllocThreshold = kMetadataAllocChunkSize / 8;
// usually malloc uses larger alignments, but because metadata cannot
// have and fancy simd types, aligning on pointer size seems fine
static const size_t kMetadataAllignment = sizeof(void *);

static char *metadata_chunk_alloc_;
static size_t metadata_chunk_avail_;

static SpinLock metadata_alloc_lock(SpinLock::LINKER_INITIALIZED);

void* MetaDataAlloc(size_t bytes) {
  if (bytes >= kMetadataAllocChunkSize) {
    void *rv = TCMalloc_SystemAlloc(bytes,
                                    NULL, kMetadataAllignment);
    if (rv != NULL) {
      metadata_system_bytes_ += bytes;
    }
    return rv;
  }

  SpinLockHolder h(&metadata_alloc_lock);

  // the following works by essentially turning address to integer of
  // log_2 kMetadataAllignment size and negating it. I.e. negated
  // value + original value gets 0 and that's what we want modulo
  // kMetadataAllignment. Note, we negate before masking higher bits
  // off, otherwise we'd have to mask them off after negation anyways.
  intptr_t alignment = -reinterpret_cast<intptr_t>(metadata_chunk_alloc_) & (kMetadataAllignment-1);

  if (metadata_chunk_avail_ < bytes + alignment) {
    size_t real_size;
    void *ptr = TCMalloc_SystemAlloc(kMetadataAllocChunkSize,
                                     &real_size, kMetadataAllignment);
    if (ptr == NULL) {
      return NULL;
    }

    metadata_chunk_alloc_ = static_cast<char *>(ptr);
    metadata_chunk_avail_ = real_size;

    alignment = 0;
  }

  void *rv = static_cast<void *>(metadata_chunk_alloc_ + alignment);
  bytes += alignment;
  metadata_chunk_alloc_ += bytes;
  metadata_chunk_avail_ -= bytes;
  metadata_system_bytes_ += bytes;
  return rv;
}

uint64_t metadata_system_bytes() { return metadata_system_bytes_; }

}  // namespace tcmalloc