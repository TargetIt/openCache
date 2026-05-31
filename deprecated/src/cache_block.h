// =============================================================================
// Ported from GPGPU-Sim (gpgpu-sim_distribution)
//   gpu-cache.h:125-170  struct cache_block_t       → CacheBlock (abstract)
//   gpu-cache.h:172-283  struct line_cache_block    → LineCacheBlock
//   gpu-cache.h:285-512  struct sector_cache_block  → SectorCacheBlock
//
// Key modifications:
//   - Pure abstract base (GPGPU-Sim: non-abstract with bodies)
//   - Method naming: is_invalid_line()→is_invalid(), print_status()→print()
//   - get_modified_size() takes sector_size param (GPGPU-Sim: hardcoded SECTOR_SIZE)
//   - MAX_SECTORS=8 for future expandability (GPGPU-Sim: SECTOR_CHUNCK_SIZE=4)
//   - allocate_sector() added (ported from gpu-cache.h:360-384)
//     [v2 fix: preserves other sectors on SECTOR_MISS, Bug 4.1/C]
//   - Time types: uint64_t (GPGPU-Sim: unsigned / unsigned long long)
//   - set_byte_mask(mem_fetch*) overload removed (no mem_fetch dependency)
//   - create_cache_block() factory extracted from tag_array constructor
//   - Fixed typo: CHUNCK→CHUNK in DEFAULT_SECTOR_CHUNK_SIZE
// =============================================================================

#ifndef OPEN_CACHE_BLOCK_H
#define OPEN_CACHE_BLOCK_H

#include "open_cache_types.h"
#include "cache_config.h"
#include <cstdio>
#include <cassert>

namespace opencache {

// Abstract base class for cache blocks
class CacheBlock {
public:
    addr_t m_tag = 0;
    addr_t m_block_addr = 0;

    CacheBlock() = default;
    virtual ~CacheBlock() = default;

    virtual void allocate(addr_t tag, addr_t block_addr, uint64_t time,
                          sector_mask_t sector_mask) = 0;
    virtual void fill(uint64_t time, sector_mask_t sector_mask,
                      byte_mask_t byte_mask) = 0;

    virtual bool is_invalid() const = 0;
    virtual bool is_valid() const = 0;
    virtual bool is_reserved() const = 0;
    virtual bool is_modified() const = 0;

    virtual BlockState get_status(sector_mask_t sector_mask) const = 0;
    virtual void set_status(BlockState status, sector_mask_t sector_mask) = 0;

    virtual void set_dirty_byte_mask(byte_mask_t mask) = 0;
    virtual byte_mask_t get_dirty_byte_mask() const = 0;
    virtual sector_mask_t get_dirty_sector_mask() const = 0;

    virtual uint64_t get_last_access_time() const = 0;
    virtual void set_last_access_time(uint64_t time, sector_mask_t sector_mask) = 0;
    virtual uint64_t get_alloc_time() const = 0;

    virtual void set_ignore_on_fill(bool ignore, sector_mask_t sector_mask) = 0;
    virtual void set_modified_on_fill(bool modified, sector_mask_t sector_mask) = 0;
    virtual void set_readable_on_fill(bool readable, sector_mask_t sector_mask) = 0;
    virtual void set_byte_mask_on_fill(bool set) = 0;

    virtual uint32_t get_modified_size(uint32_t sector_size) const = 0;

    virtual void set_readable(bool readable, sector_mask_t sector_mask) = 0;
    virtual bool is_readable(sector_mask_t sector_mask) const = 0;

    virtual void print() const = 0;
};

// Line-based cache block (non-sector)
class LineCacheBlock : public CacheBlock {
public:
    LineCacheBlock() {
        m_alloc_time = 0;
        m_fill_time = 0;
        m_last_access_time = 0;
        m_status = BlockState::INVALID;
        m_ignore_on_fill = false;
        m_set_modified_on_fill = false;
        m_set_readable_on_fill = false;
        m_set_byte_mask_on_fill = false;
        m_readable = true;
        m_dirty_byte_mask.reset();
    }

    void allocate(addr_t tag, addr_t block_addr, uint64_t time,
                  sector_mask_t /*sector_mask*/) override {
        m_tag = tag;
        m_block_addr = block_addr;
        m_alloc_time = time;
        m_last_access_time = time;
        m_fill_time = 0;
        m_status = BlockState::RESERVED;
        m_ignore_on_fill = false;
        m_set_modified_on_fill = false;
        m_set_readable_on_fill = false;
        m_set_byte_mask_on_fill = false;
    }

    void fill(uint64_t time, sector_mask_t /*sector_mask*/,
              byte_mask_t byte_mask) override {
        m_status = m_set_modified_on_fill ? BlockState::MODIFIED : BlockState::VALID;
        if (m_set_readable_on_fill) m_readable = true;
        if (m_set_byte_mask_on_fill) set_dirty_byte_mask(byte_mask);
        m_fill_time = time;
    }

    bool is_invalid() const override { return m_status == BlockState::INVALID; }
    bool is_valid() const override { return m_status == BlockState::VALID; }
    bool is_reserved() const override { return m_status == BlockState::RESERVED; }
    bool is_modified() const override { return m_status == BlockState::MODIFIED; }

    BlockState get_status(sector_mask_t /*sector_mask*/) const override {
        return m_status;
    }

    void set_status(BlockState status, sector_mask_t /*sector_mask*/) override {
        m_status = status;
    }

    void set_dirty_byte_mask(byte_mask_t mask) override {
        m_dirty_byte_mask |= mask;
    }

    byte_mask_t get_dirty_byte_mask() const override {
        return m_dirty_byte_mask;
    }

    sector_mask_t get_dirty_sector_mask() const override {
        sector_mask_t sm;
        if (m_status == BlockState::MODIFIED) sm.set();
        return sm;
    }

    uint64_t get_last_access_time() const override { return m_last_access_time; }
    void set_last_access_time(uint64_t time, sector_mask_t /*sector_mask*/) override {
        m_last_access_time = time;
    }
    uint64_t get_alloc_time() const override { return m_alloc_time; }

    void set_ignore_on_fill(bool ignore, sector_mask_t /*sector_mask*/) override {
        m_ignore_on_fill = ignore;
    }
    void set_modified_on_fill(bool modified, sector_mask_t /*sector_mask*/) override {
        m_set_modified_on_fill = modified;
    }
    void set_readable_on_fill(bool readable, sector_mask_t /*sector_mask*/) override {
        m_set_readable_on_fill = readable;
    }
    void set_byte_mask_on_fill(bool set) override {
        m_set_byte_mask_on_fill = set;
    }

    uint32_t get_modified_size(uint32_t sector_size) const override {
        if (m_status == BlockState::MODIFIED) return 4 * sector_size;
        return 0;
    }

    void set_readable(bool readable, sector_mask_t /*sector_mask*/) override {
        m_readable = readable;
    }
    bool is_readable(sector_mask_t /*sector_mask*/) const override {
        return m_readable;
    }

    void print() const override {
        printf("  block_addr=0x%llx status=%d\n",
               (unsigned long long)m_block_addr, (int)m_status);
    }

private:
    uint64_t m_alloc_time;
    uint64_t m_last_access_time;
    uint64_t m_fill_time;
    BlockState m_status;
    bool m_ignore_on_fill;
    bool m_set_modified_on_fill;
    bool m_set_readable_on_fill;
    bool m_set_byte_mask_on_fill;
    bool m_readable;
    byte_mask_t m_dirty_byte_mask;
};

// Sector-based cache block
class SectorCacheBlock : public CacheBlock {
public:
    static constexpr unsigned MAX_SECTORS = 8; // max 8 sectors per line

    SectorCacheBlock(uint32_t num_sectors = DEFAULT_SECTOR_CHUNK_SIZE)
        : m_num_sectors(num_sectors) {
        init();
    }

    void init() {
        for (unsigned i = 0; i < MAX_SECTORS; ++i) {
            m_alloc_time[i] = 0;
            m_fill_time[i] = 0;
            m_last_access_time[i] = 0;
            m_status[i] = BlockState::INVALID;
            m_ignore_on_fill[i] = false;
            m_set_modified_on_fill[i] = false;
            m_set_readable_on_fill[i] = false;
            m_readable[i] = true;
        }
        m_line_alloc_time = 0;
        m_line_last_access_time = 0;
        m_line_fill_time = 0;
        m_set_byte_mask_on_fill = false;
        m_dirty_byte_mask.reset();
    }

    void allocate(addr_t tag, addr_t block_addr, uint64_t time,
                  sector_mask_t sector_mask) override {
        init();
        m_tag = tag;
        m_block_addr = block_addr;

        unsigned sidx = get_sector_index(sector_mask);
        m_alloc_time[sidx] = time;
        m_last_access_time[sidx] = time;
        m_fill_time[sidx] = 0;
        m_status[sidx] = BlockState::RESERVED;
        m_ignore_on_fill[sidx] = false;
        m_set_modified_on_fill[sidx] = false;
        m_set_readable_on_fill[sidx] = false;
        m_set_byte_mask_on_fill = false;

        m_line_alloc_time = time;
        m_line_last_access_time = time;
        m_line_fill_time = 0;
    }

    // Allocate a single sector within an already-valid line
    // (preserves other sectors — unlike allocate() which resets everything)
    void allocate_sector(uint64_t time, sector_mask_t sector_mask) {
        assert(is_valid()); // line must already be valid at the line level
        unsigned sidx = get_sector_index(sector_mask);

        m_alloc_time[sidx] = time;
        m_last_access_time[sidx] = time;
        m_fill_time[sidx] = 0;
        if (m_status[sidx] == BlockState::MODIFIED)
            m_set_modified_on_fill[sidx] = true;
        else
            m_set_modified_on_fill[sidx] = false;
        m_set_readable_on_fill[sidx] = false;
        m_status[sidx] = BlockState::RESERVED;
        m_ignore_on_fill[sidx] = false;
        m_readable[sidx] = true;

        m_line_last_access_time = time;
        m_line_fill_time = 0;
    }

    void fill(uint64_t time, sector_mask_t sector_mask,
              byte_mask_t byte_mask) override {
        for (unsigned i = 0; i < m_num_sectors; ++i) {
            if (sector_mask.test(i)) {
                m_status[i] = m_set_modified_on_fill[i] ?
                    BlockState::MODIFIED : BlockState::VALID;

                if (m_set_readable_on_fill[i]) {
                    m_readable[i] = true;
                    m_set_readable_on_fill[i] = false;
                }
                if (m_set_byte_mask_on_fill) set_dirty_byte_mask(byte_mask);

                m_fill_time[i] = time;
            }
        }
        m_line_fill_time = time;
    }

    bool is_invalid() const override {
        for (unsigned i = 0; i < m_num_sectors; ++i) {
            if (m_status[i] != BlockState::INVALID) return false;
        }
        return true;
    }
    bool is_valid() const override { return !is_invalid(); }
    bool is_reserved() const override {
        for (unsigned i = 0; i < m_num_sectors; ++i) {
            if (m_status[i] == BlockState::RESERVED) return true;
        }
        return false;
    }
    bool is_modified() const override {
        for (unsigned i = 0; i < m_num_sectors; ++i) {
            if (m_status[i] == BlockState::MODIFIED) return true;
        }
        return false;
    }

    BlockState get_status(sector_mask_t sector_mask) const override {
        unsigned sidx = get_sector_index(sector_mask);
        return m_status[sidx];
    }

    void set_status(BlockState status, sector_mask_t sector_mask) override {
        unsigned sidx = get_sector_index(sector_mask);
        m_status[sidx] = status;
    }

    void set_dirty_byte_mask(byte_mask_t mask) override {
        m_dirty_byte_mask |= mask;
    }

    byte_mask_t get_dirty_byte_mask() const override {
        return m_dirty_byte_mask;
    }

    sector_mask_t get_dirty_sector_mask() const override {
        sector_mask_t sm;
        for (unsigned i = 0; i < m_num_sectors; ++i) {
            if (m_status[i] == BlockState::MODIFIED) sm.set(i);
        }
        return sm;
    }

    uint64_t get_last_access_time() const override {
        return m_line_last_access_time;
    }

    void set_last_access_time(uint64_t time, sector_mask_t sector_mask) override {
        unsigned sidx = get_sector_index(sector_mask);
        m_last_access_time[sidx] = time;
        m_line_last_access_time = time;
    }

    uint64_t get_alloc_time() const override { return m_line_alloc_time; }

    void set_ignore_on_fill(bool ignore, sector_mask_t sector_mask) override {
        unsigned sidx = get_sector_index(sector_mask);
        m_ignore_on_fill[sidx] = ignore;
    }

    void set_modified_on_fill(bool modified, sector_mask_t sector_mask) override {
        unsigned sidx = get_sector_index(sector_mask);
        m_set_modified_on_fill[sidx] = modified;
    }

    void set_readable_on_fill(bool readable, sector_mask_t sector_mask) override {
        unsigned sidx = get_sector_index(sector_mask);
        m_set_readable_on_fill[sidx] = readable;
    }

    void set_byte_mask_on_fill(bool set) override {
        m_set_byte_mask_on_fill = set;
    }

    uint32_t get_modified_size(uint32_t sector_size) const override {
        uint32_t modified = 0;
        for (unsigned i = 0; i < m_num_sectors; ++i) {
            if (m_status[i] == BlockState::MODIFIED) modified++;
        }
        return modified * sector_size;
    }

    void set_readable(bool readable, sector_mask_t sector_mask) override {
        unsigned sidx = get_sector_index(sector_mask);
        m_readable[sidx] = readable;
    }

    bool is_readable(sector_mask_t sector_mask) const override {
        unsigned sidx = get_sector_index(sector_mask);
        return m_readable[sidx];
    }

    void print() const override {
        printf("  block_addr=0x%llx status=[%d,%d,%d,%d]\n",
               (unsigned long long)m_block_addr,
               (int)m_status[0], (int)m_status[1], (int)m_status[2], (int)m_status[3]);
    }

private:
    uint32_t m_num_sectors;

    uint64_t m_alloc_time[MAX_SECTORS];
    uint64_t m_last_access_time[MAX_SECTORS];
    uint64_t m_fill_time[MAX_SECTORS];
    BlockState m_status[MAX_SECTORS];
    bool m_ignore_on_fill[MAX_SECTORS];
    bool m_set_modified_on_fill[MAX_SECTORS];
    bool m_set_readable_on_fill[MAX_SECTORS];

    uint64_t m_line_alloc_time;
    uint64_t m_line_last_access_time;
    uint64_t m_line_fill_time;
    bool m_set_byte_mask_on_fill;
    bool m_readable[MAX_SECTORS];
    byte_mask_t m_dirty_byte_mask;

    unsigned get_sector_index(sector_mask_t sector_mask) const {
        assert(sector_mask.count() == 1);
        for (unsigned i = 0; i < m_num_sectors; ++i) {
            if (sector_mask.test(i)) return i;
        }
        return m_num_sectors; // error
    }
};

// Factory for creating appropriate cache blocks
inline CacheBlock *create_cache_block(CacheType type, uint32_t sector_chunk_size) {
    if (type == CacheType::SECTOR) {
        return new SectorCacheBlock(sector_chunk_size);
    } else {
        return new LineCacheBlock();
    }
}

} // namespace opencache

#endif // OPEN_CACHE_BLOCK_H
