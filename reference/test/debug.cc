#include "gpu_cache_ref.h"
#include <cstdio>
int main() {
    printf("DEBUG START\n"); fflush(stdout);
    cache_config config;
    char cfg[] = "N:64:128:4,L:R:m:N:L,F:24:4,48,16";
    config.m_config_string = cfg;
    printf("Parsing: %s\n", cfg); fflush(stdout);
    config.init(cfg, FuncCachePreferNone);
    printf("OK: nsets=%u\n", config.get_nset()); fflush(stdout);
    return 0;
}
