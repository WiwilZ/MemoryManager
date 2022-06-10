/*
 * Created by WiwilZ on 2022/6/8.
 */

#pragma once

#include <utility>
#include <cstddef>

template <typename T>
class Allocator {
    static constexpr size_t block_size = sizeof(T);
    static constexpr size_t blocks_per_chunk = 128;

    struct FreeBlock {
        std::byte buffer[block_size];
        FreeBlock* next{};
    };

    struct Chunk {
        FreeBlock blocks[blocks_per_chunk];
        Chunk* next{};

        constexpr Chunk() noexcept {
            for (auto it = std::begin(blocks); it != std::end(blocks) - 1; ++it) {
                it->next = it + 1;
            }
        }
    };

public:
    constexpr Allocator() noexcept = default;
    constexpr Allocator(const Allocator&) noexcept = default;

    template <typename U>
    constexpr explicit Allocator(const Allocator<U>&) noexcept {}

    constexpr Allocator(Allocator&& al) noexcept
            : chunk_head_(std::exchange(al.chunk_head_, nullptr)),
              free_block_head_(std::exchange(al.free_block_head_, nullptr)) {}

    constexpr Allocator& operator=(const Allocator&) noexcept = delete;

    constexpr Allocator& operator=(Allocator&& al) noexcept {
        if (this != std::addressof(al)) {
            chunk_head_ = std::exchange(al.chunk_head_, nullptr);
            free_block_head_ = std::exchange(al.free_block_head_, nullptr);
        }
        return *this;
    }

    ~Allocator() {
        while (chunk_head_) {
            delete std::exchange(chunk_head_, chunk_head_->next);
        }
    }

    [[nodiscard]] T* allocate() {
        if (free_block_head_) {
            return reinterpret_cast<T*>(std::exchange(free_block_head_, free_block_head_->next));
        }

        auto chunk = new Chunk;
        chunk->next = chunk_head_;
        chunk_head_ = chunk;

        free_block_head_ = std::begin(chunk->blocks) + 1;
        return reinterpret_cast<T*>(std::begin(chunk->blocks));
    }

    void deallocate(T* p) noexcept {
        auto block = reinterpret_cast<FreeBlock*>(p);
        block->next = free_block_head_;
        free_block_head_ = block;
    }

private:
    Chunk* chunk_head_{};
    FreeBlock* free_block_head_{};
};




