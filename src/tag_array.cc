#include "tag_array.h"
#include <algorithm>
#include <cstring>

namespace opencache {

TagArray::TagArray(CacheConfig &config, int core_id, int type_id)
    : m_config(config), m_core_id(core_id), m_type_id(type_id) {

    m_num_lines = config.get_num_lines();
    m_lines.resize(m_num_lines, nullptr);

    for (uint32_t i = 0; i < m_num_lines; ++i) {
        m_lines[i] = create_cache_block(
            config.cache_type,
            config.sector_chunk_size);
    }

    // Initialize LRU order and FIFO next for each set
    m_lru_order.resize(config.num_sets);
    for (uint32_t s = 0; s < config.num_sets; ++s) {
        m_lru_order[s].resize(config.associativity);
        for (uint32_t w = 0; w < config.associativity; ++w) {
            m_lru_order[s][w] = w; // initially 0,1,2,...
        }
    }
    m_fifo_next.resize(config.num_sets, 0);
}

TagArray::~TagArray() {
    for (auto *line : m_lines) {
        delete line;
    }
}

int32_t TagArray::find_matching_way(uint32_t set_index, addr_t tag) const {
    for (uint32_t w = 0; w < m_config.associativity; ++w) {
        uint32_t idx = get_line_index(set_index, w);
        const auto *block = m_lines[idx];
        if (!block->is_invalid() && block->m_tag == tag) {
            return static_cast<int32_t>(w);
        }
    }
    return -1;
}

int32_t TagArray::find_invalid_way(uint32_t set_index) const {
    for (uint32_t w = 0; w < m_config.associativity; ++w) {
        uint32_t idx = get_line_index(set_index, w);
        if (m_lines[idx]->is_invalid()) {
            return static_cast<int32_t>(w);
        }
    }
    return -1;
}

void TagArray::lru_promote(uint32_t set_index, uint32_t way_index) {
    auto &order = m_lru_order[set_index];
    // Find way_index in the order list
    auto it = std::find(order.begin(), order.end(), way_index);
    if (it != order.end()) {
        order.erase(it);
    }
    // Push to front (MRU position)
    order.insert(order.begin(), way_index);
}

void TagArray::fifo_advance(uint32_t set_index) {
    m_fifo_next[set_index] = (m_fifo_next[set_index] + 1) % m_config.associativity;
}

uint32_t TagArray::find_victim(uint32_t set_index) const {
    if (m_config.replacement_policy == ReplacementPolicy::FIFO) {
        return m_fifo_next[set_index];
    } else if (m_config.replacement_policy == ReplacementPolicy::RANDOM) {
        return static_cast<uint32_t>(rand()) % m_config.associativity;
    } else if (m_config.replacement_policy == ReplacementPolicy::PLRU) {
        // Simple tree-PLRU approximation: return LRU way
        const auto &order = m_lru_order[set_index];
        return order.back(); // LRU is at the back
    } else {
        // Default: LRU
        const auto &order = m_lru_order[set_index];
        return order.back(); // LRU is at the back
    }
}

void TagArray::update_replacement_state(uint32_t set_index, uint32_t way_index,
                                         bool /*is_fill*/) {
    if (m_config.replacement_policy == ReplacementPolicy::LRU ||
        m_config.replacement_policy == ReplacementPolicy::PLRU) {
        lru_promote(set_index, way_index);
    } else if (m_config.replacement_policy == ReplacementPolicy::FIFO) {
        // FIFO only advances on fill, not on hit
        // (advance is called separately in access())
    }
}

TagProbeResult TagArray::probe(addr_t addr, bool is_write,
                                bool probe_mode) const {
    return probe(addr, m_config.get_sector_mask(addr), is_write, probe_mode);
}

TagProbeResult TagArray::probe(addr_t addr, sector_mask_t mask, bool is_write,
                                bool probe_mode) const {
    TagProbeResult result;
    result.status = AccessStatus::MISS;
    result.set_index = m_config.get_set_index(addr);
    result.way_index = 0;
    result.sector_index = 0;
    result.sector_mask = mask;

    addr_t tag = m_config.get_tag(addr);

    int32_t way = find_matching_way(result.set_index, tag);
    if (way >= 0) {
        uint32_t idx = get_line_index(result.set_index, static_cast<uint32_t>(way));
        result.way_index = static_cast<uint32_t>(way);
        const auto *block = m_lines[idx];

        if (m_config.cache_type == CacheType::SECTOR) {
            BlockState sector_status = block->get_status(mask);
            if (sector_status == BlockState::VALID ||
                sector_status == BlockState::MODIFIED) {
                if (block->is_readable(mask)) {
                    result.status = AccessStatus::HIT;
                } else {
                    result.status = AccessStatus::RESERVATION_FAIL;
                }
            } else if (sector_status == BlockState::RESERVED) {
                result.status = AccessStatus::HIT_RESERVED;
            } else {
                result.status = AccessStatus::SECTOR_MISS;
            }
        } else {
            BlockState line_status = block->get_status(mask);
            if (line_status == BlockState::VALID ||
                line_status == BlockState::MODIFIED) {
                result.status = AccessStatus::HIT;
            } else if (line_status == BlockState::RESERVED) {
                result.status = AccessStatus::HIT_RESERVED;
            } else {
                result.status = AccessStatus::MISS;
            }
        }
    }

    return result;
}

TagProbeResult TagArray::access(addr_t addr, uint64_t time, bool is_write) {
    bool wb;
    EvictedBlockInfo evicted;
    return access(addr, time, is_write, wb, evicted);
}

TagProbeResult TagArray::access(addr_t addr, uint64_t time, bool is_write,
                                 bool &wb, EvictedBlockInfo &evicted) {
    wb = false;
    m_accesses++;

    sector_mask_t smask = m_config.get_sector_mask(addr);

    TagProbeResult result;
    result.set_index = m_config.get_set_index(addr);
    result.way_index = 0;
    result.sector_index = 0;
    result.sector_mask = smask;

    addr_t tag = m_config.get_tag(addr);

    int32_t way = find_matching_way(result.set_index, tag);
    if (way >= 0) {
        // Cache hit
        uint32_t idx = get_line_index(result.set_index, static_cast<uint32_t>(way));
        result.way_index = static_cast<uint32_t>(way);
        auto *block = m_lines[idx];

        if (m_config.cache_type == CacheType::SECTOR) {
            BlockState sector_status = block->get_status(smask);
            if (sector_status == BlockState::VALID ||
                sector_status == BlockState::MODIFIED) {
                if (!block->is_readable(smask)) {
                    m_res_fails++;
                    result.status = AccessStatus::RESERVATION_FAIL;
                    return result;
                }
                result.status = AccessStatus::HIT;
                block->set_last_access_time(time, smask);
                update_replacement_state(result.set_index, result.way_index, false);
                return result;
            } else if (sector_status == BlockState::RESERVED) {
                m_pending_hits++;
                result.status = AccessStatus::HIT_RESERVED;
                return result;
            }
            // SECTOR_MISS: line tag matches but this sector is INVALID
            // Allocate just this sector — do NOT reset the whole line
            m_sector_misses++;
            m_misses++;
            result.status = AccessStatus::SECTOR_MISS;
            if (m_config.alloc_policy == AllocationPolicy::ON_MISS) {
                bool was_modified = block->is_modified();
                static_cast<SectorCacheBlock*>(block)->allocate_sector(time, smask);
                if (was_modified && !block->is_modified()) m_dirty--;
                if (m_config.replacement_policy == ReplacementPolicy::FIFO) {
                    fifo_advance(result.set_index);
                }
                update_replacement_state(result.set_index, result.way_index, true);
            }
            return result;
        } else {
            BlockState line_status = block->get_status(smask);
            if (line_status == BlockState::VALID ||
                line_status == BlockState::MODIFIED) {
                result.status = AccessStatus::HIT;
                block->set_last_access_time(time, smask);
                update_replacement_state(result.set_index, result.way_index, false);
                return result;
            } else if (line_status == BlockState::RESERVED) {
                m_pending_hits++;
                result.status = AccessStatus::HIT_RESERVED;
                return result;
            }
        }
    }

    // Cache miss
    m_misses++;

    // Find a victim or invalid way
    int32_t victim_way = find_invalid_way(result.set_index);
    if (victim_way < 0) {
        victim_way = static_cast<int32_t>(find_victim(result.set_index));
    }

    if (victim_way < 0) {
        m_res_fails++;
        result.status = AccessStatus::RESERVATION_FAIL;
        return result;
    }

    result.way_index = static_cast<uint32_t>(victim_way);
    uint32_t idx = get_line_index(result.set_index, result.way_index);
    auto *block = m_lines[idx];

    // Check if evicting a dirty block (check whole line)
    if (block->is_modified()) {
        wb = true;
        m_dirty--;
        evicted.block_addr = block->m_block_addr;
        evicted.modified_size = block->get_modified_size(m_config.sector_size);
        evicted.byte_mask = block->get_dirty_byte_mask();
        evicted.sector_mask = block->get_dirty_sector_mask();
    } else {
        wb = false;
    }

    // Allocate new block for this sector
    block->allocate(tag, m_config.get_block_addr(addr), time, smask);

    if (m_config.replacement_policy == ReplacementPolicy::FIFO) {
        fifo_advance(result.set_index);
    }
    update_replacement_state(result.set_index, result.way_index, true);

    result.status = AccessStatus::MISS;
    return result;
}

void TagArray::fill(addr_t addr, uint64_t time, bool is_write) {
    sector_mask_t smask = m_config.get_sector_mask(addr);
    byte_mask_t full_bytes;
    full_bytes.set();
    fill(addr, time, smask, full_bytes, is_write);
}

void TagArray::fill(uint32_t idx, uint64_t time, bool /*is_write*/) {
    // For line fill by index, set all sectors
    sector_mask_t all_sectors;
    all_sectors.set();
    byte_mask_t full_bytes;
    full_bytes.set();
    m_lines[idx]->fill(time, all_sectors, full_bytes);
}

void TagArray::fill(addr_t addr, uint64_t time, sector_mask_t mask,
                     byte_mask_t byte_mask, bool /*is_write*/) {
    uint32_t set_index = m_config.get_set_index(addr);
    addr_t tag = m_config.get_tag(addr);

    int32_t way = find_matching_way(set_index, tag);
    if (way >= 0) {
        uint32_t idx = get_line_index(set_index, static_cast<uint32_t>(way));
        bool was_modified = m_lines[idx]->is_modified();
        m_lines[idx]->fill(time, mask, byte_mask);
        if (m_lines[idx]->is_modified() && !was_modified) m_dirty++;
        return;
    }

    // ON_FILL: allocate first, then fill
    if (m_config.alloc_policy == AllocationPolicy::ON_FILL) {
        bool wb;
        EvictedBlockInfo evicted;
        TagProbeResult alloc = access(addr, time, false, wb, evicted);
        if (alloc.status != AccessStatus::RESERVATION_FAIL) {
            uint32_t idx = get_line_index(alloc.set_index, alloc.way_index);
            bool was_modified = m_lines[idx]->is_modified();
            m_lines[idx]->fill(time, mask, byte_mask);
            if (m_lines[idx]->is_modified() && !was_modified) m_dirty++;
        }
    }
}

void TagArray::flush() {
    for (auto *line : m_lines) {
        if (line->is_modified()) {
            // Write back dirty data
            line->set_status(BlockState::INVALID, sector_mask_t());
        }
    }
}

void TagArray::invalidate() {
    for (auto *line : m_lines) {
        line->set_status(BlockState::INVALID, sector_mask_t());
    }
    // Reset LRU/FIFO state
    for (uint32_t s = 0; s < m_config.num_sets; ++s) {
        for (uint32_t w = 0; w < m_config.associativity; ++w) {
            m_lru_order[s][w] = w;
        }
        m_fifo_next[s] = 0;
    }
}

void TagArray::get_stats(uint32_t &total_access, uint32_t &total_misses,
                          uint32_t &total_hit_res, uint32_t &total_res_fail) const {
    total_access = m_accesses;
    total_misses = m_misses;
    total_hit_res = m_pending_hits;
    total_res_fail = m_res_fails;
}

void TagArray::print(FILE *stream) const {
    fprintf(stream, "TagArray: %u lines, %u accesses, %u misses (%.2f%% hit rate), "
            "%u pending_hits, %u res_fail\n",
            m_num_lines, m_accesses, m_misses,
            m_accesses > 0 ? 100.0 * (m_accesses - m_misses) / m_accesses : 0.0,
            m_pending_hits, m_res_fails);
}

} // namespace opencache
