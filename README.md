## Allocator（内存分配器）：定长内存分配
采用`free list`实现

## MemoryPool（内存池）：变长内存分配
一个单链表记录分配出的大块内存`chunk`，一个按照`size`升序的列表记录空闲内存节点。
- 分配内存时二分查找首个大于或等于要求`size`的节点，若大小正好相等则从列表中删除这个节点，`count`减一；否则分裂这块内存，二分插入分裂出的新节点，`count`不变。
- 回收内存时，若`count < capacity`，则二分查找首个大于这块内存`size`的位置进行插入，`count`加一；否则新开辟一块内存，将现有的内存节点按照`buffer`升序排列，合并内存碎片，将合并后的内存节点按照`size`二分插入到新内存中。
