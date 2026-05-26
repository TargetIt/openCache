#include "open_cache.h"
#include <cassert>

namespace opencache {

// Helper: compute flat index from probe result
static inline uint32_t flat_idx(const TagProbeResult &p, uint32_t assoc) {
    return p.set_index * assoc + p.way_index;
}

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
    // Drain miss queue: send pending requests to lower level
    if (!m_miss_queue.empty()) {
        CacheRequest &req = m_miss_queue.front();
        if (m_memport && m_memport->can_accept_request()) {
            m_memport->send_request(req);
            m_miss_queue.pop_front();
        }
    }
    replenish_ports();
}

void BaselineCache::fill(const CacheRequest &req, uint64_t time) {
    addr_t mshr_addr = m_config.get_mshr_addr(req.address);

    // SECTOR_ASSOC: track pending sector responses
    if (m_config.mshr_type == MSHRType::SECTOR_ASSOC) {
        auto it = m_extra_fields.find(mshr_addr);
        if (it != m_extra_fields.end() && it->second.valid) {
            it->second.pending_read--;
            if (it->second.pending_read > 0) {
                return; // wait for remaining sector responses
            }
        }
    }

    // Fill the tag array
    if (m_config.alloc_policy == AllocationPolicy::ON_MISS) {
        auto it = m_extra_fields.find(mshr_addr);
        if (it != m_extra_fields.end() && it->second.valid) {
            m_tag_array->fill(it->second.cache_index, time, false);
        }
    } else {
        // ON_FILL: tag_array fill auto-allocates
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        byte_mask_t full_bytes;
        full_bytes.set();
        m_tag_array->fill(req.address, time, smask, full_bytes, false);
    }

    m_mshrs.mark_ready(mshr_addr);

    // Clean up extra fields
    auto it = m_extra_fields.find(mshr_addr);
    if (it != m_extra_fields.end()) {
        m_extra_fields.erase(it);
    }

    // Record fill port usage
    use_fill_port(m_config.atom_size);
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

// Bandwidth management
void BaselineCache::use_data_port(uint32_t data_size, AccessStatus outcome) {
    uint32_t port_width = m_config.data_port_width;
    if (port_width == 0) port_width = m_config.line_size;

    switch (outcome) {
        case AccessStatus::HIT: {
            uint32_t data_cycles = data_size / port_width +
                ((data_size % port_width) > 0 ? 1 : 0);
            m_data_port_occupied_cycles += data_cycles;
            m_data_port_available = false;
            break;
        }
        default:
            break;
    }
}

void BaselineCache::use_fill_port(uint32_t atom_size) {
    uint32_t port_width = m_config.data_port_width;
    if (port_width == 0) port_width = m_config.line_size;
    uint32_t fill_cycles = atom_size / port_width +
        ((atom_size % port_width) > 0 ? 1 : 0);
    m_fill_port_occupied_cycles += fill_cycles;
    m_fill_port_available = false;
}

// ---- send_read_request (without writeback) ----
void BaselineCache::send_read_request(addr_t addr, addr_t block_addr,
                                       uint32_t cache_index,
                                       const CacheRequest &req,
                                       uint64_t time,
                                       bool &do_miss,
                                       std::vector<CacheEvent> &events,
                                       bool read_only, bool wa) {
    bool dummy_wb;
    EvictedBlockInfo dummy_evicted;
    send_read_request(addr, block_addr, cache_index, req, time,
                      do_miss, dummy_wb, dummy_evicted, events, read_only, wa);
}

// ---- send_read_request (with writeback tracking) ----
void BaselineCache::send_read_request(addr_t addr, addr_t block_addr,
                                       uint32_t cache_index,
                                       const CacheRequest &req,
                                       uint64_t time,
                                       bool &do_miss,
                                       bool &wb, EvictedBlockInfo &evicted,
                                       std::vector<CacheEvent> &events,
                                       bool read_only, bool wa) {
    do_miss = true;
    wb = false;

    addr_t mshr_addr = m_config.get_mshr_addr(addr);
    bool mshr_hit = m_mshrs.probe(mshr_addr);
    bool mshr_avail = !m_mshrs.full(mshr_addr);

    if (mshr_hit && mshr_avail) {
        // MSHR hit — merge with existing entry
        if (read_only)
            m_tag_array->access(block_addr, time, false);
        else
            m_tag_array->access(block_addr, time, true, wb, evicted);

        m_mshrs.add(mshr_addr, req);
        m_stats.record_access(req.type, AccessStatus::MSHR_HIT);
        do_miss = true;

    } else if (!mshr_hit && mshr_avail && !miss_queue_full(1)) {
        // New miss — allocate and queue
        TagProbeResult alloc;
        if (read_only)
            alloc = m_tag_array->access(block_addr, time, false);
        else
            alloc = m_tag_array->access(block_addr, time, true, wb, evicted);

        if (alloc.status == AccessStatus::RESERVATION_FAIL) {
            m_stats.record_fail(req.type, ReservationFailReason::LINE_ALLOC_FAIL);
            do_miss = false;
            return;
        }

        m_mshrs.add(mshr_addr, req);

        // Save extra fields for fill() lookup
        ExtraFields ef;
        ef.valid = true;
        ef.block_addr = block_addr;
        ef.address = addr;
        ef.cache_index = alloc.set_index * m_config.associativity + alloc.way_index;
        ef.data_size = req.size;
        ef.pending_read = (m_config.mshr_type == MSHRType::SECTOR_ASSOC) ? 1 : 0;
        m_extra_fields[mshr_addr] = ef;

        // Push to miss queue for later cycle() drain
        m_miss_queue.push_back(req);
        if (!wa) events.push_back(CacheEvent(CacheEventType::READ_REQUEST_SENT));
        do_miss = true;

    } else if (mshr_hit && !mshr_avail) {
        m_stats.record_fail(req.type, ReservationFailReason::MSHR_MERGE_ENTRY_FAIL);
        do_miss = false;
    } else if (!mshr_hit && !mshr_avail) {
        m_stats.record_fail(req.type, ReservationFailReason::MSHR_ENTRY_FAIL);
        do_miss = false;
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

    if (probe.status == AccessStatus::HIT) {
        use_data_port(req.size, AccessStatus::HIT);
        return CacheResult(AccessStatus::HIT, m_config.hit_latency);
    }

    if (probe.status == AccessStatus::RESERVATION_FAIL) {
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    // Miss or SECTOR_MISS: use send_read_request through MSHR + miss queue
    bool do_miss = false;
    std::vector<CacheEvent> events;
    addr_t block_addr = m_config.get_block_addr(req.address);

    send_read_request(req.address, block_addr,
                      0, // cache_index filled by send_read_request
                      req, time, do_miss, events, true, false);

    if (do_miss) {
        return CacheResult(AccessStatus::MISS, m_config.fill_latency);
    }
    return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
}


// ===== DataCache =====

DataCache::DataCache(const std::string &name, const CacheConfig &config,
                     int core_id, int type_id,
                     CacheMemoryInterface *memport,
                     CacheLevel level)
    : BaselineCache(name, config, core_id, type_id, memport, level) {
    assert(config.write_policy != WritePolicy::READ_ONLY &&
           "Use ReadOnlyCache for READ_ONLY policy");
    init_function_pointers();
}

void DataCache::init_function_pointers() {
    // Write-hit function
    switch (m_config.write_policy) {
        case WritePolicy::WRITE_BACK:
            m_wr_hit = &DataCache::wr_hit_wb; break;
        case WritePolicy::WRITE_THROUGH:
            m_wr_hit = &DataCache::wr_hit_wt; break;
        case WritePolicy::WRITE_EVICT:
            m_wr_hit = &DataCache::wr_hit_we; break;
        case WritePolicy::LOCAL_WB_GLOBAL_WT:
            m_wr_hit = &DataCache::wr_hit_global_we_local_wb; break;
        default:
            m_wr_hit = &DataCache::wr_hit_wb; break;
    }

    // Write-miss function
    switch (m_config.write_alloc_policy) {
        case WriteAllocatePolicy::NO_WRITE_ALLOCATE:
            m_wr_miss = &DataCache::wr_miss_no_wa; break;
        case WriteAllocatePolicy::WRITE_ALLOCATE:
            m_wr_miss = &DataCache::wr_miss_wa_naive; break;
        case WriteAllocatePolicy::FETCH_ON_WRITE:
            m_wr_miss = &DataCache::wr_miss_wa_fetch_on_write; break;
        case WriteAllocatePolicy::LAZY_FETCH_ON_READ:
            m_wr_miss = &DataCache::wr_miss_wa_lazy_fetch_on_read; break;
        default:
            m_wr_miss = &DataCache::wr_miss_no_wa; break;
    }

    // Read-hit function
    m_rd_hit = &DataCache::rd_hit_base;

    // Read-miss function
    m_rd_miss = &DataCache::rd_miss_base;
}

CacheResult DataCache::access(const CacheRequest &req) {
    uint64_t time = 0;

    // WRITE_BACK: fill directly into tag array without probe
    if (req.type == AccessType::WRITE_BACK) {
        m_tag_array->fill(req.address, time, false);
        m_stats.record_access(req.type, AccessStatus::HIT);
        return CacheResult(AccessStatus::HIT, m_config.hit_latency);
    }

    bool is_write = req.is_write();
    TagProbeResult probe = m_tag_array->probe(req.address, is_write);
    m_stats.record_access(req.type, probe.status);

    std::vector<CacheEvent> events;
    CacheResult result = process_tag_probe(is_write, probe, req, time, events);

    // Bandwidth management
    use_data_port(req.size, result.status);

    return result;
}

CacheResult DataCache::process_tag_probe(bool is_write, TagProbeResult &probe,
                                          const CacheRequest &req, uint64_t time,
                                          std::vector<CacheEvent> &events) {
    CacheResult result;

    if (is_write) {
        if (probe.status == AccessStatus::HIT) {
            uint32_t fidx = flat_idx(probe, m_config.associativity);
            result = (this->*m_wr_hit)(req, time, probe.set_index,
                                       probe.way_index, fidx, events);
        } else {
            result = (this->*m_wr_miss)(req, time, probe, events);
        }
    } else {
        if (probe.status == AccessStatus::HIT) {
            uint32_t fidx = flat_idx(probe, m_config.associativity);
            result = (this->*m_rd_hit)(req, time, probe.set_index,
                                       probe.way_index, fidx, events);
        } else if (probe.status == AccessStatus::HIT_RESERVED) {
            result = CacheResult(AccessStatus::HIT_RESERVED, m_config.hit_latency);
        } else {
            result = (this->*m_rd_miss)(req, time, probe, events);
        }
    }

    return result;
}

void DataCache::send_write_request(const CacheRequest &req,
                                    CacheEventType event_type,
                                    std::vector<CacheEvent> &events) {
    events.push_back(CacheEvent(event_type));
    m_miss_queue.push_back(req);
}

// ===== Write-hit handlers =====

CacheResult DataCache::wr_hit_wb(const CacheRequest &req, uint64_t time,
                                   uint32_t set_index, uint32_t way_index,
                                   uint32_t flat_idx,
                                   std::vector<CacheEvent> & /*events*/) {
    sector_mask_t smask = m_config.get_sector_mask(req.address);
    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        if (!block->is_modified()) {
            m_tag_array->inc_dirty();
        }
        block->set_status(BlockState::MODIFIED, smask);
        block->set_last_access_time(time, smask);
        block->set_dirty_byte_mask(req.byte_mask);
    }
    m_tag_array->update_replacement_state(set_index, way_index, false);
    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

CacheResult DataCache::wr_hit_wt(const CacheRequest &req, uint64_t time,
                                   uint32_t set_index, uint32_t way_index,
                                   uint32_t flat_idx,
                                   std::vector<CacheEvent> &events) {
    if (miss_queue_full(0)) {
        m_stats.record_fail(req.type, ReservationFailReason::MISS_QUEUE_FULL);
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        if (!block->is_modified()) {
            m_tag_array->inc_dirty();
        }
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_status(BlockState::MODIFIED, smask);
        block->set_dirty_byte_mask(req.byte_mask);
        block->set_last_access_time(time, smask);
    }
    m_tag_array->update_replacement_state(set_index, way_index, false);

    send_write_request(req, CacheEventType::WRITE_REQUEST_SENT, events);

    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

CacheResult DataCache::wr_hit_we(const CacheRequest &req, uint64_t /*time*/,
                                   uint32_t set_index, uint32_t /*way_index*/,
                                   uint32_t flat_idx,
                                   std::vector<CacheEvent> &events) {
    if (miss_queue_full(0)) {
        m_stats.record_fail(req.type, ReservationFailReason::MISS_QUEUE_FULL);
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_status(BlockState::INVALID, smask);
    }

    send_write_request(req, CacheEventType::WRITE_REQUEST_SENT, events);

    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

CacheResult DataCache::wr_hit_global_we_local_wb(const CacheRequest &req,
                                                   uint64_t time,
                                                   uint32_t set_index,
                                                   uint32_t way_index,
                                                   uint32_t flat_idx,
                                                   std::vector<CacheEvent> &events) {
    // GPGPU-Sim Fermi L1: global writes → write-evict, local writes → write-back
    if (req.is_global_access) {
        return wr_hit_we(req, time, set_index, way_index, flat_idx, events);
    } else {
        return wr_hit_wb(req, time, set_index, way_index, flat_idx, events);
    }
}

// ===== Write-miss handlers =====

CacheResult DataCache::wr_miss_no_wa(const CacheRequest &req, uint64_t /*time*/,
                                       TagProbeResult & /*probe*/,
                                       std::vector<CacheEvent> &events) {
    send_write_request(req, CacheEventType::WRITE_REQUEST_SENT, events);
    return CacheResult(AccessStatus::MISS, m_config.fill_latency);
}

CacheResult DataCache::wr_miss_wa_naive(const CacheRequest &req, uint64_t time,
                                          TagProbeResult &probe,
                                          std::vector<CacheEvent> &events) {
    addr_t mshr_addr = m_config.get_mshr_addr(req.address);
    addr_t block_addr = m_config.get_block_addr(req.address);
    bool mshr_hit = m_mshrs.probe(mshr_addr);
    bool mshr_avail = !m_mshrs.full(mshr_addr);

    if (miss_queue_full(2) ||
        (!(mshr_hit && mshr_avail) &&
         !(!mshr_hit && mshr_avail && !miss_queue_full(1)))) {
        if (miss_queue_full(2))
            m_stats.record_fail(req.type, ReservationFailReason::MISS_QUEUE_FULL);
        else if (mshr_hit && !mshr_avail)
            m_stats.record_fail(req.type, ReservationFailReason::MSHR_MERGE_ENTRY_FAIL);
        else if (!mshr_hit && !mshr_avail)
            m_stats.record_fail(req.type, ReservationFailReason::MSHR_ENTRY_FAIL);
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    // Send original write request
    send_write_request(req, CacheEventType::WRITE_REQUEST_SENT, events);

    // Allocate line and send read request
    bool do_miss = false;
    bool wb = false;
    EvictedBlockInfo evicted;
    send_read_request(req.address, block_addr, 0, req, time,
                      do_miss, wb, evicted, events, false, true);

    events.push_back(CacheEvent(CacheEventType::WRITE_ALLOCATE_SENT));

    if (do_miss) {
        // Handle writeback from evicted dirty block
        if (wb && m_config.write_policy != WritePolicy::WRITE_THROUGH) {
            CacheRequest wb_req(evicted.block_addr, AccessType::WRITE_BACK,
                                evicted.modified_size,
                                req.stream_id, req.instruction_id);
            wb_req.byte_mask = evicted.byte_mask;
            wb_req.sector_mask = evicted.sector_mask;
            send_write_request(wb_req, CacheEventType::WRITE_BACK_REQUEST_SENT, events);
        }
        return CacheResult(AccessStatus::MISS, m_config.fill_latency);
    }

    return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
}

CacheResult DataCache::wr_miss_wa_fetch_on_write(const CacheRequest &req,
                                                   uint64_t time,
                                                   TagProbeResult &probe,
                                                   std::vector<CacheEvent> &events) {
    addr_t block_addr = m_config.get_block_addr(req.address);
    addr_t mshr_addr = m_config.get_mshr_addr(req.address);

    // Full-line write optimization: if writing entire atom, no read needed
    uint32_t byte_count = static_cast<uint32_t>(req.byte_mask.count());
    if (byte_count >= m_config.atom_size) {
        if (miss_queue_full(0)) {
            m_stats.record_fail(req.type, ReservationFailReason::MISS_QUEUE_FULL);
            return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
        }

        bool wb = false;
        EvictedBlockInfo evicted;
        TagProbeResult alloc = m_tag_array->access(
            block_addr, time, true, wb, evicted);

        if (alloc.status == AccessStatus::RESERVATION_FAIL) {
            return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
        }

        uint32_t fidx = flat_idx(alloc, m_config.associativity);
        auto *block = m_tag_array->get_block(fidx);
        if (block) {
            if (!block->is_modified()) {
                m_tag_array->inc_dirty();
            }
            sector_mask_t smask = m_config.get_sector_mask(req.address);
            block->set_status(BlockState::MODIFIED, smask);
            block->set_dirty_byte_mask(req.byte_mask);
            if (alloc.status == AccessStatus::HIT_RESERVED) {
                block->set_ignore_on_fill(true, smask);
            }
        }

        // Handle writeback
        if (wb && m_config.write_policy != WritePolicy::WRITE_THROUGH) {
            CacheRequest wb_req(evicted.block_addr, AccessType::WRITE_BACK,
                                evicted.modified_size,
                                req.stream_id, req.instruction_id);
            wb_req.byte_mask = evicted.byte_mask;
            wb_req.sector_mask = evicted.sector_mask;
            send_write_request(wb_req, CacheEventType::WRITE_BACK_REQUEST_SENT, events);
        }

        return CacheResult(AccessStatus::MISS, m_config.fill_latency);
    }

    // Partial write: need to fetch the line first
    bool mshr_hit = m_mshrs.probe(mshr_addr);
    bool mshr_avail = !m_mshrs.full(mshr_addr);

    if (miss_queue_full(1) ||
        (!(mshr_hit && mshr_avail) &&
         !(!mshr_hit && mshr_avail && !miss_queue_full(1)))) {
        if (miss_queue_full(1))
            m_stats.record_fail(req.type, ReservationFailReason::MISS_QUEUE_FULL);
        else if (mshr_hit && !mshr_avail)
            m_stats.record_fail(req.type, ReservationFailReason::MSHR_MERGE_ENTRY_FAIL);
        else if (!mshr_hit && !mshr_avail)
            m_stats.record_fail(req.type, ReservationFailReason::MSHR_ENTRY_FAIL);
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    // Prevent Write-After-Read hazard in pending MSHR
    if (m_mshrs.probe(mshr_addr) &&
        m_mshrs.is_read_after_write_pending(mshr_addr) && req.is_write()) {
        m_stats.record_fail(req.type, ReservationFailReason::MSHR_RW_PENDING);
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    bool do_miss = false;
    bool wb = false;
    EvictedBlockInfo evicted;
    send_read_request(req.address, block_addr, 0, req, time,
                      do_miss, wb, evicted, events, false, true);

    if (do_miss) {
        if (wb && m_config.write_policy != WritePolicy::WRITE_THROUGH) {
            CacheRequest wb_req(evicted.block_addr, AccessType::WRITE_BACK,
                                evicted.modified_size,
                                req.stream_id, req.instruction_id);
            wb_req.byte_mask = evicted.byte_mask;
            wb_req.sector_mask = evicted.sector_mask;
            send_write_request(wb_req, CacheEventType::WRITE_BACK_REQUEST_SENT, events);
        }
        return CacheResult(AccessStatus::MISS, m_config.fill_latency);
    }

    return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
}

CacheResult DataCache::wr_miss_wa_lazy_fetch_on_read(const CacheRequest &req,
                                                       uint64_t time,
                                                       TagProbeResult &probe,
                                                       std::vector<CacheEvent> &events) {
    addr_t block_addr = m_config.get_block_addr(req.address);

    bool wb = false;
    EvictedBlockInfo evicted;
    TagProbeResult alloc = m_tag_array->access(
        block_addr, time, true, wb, evicted);

    if (alloc.status == AccessStatus::RESERVATION_FAIL) {
        return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
    }

    uint32_t fidx = flat_idx(alloc, m_config.associativity);
    auto *block = m_tag_array->get_block(fidx);
    if (block) {
        if (!block->is_modified()) {
            m_tag_array->inc_dirty();
        }
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_status(BlockState::MODIFIED, smask);
        block->set_dirty_byte_mask(req.byte_mask);

        // If writing full atom, mark readable; otherwise mark not-readable → fetch on read
        uint32_t byte_count = static_cast<uint32_t>(req.byte_mask.count());
        if (byte_count >= m_config.atom_size) {
            block->set_readable(true, smask);
        } else {
            block->set_readable(false, smask);
        }

        if (alloc.status == AccessStatus::HIT_RESERVED) {
            block->set_ignore_on_fill(true, smask);
        }
    }

    // Handle writeback
    if (wb && m_config.write_policy != WritePolicy::WRITE_THROUGH) {
        CacheRequest wb_req(evicted.block_addr, AccessType::WRITE_BACK,
                            evicted.modified_size,
                            req.stream_id, req.instruction_id);
        wb_req.byte_mask = evicted.byte_mask;
        wb_req.sector_mask = evicted.sector_mask;
        send_write_request(wb_req, CacheEventType::WRITE_BACK_REQUEST_SENT, events);
    }

    return CacheResult(AccessStatus::MISS, m_config.fill_latency);
}

// ===== Read handlers =====

CacheResult DataCache::rd_hit_base(const CacheRequest &req, uint64_t time,
                                     uint32_t set_index, uint32_t way_index,
                                     uint32_t flat_idx,
                                     std::vector<CacheEvent> & /*events*/) {
    auto *block = m_tag_array->get_block(flat_idx);
    if (block) {
        sector_mask_t smask = m_config.get_sector_mask(req.address);
        block->set_last_access_time(time, smask);
    }
    m_tag_array->update_replacement_state(set_index, way_index, false);
    return CacheResult(AccessStatus::HIT, m_config.hit_latency);
}

CacheResult DataCache::rd_miss_base(const CacheRequest &req, uint64_t time,
                                      TagProbeResult &probe,
                                      std::vector<CacheEvent> &events) {
    addr_t block_addr = m_config.get_block_addr(req.address);

    bool do_miss = false;
    bool wb = false;
    EvictedBlockInfo evicted;

    send_read_request(req.address, block_addr, 0, req, time,
                      do_miss, wb, evicted, events, false, false);

    if (do_miss) {
        // Handle writeback from evicted dirty block
        if (wb && m_config.write_policy != WritePolicy::WRITE_THROUGH) {
            CacheRequest wb_req(evicted.block_addr, AccessType::WRITE_BACK,
                                evicted.modified_size,
                                req.stream_id, req.instruction_id);
            wb_req.byte_mask = evicted.byte_mask;
            wb_req.sector_mask = evicted.sector_mask;
            send_write_request(wb_req, CacheEventType::WRITE_BACK_REQUEST_SENT, events);
        }
        return CacheResult(AccessStatus::MISS, m_config.fill_latency);
    }

    return CacheResult(AccessStatus::RESERVATION_FAIL, 0);
}

} // namespace opencache
