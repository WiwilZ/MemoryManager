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
		std::byte buffer[sizeof(T)];
		union {
			void* mask; //用于已分配的内存块
			FreeBlock* next; //用于未分配的内存块
		};
	};

	struct Chunk {
		FreeBlock blocks[blocks_per_chunk];
		Chunk* next;

		constexpr explicit Chunk(Chunk* next = nullptr) noexcept: next(next) {
			auto it = blocks;
			it->mask = it->buffer; //第一个 block 用于分配
			for (++it; it != blocks + blocks_per_chunk - 1; ++it) {
				it->next = it + 1;
			}
			it->next = nullptr;
		}
	};

	Chunk* chunk_head{};
	FreeBlock* free_block_head{};

public:
	constexpr Allocator() noexcept = default;
	Allocator(const Allocator&) = delete;
	Allocator& operator=(const Allocator&) = delete;

	constexpr ~Allocator() noexcept {
		while (chunk_head) {
			delete[] std::exchange(chunk_head, chunk_head->next);
		}
	}

	[[nodiscard]] constexpr T* allocate() {
		if (free_block_head) {
			const auto next_block = free_block_head->next;
			free_block_head->mask = free_block_head->buffer;
			return reinterpret_cast<T*>(std::exchange(free_block_head, next_block));
		}

		chunk_head = new Chunk{ chunk_head };
		free_block_head = chunk_head->blocks + 1;
		return reinterpret_cast<T*>(chunk_head->blocks);
	}

	constexpr void deallocate(T* p) {
		const auto block = reinterpret_cast<FreeBlock*>(p);
		if (block->mask != p) {
			throw std::runtime_error("The pointer is not allocated from here.");
		}

		block->next = free_block_head;
		free_block_head = block;
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
