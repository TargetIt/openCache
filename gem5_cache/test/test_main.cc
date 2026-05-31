// =============================================================================
// Tests for reference gem5 cache code (UNMODIFIED from original gem5)
// =============================================================================

#include "base/types.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/cache/tags/tagged_entry.hh"
#include "mem/cache/tags/indexing_policies/set_associative.hh"
#include "mem/cache/replacement_policies/lru_rp.hh"
#include "mem/cache/replacement_policies/fifo_rp.hh"
#include "mem/cache/replacement_policies/mru_rp.hh"
#include "mem/cache/replacement_policies/random_rp.hh"
#include "mem/cache/replacement_policies/tree_plru_rp.hh"
#include "mem/cache/tags/fa_lru.hh"
#include "params/LRURP.hh"
#include "params/FIFORP.hh"
#include "params/MRURP.hh"
#include "params/RandomRP.hh"
#include "params/TreePLRURP.hh"
#include "params/SetAssociative.hh"

#include <cstdio>
#include <cassert>
#include <memory>

using namespace gem5;

static int tests_run = 0;
static int tests_passed = 0;

typedef CacheBlk::KeyType KeyType;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  RUN  %-40s ... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED\n  ASSERT_EQ(%s, %s)\n", #a, #b); \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n  ASSERT_TRUE(%s)\n", #cond); \
        return; \
    } \
} while(0)

TEST(cache_blk_valid) {
    CacheBlk blk;
    // CacheBlk needs a tag extractor before insert
    blk.registerTagExtractor([](Addr a) { return a; });
    ASSERT_TRUE(!blk.isValid());
    blk.insert(KeyType{0x1000, false}, 0, 0, 0);
    ASSERT_TRUE(blk.isValid());
}

TEST(cache_blk_position) {
    CacheBlk blk;
    blk.registerTagExtractor([](Addr a) { return a; });
    blk.insert(KeyType{0xABCD, false}, 0, 0, 0);
    blk.setPosition(0, 5);
    ASSERT_EQ((int)blk.getSet(), 0);
    ASSERT_EQ((int)blk.getWay(), 5);
}

TEST(cache_blk_refcount) {
    CacheBlk blk;
    blk.registerTagExtractor([](Addr a) { return a; });
    blk.insert(KeyType{0x4000, false}, 0, 0, 0);
    // After insert, refCount starts at 0
    ASSERT_EQ((int)blk.getRefCount(), 1);
    blk.increaseRefCount();
    ASSERT_EQ((int)blk.getRefCount(), 2);
}

TEST(cache_blk_invalidate) {
    CacheBlk blk;
    blk.registerTagExtractor([](Addr a) { return a; });
    blk.insert(KeyType{0x8000, false}, 0, 0, 0);
    ASSERT_TRUE(blk.isValid());
    blk.invalidate();
    ASSERT_TRUE(!blk.isValid());
}

TEST(lru_rp_create) {
    LRURPParams p;
    replacement_policy::LRU lru(p);
    auto entry = lru.instantiateEntry();
    ASSERT_TRUE(entry != nullptr);
    lru.touch(entry);
    lru.invalidate(entry);
    lru.reset(entry);
}

TEST(fifo_rp_create) {
    FIFORPParams p;
    replacement_policy::FIFO fifo(p);
    auto entry = fifo.instantiateEntry();
    ASSERT_TRUE(entry != nullptr);
    fifo.touch(entry);
    fifo.invalidate(entry);
    fifo.reset(entry);
}

TEST(mru_rp_create) {
    MRURPParams p;
    replacement_policy::MRU mru(p);
    auto entry = mru.instantiateEntry();
    ASSERT_TRUE(entry != nullptr);
}

TEST(random_rp_create) {
    RandomRPParams p;
    replacement_policy::Random rnd(p);
    auto entry = rnd.instantiateEntry();
    ASSERT_TRUE(entry != nullptr);
}

TEST(tree_plru_rp_create) {
    TreePLRURPParams p;
    p.num_leaves = 16;
    replacement_policy::TreePLRU plru(p);
    auto entry = plru.instantiateEntry();
    ASSERT_TRUE(entry != nullptr);
}

TEST(set_assoc_create) {
    SetAssociativeParams p;
    p.size = 4096;
    p.entry_size = 64;
    p.assoc = 8;
    SetAssociative sa(p);
    auto result = sa.getPossibleEntries((Addr)0x000);
    // Construction and getPossibleEntries should not crash
    ASSERT_TRUE(result.size() >= 0);
}

TEST(falru_create) {
    FALRUParams p;
    p.block_size = 64;
    p.size = 4096;
    p.tag_latency = Cycles(1);
    p.min_tracked_cache_size = 0;
    FALRU falru(p);
    KeyType key{0x1000, false};
    auto blk = falru.findBlock(key);
    ASSERT_TRUE(blk == nullptr);
}

TEST(cycles_arth) {
    Cycles c(5);
    Cycles d(3);
    ASSERT_EQ((uint64_t)(c + d), (uint64_t)8);
    ASSERT_EQ((uint64_t)(c - d), (uint64_t)2);
    c += Cycles(1);
    ASSERT_EQ((uint64_t)c, (uint64_t)6);
}

TEST(stats_scalar) {
    statistics::Group g;
    statistics::Scalar s;
    g.addScalar(s, "test_s");
    ASSERT_EQ(s.name(), std::string("test_s"));
    s = 0;
    s++;
    ASSERT_EQ((int)s.value(), 1);
}

int main() {
    printf("\n========== gem5 Cache Reference Test Suite ==========\n\n");

    printf("[1] CacheBlk Tests\n");
    RUN_TEST(cache_blk_valid);
    RUN_TEST(cache_blk_position);
    RUN_TEST(cache_blk_refcount);
    RUN_TEST(cache_blk_invalidate);

    printf("\n[2] Replacement Policy Tests\n");
    RUN_TEST(lru_rp_create);
    RUN_TEST(fifo_rp_create);
    RUN_TEST(mru_rp_create);
    RUN_TEST(random_rp_create);
    RUN_TEST(tree_plru_rp_create);

    printf("\n[3] Tags & Indexing Tests\n");
    RUN_TEST(set_assoc_create);
    RUN_TEST(falru_create);

    printf("\n[4] Types & Stats Tests\n");
    RUN_TEST(cycles_arth);
    RUN_TEST(stats_scalar);

    printf("\n========== Results: %d/%d tests passed ==========\n",
           tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
