// =============================================================================
// Death tests for configuration/assert paths that must terminate in isolation.
// =============================================================================

#include "../gpgpu_cache/gpu_cache_ref.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static cache_config make_config(const char *text)
{
    cache_config cfg;
    char *copy = strdup(text);
    cfg.m_config_string = copy;
    cfg.init(copy, FuncCachePreferNone);
    return cfg;
}

static bool exits_abnormally(void (*fn)())
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return false;
    }
    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        fn();
        _exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return false;
    }

    return WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);
}

#define RUN_DEATH(name) do { \
    tests_run++; \
    printf("  RUN  %s ... ", #name); \
    if (exits_abnormally(death_##name)) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } else { \
        tests_failed++; \
        printf("FAILED\n"); \
    } \
} while (0)

static void death_parse_error()
{
    (void)make_config("bad-cache-config");
}

static void death_invalid_sector_line_size()
{
    (void)make_config("S:4:64:2,L:R:m:N:L,S:4:2,8");
}

static void death_writeback_on_fill()
{
    (void)make_config("N:4:64:2,L:B:f:N:L,A:4:2,8");
}

static void death_lazy_fetch_on_fill()
{
    (void)make_config("N:4:64:2,L:T:f:L:L,A:4:2,8");
}

static void death_bad_data_port_width()
{
    (void)make_config("N:4:64:2,L:R:m:N:L,A:4:2,8:1,24");
}

static void death_invalid_fermi_set_count()
{
    cache_config cfg = make_config("N:16:64:2,L:R:m:N:H,A:4:2,8");
    (void)cfg.set_index(0x1000);
}

int main()
{
    printf("\n========== GPGPU-Sim Cache Death Test Suite ==========\n\n");

    RUN_DEATH(parse_error);
    RUN_DEATH(invalid_sector_line_size);
    RUN_DEATH(writeback_on_fill);
    RUN_DEATH(lazy_fetch_on_fill);
    RUN_DEATH(bad_data_port_width);
    RUN_DEATH(invalid_fermi_set_count);

    printf("\n========== Results: %d/%d tests passed ==========\n",
           tests_passed, tests_run);
    if (tests_failed)
        printf("========== Failures: %d ==========\n", tests_failed);

    return tests_failed == 0 && tests_passed == tests_run ? 0 : 1;
}
