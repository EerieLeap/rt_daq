#ifdef CONFIG_APP_MEMORY_SHELL

    #include <stdlib.h>

    #include <zephyr/kernel.h>
    #include <zephyr/shell/shell.h>
    #include <zephyr/sys/sys_heap.h>

/* 'mem heaps': every sys_heap the firmware registered (CONFIG_SYS_HEAP_ARRAY_SIZE), with the
 * statistics of CONFIG_SYS_HEAP_RUNTIME_STATS. Two matter: the kernel heap (k_malloc, the dynamic
 * thread stacks and, on a board without a shared multi-heap, rt_core's PMR allocations) and the
 * libc arena behind malloc and C++ new. Zephyr's 'kernel heap' shows the kernel heap only. */

extern struct k_heap _system_heap;

static bool heap_holds(const struct sys_heap* heap, const struct sys_memory_stats* stats, const void* pointer) {
    const char* start = (const char*)heap->heap;
    const char* p = (const char*)pointer;
    return p >= start && p < start + stats->allocated_bytes + stats->free_bytes;
}

static int cmd_mem_heaps(const struct shell* sh, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    struct sys_heap** heaps = NULL;
    const int count = sys_heap_array_get(&heaps);
    if(count < 0) {
        shell_error(sh, "Cannot list the heaps (%d).", count);
        return count;
    }

    /* A probe allocation tells which heap is the libc arena. */
    void* probe = malloc(1);

    shell_print(sh, "%-10s %-6s %9s %9s %9s %9s", "heap", "kind", "size", "used", "free", "max used");
    for(int i = 0; i < count; i++) {
        struct sys_memory_stats stats;
        const int err = sys_heap_runtime_stats_get(heaps[i], &stats);
        if(err != 0) {
            shell_error(sh, "%p: no statistics (%d).", (void*)heaps[i], err);
            continue;
        }

        const char* kind = "other";
        if(heaps[i] == &_system_heap.heap)
            kind = "kernel";
        else if(probe != NULL && heap_holds(heaps[i], &stats, probe))
            kind = "libc";

        shell_print(
            sh, "%-10p %-6s %9zu %9zu %9zu %9zu", (void*)heaps[i], kind, stats.allocated_bytes + stats.free_bytes,
            stats.allocated_bytes, stats.free_bytes, stats.max_allocated_bytes
        );
    }

    free(probe);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_mem,
    SHELL_CMD(
        heaps,
        NULL,
        "Size, use and peak use of every heap: the kernel heap (k_malloc, dynamic thread stacks, "
        "PMR objects on boards without a shared multi-heap) and the libc arena (malloc, C++ new).",
        cmd_mem_heaps
    ),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mem, &sub_mem, "Memory bench commands", NULL);

#endif /* CONFIG_APP_MEMORY_SHELL */
