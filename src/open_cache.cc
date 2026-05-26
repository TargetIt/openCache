#include "open_cache.h"
#include <cassert>

namespace opencache {

// ===== BaselineCache =====

BaselineCache::BaselineCache(const std::string &name, const CacheConfig &config,
                             int core_id, int type_id,
                             CacheMemoryInterface *memport,
                             CacheLevel level)
    : m_name(name), m_config(config),
      m_tag_array(new TagArray(m_config, core_id, type_id)),
      m_mshrs(m_config.mshr_entries, m_config.mshr_max_merge),
      m_level(level), m_memport(memport) {}

BaselineCache::~BaselineCache() {
    delete m_tag_array;
}

CacheResult BaselineCache::access(const CacheRequest &req) {
    TagProbeResult probe = m_tag_array->probe(req.address, req.is_write());
    m_stats.record_access(req.type, probe.status);

    CacheResult result;
    result.status = probe.status;
    result.is_hit = (probe.status == AccessStatus::HIT);
    result.latency = result.is_hit ? m_config.hit_latency : m_config.fill_latency;

    return result;
}

void BaselineCache::cycle() {
    replenish_ports();
}

void BaselineCache::fill(const CacheRequest &req, uint64_t time) {
    m_tag_array->fill(req.address, time, req.is_write());
    m_mshrs.mark_ready(m_config.get_block_addr(req.address));
}

void BaselineCache::replenish_ports() {
    if (m_data_port_occupied_cycles > 0) {
        m_data_port_occupied_cycles--;
        if (m_data_port_occupied_cycles == 0) {
            m_data_port_available = true;
        }
    }
    if (m_fill_port_occupied_cycles > 0) {
        m_fill_port_occupied_cycles--;
        if (m_fill_port_occupied_cycles == 0) {
            m_fill_port_available = true;
        }
    }
    m_stats.sample_port_utility(!m_data_port_available, !m_fill_port_available);
}

bool BaselineCache::miss_queue_full(uint32_t num_miss) const {
    return (m_miss_queue.size() + num_miss) >= m_config.miss_queue_size;
}

void BaselineCache::send_read_request(addr_t addr, addr_t block_addr,
                                       uint32_t cache_index,
                                       const CacheRequest &req,
                                       uint64_t /*time*/,
                                       bool /*read_only*/, bool /*wa*/,
                                       bool &do_miss,
                                       std::vector<CacheEvent> &events) {
    do_miss = true;

    if (m_mshrs.probe(block_addr)) {
        if (!m_mshrs.full(block_addr)) {
            m_mshrs.add(block_addr, req);
            do_miss = false;
            return;
        }
    }

    if (m_mshrs.full(block_addr)) {
        do_miss = false;
        m_stats.record_fail(req.type, ReservationFailReason::MSHR_ENTRY_FAIL);
        return;
    }

    m_mshrs.add(block_addr, req);

    ExtraFields ef;
    ef.valid = true;
    ef.block_addr = block_addr;
    ef.address = addr;
    ef.cache_index = cache_index;
    ef.data_size = req.size;
    m_extra_fields[&req] = ef;

    if (m_memport && m_memport->can_accept_request()) {
        m_memport->send_request(req);
        events.push_back(CacheEvent(CacheEventType::READ_REQUEST_SENT));
    }
}

void BaselineCache::print_stats(FILE *fp) const {
    m_stats.print(fp, m_name.c_str());
    if (fp) {
        fprintf(fp, "  MSHR: %zu active, %zu ready\n",
                m_mshrs.active_count(), m_mshrs.ready_count());
    }
}

void BaselineCache::print_config(FILE *fp) const {
    if (fp) {
        fprintf(fp, "=== %s Configuration ===\n", m_name.c_str());
        m_config.print(fp);
    }
}


// ===== ReadOnlyCache =====

ReadOnlyCache::ReadOnlyCache(const std::string &name, const CacheConfig &config,
                             int core_id, int type_id,
                             CacheMemoryInterface *memport,
                             CacheLevel level)
    : BaselineCache(name, config, core_id, type_id, memport, level) {
    assert(config.write_policy == WritePolicy::READ_ONLY);
}

CacheResult ReadOnlyCache::access(const CacheRequest &req) {
    uint64_t time = 0;

    TagProbeResult probe = m_tag_array->probe(req.address, false);
    m_stats.record_access(req.type, probe.status);

    CacheResult result;
    result.status = probe.status;
    result.is_hit = (probe.status == AccessStatus::HIT);
    result.latency = result.is_hit ? m_config.hit_latency : m_config.fill_latency;

    if (probe.status == AccessStatus::MISS ||
        probe.status == AccessStatus::SECTOR_MISS) {
        bool wb;
        EvictedBlockInfo evicted;
        TagProbeResult alloc = m_tag_array->access(req.address, time, false, wb, evicted);

        if (alloc.status == AccessStatus::RESERVATION_FAIL) {
            m_stats.record_fail(req.type, ReservationFailReason::LINE_ALLOC_FAIL);
            result.status = AccessStatus::RESERVATION_FAIL;
            result.is_hit = false;
            return result;
        }

        if (m_memport) {
            m_memport->send_request(req);
        }

        uint32_t flat_idx = alloc.set_index * m_config.associativity + alloc.way_index;
        m_tag_array->fill(flat_idx, time, false);
    }

    return result;
}


// ===== DataCache =====

DataCache::DataCache(const std::string &name, const CacheConfig &config,
                     int core_id, int type_id,
                     CacheMemoryInterface *memport,
                     CacheLevel level)
    : BaselineCache(name, config, core_id, type_id, memport, level) {
    assert(config.write_policy != WritePolicy::READ_ONLY &&
           "Use ReadOnlyCache for READ_ONLY policy");
}

CacheResult DataCache::access(const CacheRequest &req) {
    uint64_t time = 0;
    return process_access(req, time);
}

// Helper: compute flat index from probe result
static inline uint32_t flat_index(const TagProbeResult &p, uint32_t assoc) {
    return p.set_index * assoc + p.way_index;
}

CacheResult DataCache::process_access(const CacheRequest &req, uint64_t time) {
    bool is_write = req.is_write();

    if (is_write && req.type == AccessType::WRITE_BACK) {
        m_tag_array->fill(req.address, time, false);
        CacheResult result(AccessStatus::HIT, m_config.hit_latency);
        m_stats.record_access(req.type, AccessStatus::HIT);
        return result;
    }

    if (is_write) {
        TagProbeResult probe = m_tag_array->probe(req.address, true);
        m_stats.record_access(req.type, probe.status);

        if (probe.status == AccessStatus::HIT) {
            uint32_t fidx = flat_index(probe, m_config.associativity);
            switch (m_config.write_policy) {
                case WritePolicy::WRITE_BACK:
                    return write_hit_writeback(req, time, probe.set_index, probe.way_index, fidx);
                case WritePolicy::WRITE_THROUGH:
                    return write_hit_writethrough(req, time, probe.set_index, probe.way_index, fidx);
                case WritePolicy::WRITE_EVICT:
                    return write_hit_writeevict(req, time, probe.set_index, fidx);
                default:
                    return write_hit_writeback(req, time, probe.set_index, probe.way_index, fidx);
            }
        } else {
            switch (m_config.write_alloc_policy) {
                case WriteAllocatePolicy::NO_WRITE_ALLOCATE:
                    return write_miss_no_wa(req, time);
                case WriteAllocatePolicy::WRITE_ALLOCATE:
                    return write_miss_wa_naive(req, time);
                case WriteAllocatePolicy::FETCH_ON_WRITE:
                    return write_miss_wa_fetch_on_write(req, time);
                case WriteAllocatePolicy::LAZY_FETCH_ON_READ:
                    return write_miss_wa_lazy_fetch_on_read(req, time);
                default:
                    return write_miss_no_wa(req, time);
            }
        }
    } else {
        TagProbeResult probe = m_tag_array->probe(req.address, false);
        m_stats.record_access(req.type, probe.status);

        if (probe.status == AccessStatus::HIT) {
            uint32_t fidx = flat_index(probe, m_config.associativity);
            return read_hit(req, time, probe.set_index, probe.way_index, fidx);
        } else if (probe.status == AccessStatus::HIT_RESERVED) {
            return CacheResult(AccessStatus::HIT_RESERVED, m_config.hit_latency);
        } else {
            return read_miss(req, time, probe);
        }
    }
}

// ===== Write-hit handlers =====

CacheResult DataCache::write_hit_writeback(const CacheRequest &req, uint64_t time,
                                            uint32_t set_index, uint32_t way_index,
                                            uint32_t flat_idx) {
    sector_mask_t smask = m_config.get_sector_mask(req.address);
    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        block->set_status(BlockState::MODIFIED, smask);
        block->set_last_access_time(time, smask);
        byte_mask_t bm;
        bm.set();
        block->set_dirty_byte_mask(bm);
    }
    m_tag_array->update_replacement_state(set_index, way_index, false);
    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

CacheResult DataCache::write_hit_writethrough(const CacheRequest &req, uint64_t time,
                                               uint32_t set_index, uint32_t way_index,
                                               uint32_t flat_idx) {
    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_last_access_time(time, smask);
    }
    m_tag_array->update_replacement_state(set_index, way_index, false);

    if (m_memport) {
        m_memport->send_request(req);
    }

    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

CacheResult DataCache::write_hit_writeevict(const CacheRequest &req, uint64_t time,
                                             uint32_t set_index, uint32_t flat_idx) {
    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_status(BlockState::INVALID, smask);
    }

    if (m_memport) {
        m_memport->send_request(req);
    }

    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

// ===== Write-miss handlers =====

CacheResult DataCache::write_miss_no_wa(const CacheRequest &req, uint64_t /*time*/) {
    if (m_memport) {
        m_memport->send_request(req);
    }
    return CacheResult(AccessStatus::MISS, m_config.fill_latency);
}

CacheResult DataCache::write_miss_wa_naive(const CacheRequest &req, uint64_t time) {
    bool wb;
    EvictedBlockInfo evicted;
    TagProbeResult alloc = m_tag_array->access(req.address, time, true, wb, evicted);

    if (alloc.status == AccessStatus::RESERVATION_FAIL) {
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    // Fill line, then mark as modified
    uint32_t fidx = flat_index(alloc, m_config.associativity);
    m_tag_array->fill(fidx, time, false); // fill first to transition RESERVED→VALID
    auto *block = m_tag_array->get_block(fidx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_status(BlockState::MODIFIED, smask);
    }

    return CacheResult(AccessStatus::MISS, m_config.fill_latency);
}

CacheResult DataCache::write_miss_wa_fetch_on_write(const CacheRequest &req,
                                                      uint64_t time) {
    bool wb;
    EvictedBlockInfo evicted;
    TagProbeResult alloc = m_tag_array->access(req.address, time, true, wb, evicted);

    if (alloc.status == AccessStatus::RESERVATION_FAIL) {
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    // Fill line, then mark as modified (simulating fetch-then-write)
    uint32_t fidx = flat_index(alloc, m_config.associativity);
    m_tag_array->fill(fidx, time, false);
    auto *block = m_tag_array->get_block(fidx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_status(BlockState::MODIFIED, smask);
    }

    if (m_memport) {
        m_memport->send_request(CacheRequest(req.address, AccessType::READ, m_config.line_size));
    }

    return CacheResult(AccessStatus::MISS, m_config.fill_latency);
}

CacheResult DataCache::write_miss_wa_lazy_fetch_on_read(const CacheRequest &req,
                                                         uint64_t time) {
    bool wb;
    EvictedBlockInfo evicted;
    TagProbeResult alloc = m_tag_array->access(req.address, time, true, wb, evicted);

    if (alloc.status == AccessStatus::RESERVATION_FAIL) {
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    uint32_t fidx = flat_index(alloc, m_config.associativity);
    auto *block = m_tag_array->get_block(fidx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_readable_on_fill(false, smask);
        block->set_modified_on_fill(true, smask);
    }

    return CacheResult(AccessStatus::MISS, m_config.fill_latency);
}

// ===== Read handlers =====

CacheResult DataCache::read_hit(const CacheRequest &req, uint64_t time,
                                 uint32_t set_index, uint32_t way_index,
                                 uint32_t flat_idx) {
    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_last_access_time(time, smask);
    }
    m_tag_array->update_replacement_state(set_index, way_index, false);
    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

CacheResult DataCache::read_miss(const CacheRequest &req, uint64_t time,
                                  TagProbeResult &probe) {
    bool wb;
    EvictedBlockInfo evicted;
    TagProbeResult alloc = m_tag_array->access(req.address, time, false, wb, evicted);

    if (alloc.status == AccessStatus::RESERVATION_FAIL) {
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    if (m_memport) {
        m_memport->send_request(CacheRequest(req.address, AccessType::READ, m_config.line_size));
    }

    uint32_t fidx = flat_index(alloc, m_config.associativity);
    m_tag_array->fill(fidx, time + m_config.fill_latency, false);

    return CacheResult(AccessStatus::MISS, m_config.fill_latency);
}

} // namespace opencache
