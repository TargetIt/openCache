#include "gpu_cache_ref.h"
#include <cstdio>
int main() {
    printf("T1\n"); fflush(stdout);
    cache_config config;
    char cfg[] = "N:16:64:4,L:B:m:F:L,A:16:4,32";
    config.m_config_string = cfg;
    config.init(cfg, FuncCachePreferNone);
    printf("config nsets=%u\n", config.get_nset()); fflush(stdout);

    simple_mem_interface mem(64);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    printf("T2: creating cache\n"); fflush(stdout);
    data_cache cache("TestWB", config, 0, 0, &mem, &allocator,
                     IN_L1D_MISS_QUEUE, L1_WR_ALLOC_R, L1_WRBK_ACC,
                     &gpu, L1_GPU_CACHE);
    printf("T3: cache created\n"); fflush(stdout);

    mem_access_sector_mask_t smask;
    smask.set(0);
    mem_access_t access(GLOBAL_ACC_W, 0x1000, 4, true, active_mask_t(), mem_access_byte_mask_t(), smask);
    warp_inst_t inst;
    mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);
    printf("T4: mf created\n"); fflush(stdout);

    std::list<cache_event> events;
    printf("T5: calling access...\n"); fflush(stdout);
    enum cache_request_status s = cache.access(mf.get_addr(), &mf, 1, events);
    printf("T6: status=%d\n", (int)s); fflush(stdout);
    return 0;
}
