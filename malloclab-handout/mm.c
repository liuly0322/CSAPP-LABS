/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {"lily (flower)", "liuly", "liuly@mail.ustc.edu.cn", "", ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

/* rounds up to the nearest multiple of ALIGNMENT */
/* 8 bytes in my computer */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)

/* 总是指向序言块的第二块 */
static void* heap_list;

/* util function for calculate max */
static inline unsigned max(unsigned x, unsigned y) {
    return x > y ? x : y;
}
static inline unsigned min(unsigned x, unsigned y) {
    return x < y ? x : y;
}

/* get or set p as unsigned pointer */
static inline unsigned get(void* p) {
    return *(unsigned*)p;
}
static inline void put(void* p, unsigned val) {
    *(unsigned*)p = val;
}

/* pack info. size & alloc sign bits */
static inline unsigned pack(unsigned size, unsigned alloc) {
    return size | alloc;
}
/* depack size and alloc sign bits from pack */
static inline unsigned get_size(void* p) {
    return get(p) & ~0x7;
}
static inline unsigned get_alloc(void* p) {
    return get(p) & 1;
}

/* find head pointer for given class_num */
static inline void* get_head(int class_num) {
    return (void*)(get(heap_list + WSIZE * class_num));
}
/* find pre or suc pointer for given bp */
static inline void* get_pre(void* bp) {
    return (void*)get(bp);
}
static inline void* get_suc(void* bp) {
    return (void*)(get((void*)bp + WSIZE));
}

/* 根据有效载荷指针, 找到头部，脚部，前一块，下一块 */
static inline void* hdrp(void* bp) {
    return bp - WSIZE;
}
static inline void* ftrp(void* bp) {
    return bp + get_size(hdrp(bp)) - DSIZE;
}
static inline void* next_blkp(void* bp) {
    return bp + get_size(bp - WSIZE);
}
static inline void* prev_blkp(void* bp) {
    return bp - get_size(bp - DSIZE);
}

/* 分离适配的大小类个数 */
#define CLASS_SIZE 20

static void* extend_heap(unsigned words);     // 扩展堆
static void* merge(void* bp);                 // 合并空闲块
static void* find_fit(unsigned asize);        // 找到匹配的块
static unsigned find_class(unsigned size);    // 找到对应大小类
static void place(void* bp, unsigned asize);  // 分割空闲块
static void delete (void* bp);                // 从相应链表中删除块
static void insert(void* bp);                 // 在对应链表中插入块

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    /* 申请四个字节空间 */
    if ((heap_list = mem_sbrk((4 + CLASS_SIZE) * WSIZE)) == (void*)-1)
        return -1;
    /* 初始化大小类头指针 */
    for (int i = 0; i < CLASS_SIZE; i++) {
        put(heap_list + i * WSIZE, 0);
    }
    /* 对齐 */
    put(heap_list + CLASS_SIZE * WSIZE, 0);
    /* 序言块和结尾块 */
    put(heap_list + ((1 + CLASS_SIZE) * WSIZE), pack(DSIZE, 1));
    put(heap_list + ((2 + CLASS_SIZE) * WSIZE), pack(DSIZE, 1));
    put(heap_list + ((3 + CLASS_SIZE) * WSIZE), pack(0, 1));

    /* 扩展空闲空间 */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * 扩展 heap, 传入的是字数
 */
void* extend_heap(unsigned words) {
    unsigned size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    void* bp; /* 指向有效载荷 */
    if ((int)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* 设置头部和脚部 */
    put(hdrp(bp), pack(size, 0));         /* 空闲块头 */
    put(ftrp(bp), pack(size, 0));         /* 空闲块脚 */
    put(hdrp(next_blkp(bp)), pack(0, 1)); /* 片的新结尾块 */

    /* 判断相邻块是否是空闲块, 进行合并 */
    return merge(bp);
}

/*
 * 合并空闲块
 */
void* merge(void* bp) {
    unsigned prev_alloc = get_alloc(ftrp(prev_blkp(bp))); /* 前一块是否被分配 */
    unsigned next_alloc = get_alloc(hdrp(next_blkp(bp))); /* 后一块是否被分配 */
    unsigned size = get_size(hdrp(bp));                   /* 当前块大小 */

    /*
     * 四种情况：前后都不空, 前不空后空, 前空后不空, 前后都空
     */
    /* 前后都不空 */
    if (prev_alloc && next_alloc) {
        insert(bp);
        return bp;
    }
    /* 前不空后空 */
    else if (prev_alloc && !next_alloc) {
        /* 将后面的块从其链表中删除 */
        delete (next_blkp(bp));
        size += get_size(hdrp(next_blkp(bp)));  //增加当前块大小
        put(hdrp(bp), pack(size, 0));           //先修改头
        put(ftrp(bp), pack(size, 0));  //根据头部中的大小来定位尾部
    }
    /* 前空后不空 */
    else if (!prev_alloc && next_alloc) {
        /* 将其前面的块从链表中删除 */
        delete (prev_blkp(bp));
        size += get_size(hdrp(prev_blkp(bp)));  //增加当前块大小
        put(ftrp(bp), pack(size, 0));
        put(hdrp(prev_blkp(bp)), pack(size, 0));
        bp = prev_blkp(bp);  //注意指针要变
    }
    /* 都空 */
    else {
        /* 将前后两个块都从其链表中删除 */
        delete (next_blkp(bp));
        delete (prev_blkp(bp));
        size += get_size(hdrp(prev_blkp(bp))) +
                get_size(ftrp(next_blkp(bp)));  //增加当前块大小
        put(ftrp(next_blkp(bp)), pack(size, 0));
        put(hdrp(prev_blkp(bp)), pack(size, 0));
        bp = prev_blkp(bp);
    }
    /* 空闲块准备好后,将其插入合适位置 */
    insert(bp);
    return bp;
}

/*
 *  插入块, 将块插到表头
 */
void insert(void* bp) {
    /* 块大小 */
    unsigned size = get_size(hdrp(bp));
    /* 根据块大小找到头节点位置 */
    unsigned class_num = find_class(size);
    /* 空的，直接放 */
    if (get_head(class_num) == NULL) {
        put(bp, 0);
        put((unsigned*)bp + 1, 0);

        put(heap_list + WSIZE * class_num, (unsigned)bp);
    } else {
        put(bp, 0);
        put((unsigned*)bp + 1, (unsigned)get_head(class_num));

        put(get_head(class_num), (unsigned)bp);
        put(heap_list + WSIZE * class_num, (unsigned)bp);
    }
}

/*
 *  删除块,清理指针
 */
void delete (void* bp) {
    unsigned size = get_size(hdrp(bp));
    unsigned class_num = find_class(size);

    void *pre_p = get_pre(bp), *suc_p = get_suc(bp);
    /* 唯一节点 头节点设为 null */
    if (!pre_p && !suc_p) {
        put(heap_list + WSIZE * class_num, 0);
    }
    /* 最后一个节点 前驱的后继设为 null */
    else if (pre_p && !suc_p) {
        put(pre_p + WSIZE, 0);
    }
    /* 第一个结点 头节点设为 bp 的后继 */
    else if (!pre_p && suc_p) {
        put(heap_list + WSIZE * class_num, (unsigned)suc_p);
        put(suc_p, 0);
    }
    /* 中间结点，前驱的后继设为后继，后继的前驱设为前驱 */
    else {
        put(pre_p + WSIZE, (unsigned)suc_p);
        put(suc_p, (unsigned)pre_p);
    }
}

/*
 * find_class - 找到块大小对应的等价类的序号
 */
unsigned find_class(unsigned size) {
    for (int i = 4; i < CLASS_SIZE + 4; i++) {
        if (size <= (1 << i))
            return i - 4;
    }
    return CLASS_SIZE - 1;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void* mm_malloc(unsigned size) {
    void* bp;
    if (size == 0)
        return NULL;
    unsigned asize;
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    /* 寻找合适的空闲块 */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    /* 找不到则扩展堆 */
    if ((bp = extend_heap(max(asize, CHUNKSIZE) / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * 适配
 */
void* find_fit(unsigned asize) {
    unsigned class_num = find_class(asize);
    void* bp;
    /* 如果找不到合适的块，那么就搜索下一个更大的大小类 */
    while (class_num < CLASS_SIZE) {
        bp = get_head(class_num);
        /* 不为空则寻找 */
        unsigned min_size = (unsigned)(-1);
        void* min_bp = NULL;
        while (bp) {
            unsigned cur_size = get_size(hdrp(bp));
            if (cur_size >= asize && cur_size < min_size) {
                min_size = cur_size;
                min_bp = bp;
            }
            /* 用后继找下一块 */
            bp = get_suc(bp);
        }
        /* 找不到则进入下一个大小类 */
        if (min_bp) {
            return min_bp;
        }
        class_num++;
    }
    return NULL;
}

/*
 * 分离空闲块
 */
void place(void* bp, unsigned asize) {
    unsigned csize = get_size(hdrp(bp));

    /* 已分配，从空闲链表中删除 */
    delete (bp);
    if ((csize - asize) >= 2 * DSIZE) {
        put(hdrp(bp), pack(asize, 1));
        put(ftrp(bp), pack(asize, 1));
        /* bp 指向空闲块 */
        bp = next_blkp(bp);
        put(hdrp(bp), pack(csize - asize, 0));
        put(ftrp(bp), pack(csize - asize, 0));
        /* 加入分离出来的空闲块 */
        insert(bp);
    }
    /* 设置为填充 */
    else {
        put(hdrp(bp), pack(csize, 1));
        put(ftrp(bp), pack(csize, 1));
    }
}
/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void* ptr) {
    if (ptr == 0)
        return;
    unsigned size = get_size(hdrp(ptr));

    put(hdrp(ptr), pack(size, 0));
    put(ftrp(ptr), pack(size, 0));
    merge(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void* mm_realloc(void* ptr, unsigned size) {
    if (ptr == NULL) {
        return mm_malloc(size);
    } else if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    unsigned copysize = min(get_size(hdrp(ptr)), size);

    void* newptr;
    newptr = mm_malloc(size);
    memcpy(newptr, ptr, copysize);
    mm_free(ptr);

    return newptr;
}