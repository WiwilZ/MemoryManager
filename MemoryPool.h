/*
 * Created by WiwilZ on 2022/6/8.
 */

#pragma once

#include <utility>
#include <algorithm>
#include <bit>


//tag: size prev_free(1b) free(1b)
//|ptr(next chunk)|...|tag(free)|buffer|tag(free)|...|tag(unfree)|buffer|0|ptr(next chunk)|
class MemoryPool {
    static constexpr size_t default_size = 1024;

    using chunk_tag_t = std::byte*;
    using block_tag_t = size_t;
    using block_t = block_tag_t*;

    static constexpr size_t chunk_tag_size = sizeof(chunk_tag_t);
    static constexpr size_t block_tag_size = sizeof(block_tag_t);

    chunk_tag_t chunk_head_{ nullptr };
    block_t curr_block_{ nullptr };

public:
    MemoryPool(const MemoryPool&) noexcept = delete;
    MemoryPool(MemoryPool&&) noexcept = delete;
    MemoryPool& operator=(const MemoryPool&) noexcept = delete;
    MemoryPool& operator=(MemoryPool&&) noexcept = delete;

    ~MemoryPool() {
        while (chunk_head_) {
            delete std::exchange(chunk_head_, *reinterpret_cast<chunk_tag_t*>(chunk_head_));
        }
    }

    static MemoryPool& instance() {
        static MemoryPool inst;
        return inst;
    }

    void* allocate(size_t size) {
        if (size == 0) {
            return nullptr;
        }

        Find_free_block(size);
        return Alloc_split_move_cur_block(size);
    }

    void deallocate(void* p) noexcept {
        curr_block_ = static_cast<block_t>(p) - 1;
        size_t block_size = *curr_block_ >> 2;

        auto p_curr_block = reinterpret_cast<std::byte*>(curr_block_);

        auto next_block = reinterpret_cast<block_t>(p_curr_block + block_size);
        auto tail = next_block - 1;

        if (*next_block & 1) {
            block_size += *next_block >> 2;
            tail = reinterpret_cast<block_t>(p_curr_block + block_size) - 1;
        }
        if ((*curr_block_ >> 1) & 1) {
            size_t prev_block_size = *(curr_block_ - 1) >> 2;
            block_size += prev_block_size;
            curr_block_ = reinterpret_cast<block_t>(p_curr_block - prev_block_size);
        }

        *tail = (block_size << 2) ^ 1;
        if ((*curr_block_ >> 1) & 1) {
            *tail ^= 2;
        }
        *curr_block_ = *tail;
    }

private:
    constexpr MemoryPool() noexcept = default;

    void Find_free_block(size_t need_size) noexcept {
        if (!chunk_head_) {
            return New_chunk(need_size);
        }

        auto start = curr_block_;
        while (!(*curr_block_ & 1) || (*curr_block_ >> 2) - block_tag_size < need_size) {
            curr_block_ = reinterpret_cast<block_t>(reinterpret_cast<std::byte*>(curr_block_) + (*curr_block_ >> 2));
            if (*curr_block_ == 0) {
                Move_cur_block_in_last_to_next();
            }
            if (curr_block_ == start) {
                return New_chunk(need_size);
            }
        }
    }

    void New_chunk(size_t need_size) {
        const size_t chunk_size = std::max(
                default_size, size_t{ 1 } << std::bit_width(2 * (chunk_tag_size + block_tag_size) + need_size)
        );

        auto p = new std::byte[chunk_size];

        auto head = reinterpret_cast<chunk_tag_t*>(p);
        auto tail = reinterpret_cast<chunk_tag_t*>(p + chunk_size) - 1;
        *head = *tail = chunk_head_;
        chunk_head_ = p;

        p += sizeof(chunk_tag_t);
        curr_block_ = reinterpret_cast<block_t>(p);
        size_t block_size = chunk_size - 2 * chunk_tag_size - block_tag_size;
        *curr_block_ = block_size << 2;

        *reinterpret_cast<block_t>(p + block_size) = 0;
    }

    [[nodiscard]] void* Alloc_split_move_cur_block(size_t need_size) noexcept {
        auto res_block = curr_block_;
        size_t block_size = *curr_block_ >> 2;
        // next block
        auto p = reinterpret_cast<std::byte*>(curr_block_) + block_size;
        curr_block_ = reinterpret_cast<block_t>(p);
        // |tag(unfree)|.............buffer..............|
        //             |......|tag(free)|buffer|tag(free)|
        if (block_size - 3 * block_tag_size - need_size < 0) {
            *res_block &= ~size_t{ 1 };  //最低位置0

            if (*curr_block_ == 0) {
                Move_cur_block_in_last_to_next();
            } else {
                *curr_block_ &= ~size_t{ 2 };  //次低位置0
                if (*curr_block_ & 1) {  //free block有两个tag
                    *(reinterpret_cast<block_t>(p + (*curr_block_ >> 2)) - 1) = *curr_block_;
                }
            }
        } else {
            size_t res_block_size = block_tag_size + need_size;
            bool prev_free = (*res_block >> 1) & 1;
            *res_block = res_block_size << 2;
            if (prev_free) {
                *res_block ^= 2;
            }

            auto tail = curr_block_ - 1;
            block_size -= res_block_size;
            curr_block_ = reinterpret_cast<block_t>(p - block_size);
            *curr_block_ = *tail = (block_size << 2) ^ 1;
        }
        return res_block + 1;
    }

    void Move_cur_block_in_last_to_next() noexcept {
        auto next_chunk = *reinterpret_cast<chunk_tag_t*>(curr_block_ + 1);
        if (!next_chunk) {
            next_chunk = chunk_head_;
        }
        curr_block_ = reinterpret_cast<block_t>(next_chunk + chunk_tag_size);
    }
};

