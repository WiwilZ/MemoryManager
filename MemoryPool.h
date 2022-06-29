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
		std::byte payload[chunk_size];

		constexpr Chunk(Chunk* next = nullptr) noexcept: next(next) {}
	};

	class FreeBlock {
		static constexpr size_t base_size = 16;

		struct Block {
			void* payload;
			size_t size;

			[[nodiscard]] inline constexpr std::byte* begin() const noexcept {
				return static_cast<std::byte*>(payload);
			}

			[[nodiscard]] inline constexpr std::byte* end() const noexcept {
				return begin() + size;
			}

			friend constexpr inline bool operator<(Block lhs, Block rhs) noexcept {
				return lhs.size < rhs.size;
			}
		};

		Block* list{};
		size_t size{};
		size_t capacity{};

	public:
		constexpr FreeBlock() noexcept = default;

		constexpr ~FreeBlock() noexcept {
			delete[](std::align_val_t{ sizeof(Block) }, list);
		}


		[[nodiscard]] constexpr void* allocate(size_t block_size) noexcept {
			if (list == nullptr) {
				return nullptr;
			}

			const auto target = std::lower_bound(begin(), end(), Block{ nullptr, block_size });

			if (target == end()) {
				return nullptr;
			}

			if (target->size == block_size) {
				Memory_move(target + 1, end(), target);
				--size;
			} else {
				const auto new_block = Block{ target->begin() + block_size, target->size - block_size };
				const auto new_index = std::upper_bound(begin(), target, new_block);
				Memory_move(new_index, target, new_index + 1);
				*new_index = new_block;
			}
			return target->payload;
		}

		constexpr void insert(Block block) {
			if (list == nullptr) {
				capacity = base_size;
				list = new(std::align_val_t{ sizeof(Block) }) Block[capacity]{ block };
				size = 1;
				return;
			}

			const auto target = std::find_if(
					begin(), end(), [&](auto e) { return e.end() == block.begin() || block.end() == e.begin(); }
			);

			if (target == end()) {
				if (capacity == size) {
					capacity += capacity / 2;
					const auto new_list = new(std::align_val_t{ sizeof(Block) }) Block[capacity];
					const auto index = std::upper_bound(begin(), end(), block);
					const auto p = Memory_copy(begin(), index, new_list);
					*p = block;
					Memory_copy(index, end(), p + 1);
					delete[](std::align_val_t{ sizeof(Block) }, list);
					list = new_list;
				} else {
					const auto index = std::upper_bound(begin(), end(), block);
					Memory_move(index, end(), index + 1);
					*index = block;
				}
				++size;
				return;
			}

			if (target->end() == block.begin()) {
				const auto new_target = std::find_if(
						target + 1, end(), [&](auto e) { return block.end() == e.begin(); }
				);
				if (new_target == end()) {
					Insert({ target->payload, target->size + block.size }, target);
				} else {
					Insert({ target->payload, target->size + block.size + new_target->size }, target, new_target);
				}
			} else {
				const auto new_target = std::find_if(
						target + 1, end(), [&](auto e) { return e.end() == block.begin(); }
				);
				if (new_target == end()) {
					Insert({ block.payload, block.size + target->size }, target);
				} else {
					Insert({ new_target->payload, new_target->size + block.size + target->size }, target, new_target);
				}
			}
		}

	private:
		[[nodiscard]] inline constexpr Block* begin() const noexcept {
			return list;
		}

		[[nodiscard]] inline constexpr Block* end() const noexcept {
			return list + size;
		}

		static inline Block* Memory_copy(Block* first, size_t size, Block* result) noexcept {
			memcpy(result, first, size);
			return result + size;
		}

		static inline Block* Memory_copy(Block* first, Block* last, Block* result) noexcept {
			return Memory_copy(first, last - first, result);
		}

		static inline Block* Memory_move(Block* first, size_t size, Block* result) noexcept {
			memmove(result, first, size);
			return result + size;
		}

		static inline Block* Memory_move(Block* first, Block* last, Block* result) noexcept {
			return Memory_move(first, last - first, result);
		}

		void Insert(Block insert_block, Block* remove_block) const noexcept {
			const auto index = std::upper_bound(remove_block, end(), insert_block);
			const auto p = Memory_move(remove_block + 1, index, remove_block);
			*p = insert_block;
		}

		void Insert(Block insert_block, Block* remove_block1, Block* remove_block2) noexcept {
			const auto index = std::upper_bound(remove_block2, end(), insert_block);
			auto p = Memory_move(remove_block1 + 1, remove_block2, remove_block1);
			p = Memory_move(remove_block2 + 1, index, p);
			*p = insert_block;
			Memory_move(index, end(), p + 1);
			--size;
		}
	};


	Chunk* chunk_head{};
	FreeBlock free_block{};

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

		if (const auto ret = free_block.allocate(size); ret != nullptr) {
			return ret;
		}

		chunk_head = new Chunk(chunk_head);
		free_block.insert({ chunk_head->payload + size, chunk_size - size });
		return chunk_head->payload;
	}

	void deallocate(void* p, size_t size) noexcept {
		if (p == nullptr || size == 0) {
			return;
		}

		if (size >= chunk_size) {
			operator delete[](p);
		} else {
			free_block.insert({ p, size });
		}
	}
};

