#!/bin/bash
sed -i '' 's/extreme_mshr_merge_refcount_spike/extreme_refcount_pure_miss/g' test/test_cache_whitebox.cc
sed -i '' 's/extreme_hit_queue_ready_queue_congestion/extreme_refcount_pure_hit/g' test/test_cache_whitebox.cc

awk '
/^TEST\(extreme_miss_queue_backpressure\)/ {
  print "TEST(extreme_refcount_mixed_miss_hit)"
  print "{"
  print "    cache_config cfg = make_config(\"N:1:64:1,L:R:m:N:L,A:64:64,64:0,1\");"
  print "    cfg.set_defer_hit_response(true);"
  print "    cfg.set_hit_response_queue_size(64);"
  print "    "
  print "    simple_mem_interface mem(128);"
  print "    gpgpu_sim gpu;"
  print "    read_only_cache cache(\"MaxMixRef\", cfg, 0, 0, &mem, IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);"
  print ""
  print "    mem_access_sector_mask_t sm; sm.set(0);"
  print "    std::vector<mem_fetch*> mfs;"
  print "    "
  print "    // 1. Issue 32 Pure Misses (1 allocates, 31 merge)"
  print "    for (int i = 0; i < 32; ++i) {"
  print "        mem_access_t acc(GLOBAL_ACC_R, 0x1000, 4, false, active_mask_t(), mem_access_byte_mask_t(), sm);"
  print "        warp_inst_t inst;"
  print "        mem_fetch* mf = new mem_fetch(acc, &inst, 0, 0, 0, 0, 0, NULL, 0);"
  print "        mfs.push_back(mf);"
  print "        std::list<cache_event> events;"
  print "        cache.access(mf->get_addr(), mf, i, events);"
  print "        cache.cycle();"
  print "    }"
  print "    "
  print "    // Fill the miss so they become pending ready responses (refcount = 32)"
  print "    mem_fetch* fetch_req = mem.queue.front();"
  print "    mem.queue.pop_front();"
  print "    cache.fill(fetch_req, 100);"
  print "    "
  print "    // 2. Issue 32 Pure Hits (since line is now valid, they go to hit queue)"
  print "    for (int i = 32; i < 64; ++i) {"
  print "        mem_access_t acc(GLOBAL_ACC_R, 0x1000, 64, false, active_mask_t(), mem_access_byte_mask_t(), sm);"
  print "        warp_inst_t inst;"
  print "        mem_fetch* mf = new mem_fetch(acc, &inst, 0, 0, 0, 0, 0, NULL, 0);"
  print "        mfs.push_back(mf);"
  print "        std::list<cache_event> events;"
  print "        enum cache_request_status s = cache.access(mf->get_addr(), mf, 100+i, events);"
  print "        CHECK_EQ(s, HIT);"
  print "    }"
  print "    "
  print "    cache.queue_watermarks();"
  print "    baseline_queue_watermark_stats watermarks = cache.queue_watermarks();"
  print "    CHECK_EQ(watermarks.line_refcount, 64u);"
  print "    "
  print "    while (cache.access_ready()) cache.next_access();"
  print "    for (auto mf : mfs) delete mf;"
  print "}"
  print ""
}
1' test/test_cache_whitebox.cc > tmp.cc && mv tmp.cc test/test_cache_whitebox.cc

sed -i '' '/RUN_TEST(extreme_miss_queue_backpressure);/i\
    RUN_TEST(extreme_refcount_mixed_miss_hit);' test/test_cache_whitebox.cc

