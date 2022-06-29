/*
 * Created by WiwilZ on 2022/6/8.
 */

#pragma once

#include <utility>
#include <memory>


template <typename T>
class Allocator {
	static constexpr size_t blocks_per_chunk = size_t{ 1 } << 10;

	struct FreeBlock {
		std::byte payload[sizeof(T)];
		union {
			void* flag; //for allocated
			FreeBlock* next; //for free
		};
	};

	struct Chunk {
		FreeBlock blocks[blocks_per_chunk];
		Chunk* next;

		constexpr Chunk() noexcept {
			auto it = blocks;
			for (; it != blocks + blocks_per_chunk - 1; ++it) {
				it->next = it + 1;
			}
			it->next = nullptr;
		}
	};

	Chunk* chunk_head_{};
	FreeBlock* free_block_head_{};

public:
	constexpr Allocator() noexcept = default;
	Allocator(const Allocator&) = delete;
	Allocator& operator=(const Allocator&) = delete;

	constexpr ~Allocator() noexcept {
		while (chunk_head_) {
			delete[] std::exchange(chunk_head_, chunk_head_->next);
		}
	}

	[[nodiscard]] constexpr T* allocate() {
		if (free_block_head_) {
			const auto tmp = free_block_head_->next;
			free_block_head_->flag = free_block_head_->payload;
			return reinterpret_cast<T*>(std::exchange(free_block_head_, tmp));
		}

		const auto chunk = new Chunk;
		chunk->next = chunk_head_;
		chunk_head_ = chunk;

		free_block_head_ = chunk->blocks + 1;
		return reinterpret_cast<T*>(chunk->blocks);
	}

	constexpr void deallocate(T* p) noexcept {
		const auto block = reinterpret_cast<FreeBlock*>(p);
		if (block->flag != p) {
			return;
		}

		block->next = free_block_head_;
		free_block_head_ = block;
	}

	template <typename U, typename... Args>
	constexpr void construct(U* p, Args&& ... args) {
		std::uninitialized_construct_using_allocator(p, *this, std::forward<Args>(args)...);
	}

	template <typename U>
	constexpr void destroy(U* p) noexcept {
		p->~U();
	}
};
