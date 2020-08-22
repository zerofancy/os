/*
 * @Website: https://ntutn.top
 * @Date: 2019-12-20 15:02:24
 * @LastEditors: zero
 * @LastEditTime: 2019-12-20 15:02:27
 * @Description: 文件描述
 * @FilePath: /os/memorymanage/src/test.c
 */
#include <string.h> 	/* memcpy(), memset() */
#include <stdio.h> 	/* printf() */
#define	HEAP_SIZE	500000uL
#define	MALLOC_MAGIC	0x6D92	/* must be < 0x8000 */

typedef struct _malloc		
{
   	size_t size;			/*4 bytes */
    	struct _malloc *next;		/*4 bytes */
	unsigned magic : 15;		/*4 bytes total */
	unsigned used : 1;
} malloc_t;					/* total	12 bytes */

static char *g_heap_bot, *g_kbrk, *g_heap_top;
//heap是程序员malloc来分配的,在堆的头部有一个字节指示要分配的堆的大小.
static void dump_heap(void)
{
    unsigned blks_used = 0, blks_free = 0;
    size_t bytes_used = 0, bytes_free = 0;
    malloc_t *m;
    int total;

    printf("===============================================\n");
    for(m = (malloc_t *)g_heap_bot; m != NULL; m = m->next)
    {
        printf("blk %5p: %6u bytes %s\n", m,
               m->size, m->used ? "used" : "free");
        if(m->used)
        {
            blks_used++;
            bytes_used += m->size;
        }
        else
        {
            blks_free++;
            bytes_free += m->size;
        }
    }
    printf("blks:  %6u used, %6u free, %6u total\n", blks_used,
           blks_free, blks_used + blks_free);
    printf("bytes: %6u used, %6u free, %6u total\n", bytes_used,
           bytes_free, bytes_used + bytes_free);
    printf("g_heap_bot=0x%p, g_kbrk=0x%p, g_heap_top=0x%p\n",
           g_heap_bot, g_kbrk, g_heap_top);
    total = (bytes_used + bytes_free) +
            (blks_used + blks_free) * sizeof(malloc_t);
    if(total != g_kbrk - g_heap_bot)
        printf("*** some heap memory is not accounted for\n");
    printf("===============================================\n");
}

static void *kbrk(int *delta)
{
    static char heap[HEAP_SIZE];
    /**/
    char *new_brk, *old_brk;	//实现虚拟内存到内存的映射

    /* heap doesn't exist yet */
    if(g_heap_bot == NULL)
    {
        g_heap_bot = g_kbrk = heap;
        g_heap_top = g_heap_bot + HEAP_SIZE;
    }
    new_brk = g_kbrk + (*delta);
    /* too low: return NULL */
    if(new_brk < g_heap_bot)
        return NULL;
    /* too high: return NULL */
    if(new_brk >= g_heap_top)
        return NULL;
    /* success: adjust brk value... */
    old_brk = g_kbrk;
    g_kbrk = new_brk;
    /* ...return actual delta... (for this sbrk(), they are the same)
    	(*delta) = (*delta); */
    /* ...return old brk value */
    return old_brk;
}
/**************************************************************************
kmalloc() and kfree() use g_heap_bot, but not g_kbrk nor g_heap_top
**************************************************************************/
void *kmalloc(size_t size)
{
    unsigned total_size;
    malloc_t *m, *n;
    int delta;

    if(size == 0)
        return NULL;
    total_size = size + sizeof(malloc_t);	//42 bytes + 12 blks =54
    m = (malloc_t *)g_heap_bot;		
   if(m != NULL)
    {
        if(m->magic != MALLOC_MAGIC)
            {
            printf("*** kernel heap is corrupt in kmalloc()\n");
            return NULL;
        }
        for(; m->next != NULL; m = m->next)
        {
            if(m->used)
                continue;
            if(size == m->size)
                m->used = 1;
            else
            {
                if(total_size > m->size)
                    continue;
                n = (malloc_t *)((char *)m + total_size);
                n->size = m->size - total_size;
                n->next = m->next;
                n->magic = MALLOC_MAGIC;
                n->used = 0;
                m->size = size;
                m->next = n;
                m->used = 1;
            }
            return (char *)m + sizeof(malloc_t);
        }
    }

    delta = total_size;
    n = kbrk(&delta);
    if(n == NULL)
        return NULL;
    if(m != NULL)
        m->next = n;
    n->size = size;
    n->magic = MALLOC_MAGIC;
    n->used = 1;
    if((int)total_size == delta)
        n->next = NULL;
    else
    {
        m = (malloc_t *)((char *)n + total_size);
        m->size = delta - total_size - sizeof(malloc_t);
        m->next = NULL;
        m->magic = MALLOC_MAGIC;
        m->used = 0;

        n->next = m;
    }
    return (char *)n + sizeof(malloc_t);
}

void kfree(void *blk)
{
    malloc_t *m, *n;

    m = (malloc_t *)((char *)blk - sizeof(malloc_t));
    if(m->magic != MALLOC_MAGIC)
    {
        printf("*** attempt to kfree() block at 0x%p "
               "with bad magic value\n", blk);
        return;
    }
    n = (malloc_t *)g_heap_bot;
    if(n->magic != MALLOC_MAGIC)
    {
        printf("*** kernel heap is corrupt in kfree()\n");
        return;
    }
    for(; n != NULL; n = n->next)
    {
        if(n == m)
            break;
    }
  
    if(n == NULL)
   
    {
        printf("*** attempt to kfree() block at 0x%p "
               "that is not in the heap\n", blk);
        return;
    }
    /* free the block */
    m->used = 0;
   
    for(m = (malloc_t *)g_heap_bot; m != NULL; m = m->next)
    {
        while(!m->used && m->next != NULL && !m->next->used)
        {
            m->size += sizeof(malloc_t) + m->next->size;
            m->next = m->next->next;
        }
    }
}

void *krealloc(void *blk, size_t size)
{
    void *new_blk;
    malloc_t *m;

    if(size == 0)
    {
        if(blk != NULL)
            kfree(blk);
        new_blk = NULL;
    }
    else
    {
        /* allocate new block */
        new_blk = kmalloc(size);
        if(new_blk != NULL && blk != NULL)
        {
            m = (malloc_t *)((char *)blk - sizeof(malloc_t));
            if(m->magic != MALLOC_MAGIC)
            {
                printf("*** attempt to krealloc() block at "
                       "0x%p with bad magic value\n", blk);
                return NULL;
            }
            if(size > m->size)
                size = m->size;
            memcpy(new_blk, blk, size);
            kfree(blk);
        }
    }
    return new_blk;
}

int main(void)
{
    void *b1, *b2, *b3;
    dump_heap();

    b1 = kmalloc(42);
    dump_heap();
    getchar();

    b2 = kmalloc(23);
    dump_heap();
    getchar();

    b3 = kmalloc(7);
    dump_heap();
    getchar();

    b2 = krealloc(b2, 24);
    dump_heap();
    getchar();

    kfree(b1);
    dump_heap();
    getchar();

    b1 = kmalloc(5);
    dump_heap();
    getchar();

    kfree(b2);
    dump_heap();
    getchar();

    kfree(b3);
    dump_heap();
    getchar();

    kfree(b1);
    dump_heap();

    return 0;
}