#g++ -pthread -Wall -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare -fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc -fno-builtin-calloc -fno-builtin-cfree -fno-builtin-memalign -fno-builtin-posix_memalign -fno-builtin-valloc -fno-builtin-pvalloc -Wno-unused-result -DNO_FRAME_POINTER -fno-builtin -g -O2 -pthread -o a.out tcmalloc_unittest-tcmalloc_unittest.o tcmalloc_unittest-testutil.o ./.libs/libtcmalloc.so ./.libs/liblogging.a -lpthread -pthread -Wl,-rpath -Wl,/u/pli/gperftools-2.2.1/.libs -Wl,-rpath -Wl,/localdisk/pli/weapons/tcmalloc/lib
#g++ -pthread -Wall -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare -fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc -fno-builtin-calloc -fno-builtin-cfree -fno-builtin-memalign -fno-builtin-posix_memalign -fno-builtin-valloc -fno-builtin-pvalloc -Wno-unused-result -DNO_FRAME_POINTER -fno-builtin -g -O2 -pthread -o a.out tcmalloc_unittest-tcmalloc_unittest.o tcmalloc_unittest-testutil.o -L/localdisk/pli/weapons/tcmalloc/lib/ -ltcmalloc ./.libs/liblogging.a -lpthread -pthread -Wl,-rpath -Wl,/u/pli/gperftools-2.2.1/.libs -Wl,-rpath -Wl,/localdisk/pli/weapons/tcmalloc/lib
g++ -pthread -Wall -fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc -fno-builtin-calloc -fno-builtin-cfree -fno-builtin-memalign -fno-builtin-posix_memalign -fno-builtin-valloc -fno-builtin-pvalloc -g -O2 -o a.out tcmalloc_unittest-tcmalloc_unittest.o tcmalloc_unittest-testutil.o -L/localdisk/pli/weapons/tcmalloc/lib/ -ltcmalloc ./.libs/liblogging.a -lpthread -pthread -Wl,-rpath -Wl,/u/pli/gperftools-2.2.1/.libs 
