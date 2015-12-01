
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
unsigned long long comm_times = 0;
unsigned long long total_syncs = 0;
unsigned long long total_fetch_syncs = 0;
unsigned long long total_return_syncs = 0;
unsigned long long mem_gap = 0;
unsigned long long total_memory_cost = 0;
