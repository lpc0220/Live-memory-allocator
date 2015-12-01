/**
 *  By Pengcheng Li <landy0220@gmail.com>
 *  -- C++ --
 *
 *  NOTE: in tcmalloc, malloc cannot be used, so we have to use mmap to
 *  alloc memory.
 */

#ifndef __LM_MMAP__
#define __LM_MMAP__

#include <sys/mman.h>

inline void * lmmmap ( int bytes )
{
  void * ret = (void *)mmap(0, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if ( ret == (void *)-1 ) {
    std::cout << "Error: lm mmap fails!\n";
    abort();
  }

  return ret;
}

inline void lmmumap ( void * location, int bytes )
{
  int ret;
  ret = munmap( location, bytes );
  if ( ret == -1 ) {
      std::cout << "Error: lm mumap failed!\n";
      abort();
  }
}

#endif /* __LM_MMAP__ */
