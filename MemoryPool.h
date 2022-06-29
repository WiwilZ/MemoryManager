/*
 * Created by WiwilZ on 2022/6/8.
 */

#pragma once

#include <utility>
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <cassert>


class MemoryPool {
	static constexpr size_t default_size = size_t{ 1 } << 12;

	struct UnfreeBlock {
		size_t is_last: 1;
		size_t is_free: 1;
		size_t is_prev_free: 1;
		size_t size: std::numeric_limits<size_t>::digits - 3;

		inline constexpr void set(bool _is_last, bool _is_free, bool _is_prev_free, size_t _size) noexcept {
			is_last = _is_last;
			is_free = _is_free;
			is_prev_free = _is_prev_free;
			size = _size;
		}
	};

	struct FreeBlock : UnfreeBlock {
		FreeBlock* prev;
		FreeBlock* next;
	};


	static inline FreeBlock* Next_neighbor(UnfreeBlock* block) noexcept {
		assert(!block->is_last);
		return reinterpret_cast<FreeBlock*>(reinterpret_cast<std::byte*>(block) + block->size);
	}

	static inline FreeBlock* Prev_neighbor(UnfreeBlock* block) noexcept {
		assert(block->is_prev_free);
		return *reinterpret_cast<FreeBlock**>(reinterpret_cast<std::byte*>(block) - free_block_footer_size);
	}

	static inline FreeBlock** Footer(FreeBlock* block) noexcept {
		return reinterpret_cast<FreeBlock**>(reinterpret_cast<std::byte*>(block) + block->size - free_block_footer_size);
	}

	static inline FreeBlock* Split_block(UnfreeBlock* block, size_t first_size) noexcept {
		return reinterpret_cast<FreeBlock*>(reinterpret_cast<std::byte*>(block) + first_size);
	}

	static inline void* Buffer(UnfreeBlock* block) noexcept {
		return reinterpret_cast<std::byte*>(block) + unfree_block_header_size;
	}


	static constexpr size_t unfree_block_header_size = sizeof(UnfreeBlock);
	static constexpr size_t free_block_header_size = sizeof(FreeBlock);
	static constexpr size_t free_block_footer_size = sizeof(FreeBlock*);
	static constexpr size_t free_block_tag_size = free_block_header_size + free_block_footer_size;
	static constexpr size_t chunk_header_size = sizeof(void*);

	void* chunk_head_{};
	FreeBlock* free_block_head_{};

	constexpr MemoryPool() noexcept = default;

public:
	MemoryPool(const MemoryPool&) = delete;
	constexpr MemoryPool& operator=(const MemoryPool&) = delete;


	constexpr ~MemoryPool() noexcept {
		for (auto p = chunk_head_; p; p = *reinterpret_cast<void**>(chunk_head_)) {
			operator delete[](p);
		}
	}

	static MemoryPool& instance() noexcept {
		static MemoryPool inst;
		return inst;
	}

public:
	[[nodiscard, gnu::alloc_size(2)]] constexpr void* allocate(size_t size) {
		if (size == 0) {
			return nullptr;
		}

		const auto alloc_size = std::max(unfree_block_header_size + size, free_block_tag_size);
		const auto alloc_size_last = std::max(unfree_block_header_size + size, free_block_header_size);

		for (auto curr = free_block_head_; curr; curr = curr->next) {
			if (const auto res = Alloc(curr, alloc_size, alloc_size_last); res != nullptr) {
				return res;
			}
		}

		const auto extend_size = chunk_header_size + alloc_size + free_block_header_size;
		const auto chunk_size = std::max(default_size, std::bit_ceil(extend_size));
		const auto p = new(std::align_val_t{ alignof(max_align_t) }) std::byte[chunk_size];

		*reinterpret_cast<void**>(p) = chunk_head_;
		chunk_head_ = p;

		const auto res = reinterpret_cast<UnfreeBlock*>(p + chunk_header_size);
		res->set(false, false, false, alloc_size);

		auto next_neighbor = Split_block(res, alloc_size);
		next_neighbor->set(true, true, false, chunk_size - chunk_header_size - alloc_size);
		Insert_block(next_neighbor);
		return Buffer(res);
	}

	[[nodiscard]] constexpr void* reallocate(void* p, size_t size) {
		if (p == nullptr) {
			return allocate(size);
		}

		auto block = reinterpret_cast<FreeBlock*>(static_cast<UnfreeBlock*>(p) - unfree_block_header_size);

		const auto payload_size = block->size - unfree_block_header_size;
		const auto alloc_size = std::max(unfree_block_header_size + size, free_block_tag_size);
		const auto alloc_size_last = std::max(unfree_block_header_size + size, free_block_header_size);

		block = Merge_next(block);
		if (const auto res = Alloc(block, alloc_size, alloc_size_last); res != nullptr) {
			return res;
		}

		if (const auto t = Merge_prev(block); block != t) {
			block = t;
			if (const auto res = Alloc(block, alloc_size, alloc_size_last); res != nullptr) {
				return memcpy(res, p, payload_size);
			}
		}

		Free(block);
		return memcpy(allocate(size), p, payload_size);
	}

	void deallocate(void* p) noexcept {
		if (p == nullptr) {
			return;
		}

		auto block = reinterpret_cast<FreeBlock*>(static_cast<UnfreeBlock*>(p) - unfree_block_header_size);
		block = Merge_next(block);
		block = Merge_prev(block);
		Free(block);
	}

private:
	constexpr void* Alloc(FreeBlock*& block, size_t alloc_size, size_t alloc_size_last) noexcept {
		if (!block->is_last) {
			const auto remain_size = static_cast<ssize_t>(block->size - alloc_size);
			if (block->size >= free_block_tag_size) {
				auto next_neighbor = Split_block(block, alloc_size);
				next_neighbor->set(false, true, false, remain_size);
				*Footer(next_neighbor) = next_neighbor;
				Insert_block(next_neighbor);

				Extract_block(block);
				block->is_free = false;
				block->size = alloc_size;
				return Buffer(block);
			}
			if (block->size >= 0) {
				Extract_block(block);
				block->is_free = false;
				Next_neighbor(block)->is_prev_free = false;
				return Buffer(block);
			}
		} else {
			const auto remain_size = static_cast<ssize_t>(block->size - alloc_size_last);
			if (block->size >= free_block_header_size) {
				auto next_neighbor = Split_block(block, alloc_size);
				next_neighbor->set(true, true, false, remain_size);
				Insert_block(next_neighbor);

				Extract_block(block);
				block->is_free = false;
				block->is_last = false;
				block->size = alloc_size;
				return Buffer(block);
			}
			if (block->size >= 0) {
				Extract_block(block);
				block->is_free = false;
				return Buffer(block);
			}
		}
		return nullptr;
	}

	constexpr void Free(FreeBlock*& block) noexcept {
		block->is_free = true;
		if (!block->is_last) {
			*Footer(block) = block;
			Next_neighbor(block)->is_prev_free = true;
		}
		Insert_block(block);
	}

	constexpr void Extract_block(FreeBlock*& block) noexcept {
		if (block->prev) {
			block->prev->next = block->next;
		} else {
			free_block_head_ = block->next;
		}
		if (block->next) {
			block->next->prev = block->prev;
		}
	}

	constexpr void Insert_block(FreeBlock*& block) noexcept {
		block->prev = nullptr;
		block->next = free_block_head_;
		if (free_block_head_) {
			free_block_head_->prev = block;
		}
		free_block_head_ = block;
	}

	constexpr FreeBlock* Merge_next(FreeBlock* block) noexcept {
		if (!block->is_last) {
			auto next_neighbor = Next_neighbor(block);
			if (next_neighbor->is_free) {
				Extract_block(next_neighbor);
				block->size += next_neighbor->size;
				block->is_last = next_neighbor->is_last;
			}
		}
		return block;
	}

	constexpr FreeBlock* Merge_prev(FreeBlock* block) noexcept {
		if (block->is_prev_free) {
			auto prev_neighbor = Prev_neighbor(block);
			Extract_block(prev_neighbor);
			block->size += prev_neighbor->size;
			block->is_prev_free = prev_neighbor->is_prev_free;
		}
		return block;
	}
};

