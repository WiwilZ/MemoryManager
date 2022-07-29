/*
 * Created by WiwilZ on 2022/6/8.
 */

#pragma once

#include <utility>
#include <cstring>


class MemoryPool {
	static constexpr size_t chunk_size = size_t{ 1 } << 12;

	struct Chunk {
		Chunk* next;
		std::byte buffer[chunk_size];
	};

	class FreeBlockList {
		static constexpr size_t base_size = 16;

		struct BlockNode {
			void* buffer;
			size_t size;

			[[nodiscard]] constexpr std::byte* begin() const noexcept {
				return static_cast<std::byte*>(buffer);
			}

			[[nodiscard]] constexpr std::byte* end() const noexcept {
				return begin() + size;
			}
		};

		BlockNode* list{};
		size_t count{};
		size_t capacity{};

	public:
		constexpr FreeBlockList() noexcept = default;

		constexpr ~FreeBlockList() noexcept {
			delete[](std::align_val_t{ sizeof(BlockNode) }, list);
		}


		[[nodiscard]] constexpr void* allocate(size_t size) noexcept {
			if (list == nullptr) {
				return nullptr;
			}

			const auto target = std::lower_bound(
					begin(), end(), BlockNode{ nullptr, size }, [](auto&& a, auto&& b) { return a.size < b.size; }
			);
			if (target == end()) {
				return nullptr;
			}

			if (target->size == size) {
				std::copy(target + 1, end(), target);
				--count;
			} else {
				Insert(begin(), target, { target->begin() + size, target->size - size });
			}
			return target->buffer;
		}

		constexpr void insert(BlockNode block) {
			if (list == nullptr) {
				capacity = base_size;
				list = new(std::align_val_t{ sizeof(BlockNode) }) BlockNode[capacity]{ block };
				count = 1;
				return;
			}

			if (count < capacity) {
				Insert(begin(), end(), block);
				++count;
				return;
			}

			capacity += capacity / 2;
			const auto new_list = new(std::align_val_t{ sizeof(BlockNode) }) BlockNode[capacity];
			auto new_count = 0;

			std::sort(begin(), end(), [](auto&& a, auto&& b) { return a.buffer < b.buffer; });
			auto it = begin();
			do {
				auto node = *it;
				for (++it; it != end() && it->begin() == node.end(); ++it) {
					node.size += it->size;
				}
				Insert(new_list, new_list + new_count, node);
				++new_count;
			} while (it != end());

			delete[](std::align_val_t{ sizeof(BlockNode) }, list);
			list = new_list;
			count = new_count;
		}

	private:
		[[nodiscard]] constexpr BlockNode* begin() const noexcept {
			return list;
		}

		[[nodiscard]] constexpr BlockNode* end() const noexcept {
			return list + count;
		}

		static constexpr void Insert(BlockNode* first, BlockNode* last, BlockNode block) noexcept {
			const auto index = std::upper_bound(first, last, block, [](auto&& a, auto&& b) { return a.size < b.size; });
			std::copy(index, last, index + 1);
			*index = block;
		}
	};


	Chunk* chunk_head{};
	FreeBlockList free_block_list{};

	constexpr MemoryPool() noexcept = default;

public:
	MemoryPool(const MemoryPool&) = delete;
	MemoryPool& operator=(const MemoryPool&) = delete;


	constexpr ~MemoryPool() noexcept {
		while (chunk_head != nullptr) {
			delete[] std::exchange(chunk_head, chunk_head->next);
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

		if (size >= chunk_size) {
			return new std::byte[size];
		}

		if (const auto ret = free_block_list.allocate(size); ret != nullptr) {
			return ret;
		}

		chunk_head = new Chunk{ chunk_head };
		free_block_list.insert({ chunk_head->buffer + size, chunk_size - size });
		return chunk_head->buffer;
	}

	constexpr void deallocate(void* p, size_t size) {
		if (p == nullptr || size == 0) {
			return;
		}

		if (size >= chunk_size) {
			operator delete[](p);
		} else {
			free_block_list.insert({ p, size });
		}
	}

	template <typename T>
	[[nodiscard]] constexpr T* allocate() {
		return static_cast<T*>(allocate(sizeof(T)));
	}

	template <typename T>
	constexpr void deallocate(T* p) {
		deallocate(p, sizeof(T));
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
