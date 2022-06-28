## Allocator（内存分配器）：固定分区分配
  ```cpp
  template <typename T>
  class Allocator {
      static constexpr size_t block_size = sizeof(T);
      static constexpr size_t blocks_per_chunk = 128;
  
      struct FreeBlock {
          std::byte payload[block_size];
          void* const mask{ payload };
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
  
      Chunk* chunk_head_{};
      FreeBlock* free_block_head_{};
  
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
      
      constexpr ~Allocator() noexcept {
          while (chunk_head_) {
              delete std::exchange(chunk_head_, chunk_head_->next);
          }
      }
  
      [[nodiscard]] T* allocate();
  
      void deallocate(T* p) noexcept;
  };
  ```
  1. ```cpp
     struct Chunk {
         FreeBlock blocks[blocks_per_chunk];
         Chunk* next{};
     
         constexpr Chunk() noexcept {
             for (auto it = std::begin(blocks); it != std::end(blocks) - 1; ++it) {
                 it->next = it + 1;
             }
         }
     };
     ```
     每个`chunk`中有`blocks_per_chunk`个`block`，`next`指针指向下一个`chunk`。当创建一个新的`chunk`时，其前`blocks_per_chunk-1`个`block`的`next`指针指向各自的下一个`block`，最后一个`block`的`next`为`nullptr`。
  2. ```cpp
     struct FreeBlock {
         std::byte payload[block_size];
         void* const mask{ payload }; 
         FreeBlock* next{};
     };
     ```
     每个`block`中有一块大小为`block_size`个字节的`payload`，表示实际负载的首地址。
     `next`指针指向下一个`block`。
     `mask`指针永远指向`payload`，调用`deallocate`时，实参指针即其所在的`block`，也即`payload`，判断它是否等于`mask`，只有二者相等才表示传入指针合法。
  3. 分配内存时，若`free_block_head_`不为空，则将其分配，`free_block_head_`指向其下一个`block`，否则创建一个新的`chunk`，采用头插法插入`chunk_head_`之前，将其的第一个`block`分配，第二个`block`作为`free_block_head_`。
  4. 回收内存时，实参指针即其所在的`block`，判断合法后采用头插法插入`free_block_head_`之前。

## MemoryPool（内存池）：动态分区分配
```cpp
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
    };

    struct ChunkHeader {
        ChunkHeader* next;
        std::byte payload[0];
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

    [[nodiscard]] void* allocate(size_t size;

    [[nodiscard]] void* reallocate(void* p, size_t size);

    void deallocate(void* p) noexcept;

private:
    constexpr MemoryPool() noexcept = default;
    
   	void* Alloc_from_new_chunk(size_t size);
                                 
    void* Alloc_from_curr_block(size_t allocSize) noexcept; 
                                 
    void Free_curr_block() noexcept;
}
```
  1. ```cpp
     struct BlockHeader {
         size_t meta;
         void* const mask{ payload };  
         std::byte payload[0];
     }
     ```
     `meta`记录每块`block`的信息，最低位`is_free`表示当前`block`是否空闲，次低位`is_prev_free`表示前一个`block`是否空闲，倒数第三位`is_last`表示当前`block`是否是当前其所在`chunk`的最后一块，其余`block_size`表示当前`block`的总字节数。将当前`block`的首地址加上`meta`中的`block_size`即可访问下一个`block`。
     `payload`不占`BlockHeader`的空间，表示实际负载的首地址。
     `mask`指针永远指向`payload`，调用`reallocate`和`deallocate`时，将实参指针减去`block_header_size`个字节即可访问它所在`block`的`BlockHeader`，然后判断它是否等于其所在`block`的`mask`，只有二者相等才表示传入指针合法。
  2. ```cpp
     struct BlockFooter {
         BlockHeader* header;
     };
     ```
     空闲`block`还需要一个尾部标记`BlockFooter`，只有一个`header`指针指向其头部标记`BlockHeader`。因为当`block`的`is_prev_free`为`1`时说明其前一个`block`空闲，为了能访问到前一个空闲`block`需要给其加上尾部标记，将当前`block`首地址减去`block_footer_size`个字节即可访问前一个`block`的尾部标记，再通过其`header`指针即可访问到头部标记。因此空闲`block`的`meta`的`block_size`为`block_tag_size`加上实际负载空间。当`block`的`is_prev_free`为`0`时说明其前一个`block`非空闲，我们不会访问前一个非空闲`block`，因此非空闲`block`无需加上尾部标记。
  3. ```cpp
     struct ChunkHeader {
         ChunkHeader* next;
         std::byte payload[0];
     };
     
     struct ChunkFooter {
         ChunkHeader* next;
     };
     ```
     每个`chunk`头部有一个`ChunkHeader`，尾部有一个`ChunkFooter`，二者都有一个`next`指针，都指向下一个`chunk`。头部的`next`指针用于析构函数释放所有内存使用，尾部的`next`指针用于遍历`block`且到达最后一个`block`（即`is_last`为`1`）时，当前`block`的下一个`block`首地址即其所在`chunk`的`ChunkFooter`，通过其`next`指针即可访问到下一个`chunk`。
  4. `curr_block_`为最近访问的`block`的地址。`allocate`时，从`curr_block_`开始遍历，若`curr_block_`满足条件则从`curr_block_`中分配，否则将`curr_block_`指向下一个`block`。当`curr_block_`回到初始位置时，说明没找到符合条件的`block`，则创建一个新的`chunk`进行分配。
  5. `reallocate`时，找到实参指针所在`block`，判断是否合法，若合法则将`curr_block_`指向它，然后合并其下一个`block`（如果有且空闲的话，结果`meta`中`block_size`为二者之和，`is_last`为下一个`block`的，`is_prev_free`为当前`block`的），判断是否能从`curr_block_`中分配，若能则直接返回其`payload`（合并下一个`block`首地址不变），若不能则继续合并前一个`block`（如果有且空闲的话，结果`meta`中`block_size`为二者之和，`is_last`为当前`block`的，`is_prev_free`肯定为`0`），若存在前一个`block`且空闲再判断是否能从`curr_block_`中分配，若能则需要拷贝原来的数据到当前`payload`（合并前一个`block`首地址变了），若不能则释放`curr_block_`，调用`allocate`并拷贝。
  6. `deallocate`时，找到实参指针所在`block`，判断是否合法，若合法则合并其下一个`block`，再合并前一个`block`，将`curr_block_`指向它，然后释放`curr_block_`。
