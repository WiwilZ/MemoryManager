/*
 * Created by WiwilZ on 2022/6/8.
 */

#pragma once

#include <utility>
#include <algorithm>
#include <bit>
#include <cstring>


/*
 * chunk: |next chunk ptr|block|...|block|
 * block meta: size is_last(1b) is_prev_free(1b) free(1b)
 * free block: |header(meta|mask)|payload|footer(header ptr)|
 * unfree block: |header(meta|mask)|payload|
 */
class MemoryPool {
    static constexpr size_t default_size = size_t{ 1 } << 12;

    struct BlockHeader;

    struct BlockFooter {
        BlockHeader* header;
    };

    struct BlockHeader {
        size_t meta;
        void* const mask{ payload };
        std::byte payload[0];

        [[nodiscard]] constexpr size_t block_size() const noexcept {
            return meta >> 3;
        }

        [[nodiscard]] constexpr size_t payload_size() const noexcept {
            return block_size() - block_footer_size;
        }

        [[nodiscard]] constexpr bool is_last() const noexcept {
            return (meta >> 2) & 1;
        }

        [[nodiscard]] constexpr bool is_prev_free() const noexcept {
            return (meta >> 1) & 1;
        }

        [[nodiscard]] constexpr bool is_free() const noexcept {
            return meta & 1;
        }

        constexpr void set_last() noexcept {
            meta |= 0b100;
        }

        constexpr void set_prev_free() noexcept {
            meta |= 0b10;
        }

        constexpr void set_free() noexcept {
            meta |= 1;
        }

        constexpr void set_not_last() noexcept {
            meta &= ~size_t{ 0b100 };
        }

        constexpr void set_prev_unfree() noexcept {
            meta &= ~size_t{ 0b10 };
        }

        constexpr void set_unfree() noexcept {
            meta &= ~size_t{ 1 };
        }

        [[nodiscard]] BlockFooter* footer() const noexcept {
            return (BlockFooter*) ((std::byte*) next_block() - block_footer_size);
        }

        [[nodiscard]] BlockHeader* next_block() const noexcept {
            return (BlockHeader*) ((std::byte*) this + block_size());
        }

        [[nodiscard]] BlockHeader* prev_block() const noexcept {
            return ((BlockFooter*) ((std::byte*) this - block_footer_size))->header;
        }

        constexpr void merge_next_block() noexcept {
            size_t blockSize = block_size();
            size_t isLast = meta & 0b100;
            if (!isLast && next_block()->is_free()) {
                blockSize += next_block()->block_size();
                isLast = next_block()->meta & 0b100;
            }
            meta = (blockSize << 3) | isLast | (meta & 0b10);
        }

        constexpr BlockHeader* merge_prev_block() noexcept {
            if (!is_prev_free()) {
                return this;
            }
            const auto block = prev_block();
            const size_t blockSize = block_size() + block->block_size();
            return new(block) BlockHeader{ (blockSize << 3) | (meta & 0b100) };
        }
    };

    struct ChunkHeader {
        ChunkHeader* next;
        std::byte payload[0];

        [[nodiscard]] BlockHeader* first_block() const noexcept {
            return (BlockHeader*) payload;
        }
    };

    struct ChunkFooter {
        ChunkHeader* next;
    };

    static constexpr size_t chunk_header_size = sizeof(ChunkHeader);
    static constexpr size_t chunk_footer_size = sizeof(ChunkFooter);
    static constexpr size_t chunk_tag_size = chunk_header_size + chunk_footer_size;

    static constexpr size_t block_header_size = sizeof(BlockHeader);
    static constexpr size_t block_footer_size = sizeof(BlockFooter);
    static constexpr size_t block_tag_size = block_header_size + block_footer_size;

    ChunkHeader* chunk_head_{};
    BlockHeader* curr_block_{};

public:
    MemoryPool(const MemoryPool&) = delete;

    constexpr ~MemoryPool() noexcept {
        while (chunk_head_) {
            delete std::exchange(chunk_head_, chunk_head_->next);
        }
    }

    static MemoryPool& instance() noexcept {
        static MemoryPool inst;
        return inst;
    }

    [[nodiscard]] void* allocate(size_t size) {
        if (size == 0) {
            return nullptr;
        }

        if (chunk_head_) {
            const auto start = curr_block_;
            const size_t allocSize = block_header_size + size;
            do {
                if (curr_block_->is_free()) {
                    if (auto res = Alloc_from_curr_block(allocSize); res != nullptr) {
                        return res;
                    }
                }

                if (curr_block_->is_last()) {
                    auto next_chunk = reinterpret_cast<ChunkFooter*>(curr_block_->next_block())->next;
                    if (!next_chunk) {
                        next_chunk = chunk_head_;
                    }
                    curr_block_ = next_chunk->first_block();
                } else {
                    curr_block_ = curr_block_->next_block();
                }
            } while (curr_block_ != start);
        }

        return Alloc_from_new_chunk(size);
    }

    [[nodiscard]] void* reallocate(void* p, size_t size) {
        if (p == nullptr) {
            return allocate(size);
        }

        const auto block = static_cast<BlockHeader*>(p) - 1;
        if (block->mask != p) {
            return nullptr;
        }

        const size_t payloadSize = block->block_size() - block_header_size;
        const size_t allocSize = block_header_size + size;

        curr_block_ = block;

        curr_block_->merge_next_block();
        if (auto res = Alloc_from_curr_block(allocSize); res != nullptr) {
            return res;
        }

        curr_block_ = curr_block_->merge_prev_block();
        if (curr_block_ != block) {
            if (auto res = Alloc_from_curr_block(allocSize); res != nullptr) {
                return memcpy(res, p, payloadSize);
            }
        }

        Free_curr_block();
        return memcpy(allocate(size), p, payloadSize);
    }

    void deallocate(void* p) noexcept {
        if (p == nullptr) {
            return;
        }

        const auto block = static_cast<BlockHeader*>(p) - 1;
        if (block->mask != p) {
            return;
        }

        block->merge_next_block();
        curr_block_ = block->merge_prev_block();
        Free_curr_block();
    }

private:
    constexpr MemoryPool() noexcept = default;

    void* Alloc_from_new_chunk(size_t size) {
        const size_t extend_size = chunk_tag_size + block_header_size + size + block_tag_size;
        const size_t chunk_size = std::max(default_size, size_t{ 1 } << std::bit_width(extend_size));

        auto p = new std::byte[chunk_size];
        chunk_head_ = new(p) ChunkHeader{ chunk_head_ };

        auto q = p + chunk_size - chunk_footer_size;
        new(q) ChunkFooter{ chunk_head_->next };

        const size_t alloc_size = block_header_size + size;
        auto target_block = new(chunk_head_->payload) BlockHeader{ alloc_size << 3 };

        p = reinterpret_cast<std::byte*>(target_block) + alloc_size;
        curr_block_ = new(p) BlockHeader{ (static_cast<size_t>(q - p) << 3) | 0b101 };
        curr_block_->footer()->header = curr_block_;

        return target_block->payload;
    }

    void* Alloc_from_curr_block(size_t allocSize) noexcept {
        const ssize_t freeSize = curr_block_->block_size() - allocSize;
        if (freeSize < 0) {
            return nullptr;
        }
        if (freeSize < block_tag_size) {
            curr_block_->set_unfree();
            return curr_block_->payload;
        }
        const size_t isLast = curr_block_->meta & 0b100;
        const size_t prevFree = curr_block_->meta & 0b10;
        auto targetBlock = new(curr_block_) BlockHeader{ (allocSize << 3) | prevFree };
        curr_block_ = new(targetBlock->next_block()) BlockHeader{ (freeSize << 3) | isLast | 1 };
        curr_block_->footer()->header = curr_block_;
        if (!isLast) {
            curr_block_->next_block()->set_prev_free();
        }
        return targetBlock->payload;
    }

    void Free_curr_block() noexcept {
        curr_block_->set_free();
        curr_block_->footer()->header = curr_block_;
        if (!curr_block_->is_last()) {
            curr_block_->next_block()->set_prev_free();
        }
    }
};

