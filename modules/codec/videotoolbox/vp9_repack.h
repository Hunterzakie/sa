#include <vlc_block.h>

struct vp9_repacker
{
    block_t *p_head;
    block_t **pp_append;
    uint8_t count ;
    size_t maxsize;
    size_t total;
};

static inline void vp9_repacker_Clean(struct vp9_repacker *r)
{
    block_ChainRelease(r->p_head);
}

static inline void vp9_repacker_Init(struct vp9_repacker *r)
{
    r->p_head = NULL;
    r->pp_append = &r->p_head;
    r->count = 0;
    r->maxsize = 0;
    r->total = 0;
}

static inline block_t * vp9_repacker_Push(struct vp9_repacker *r, block_t *block,
                                          bool show_frame)
{
    if(block->p_buffer[block->i_buffer - 1] >> 5 == 0x06)
    {
        /* discard aggregated */
        vp9_repacker_Clean(r);
        vp9_repacker_Init(r);
        return block;
    }

    if(show_frame && r->p_head == NULL)
        return block;

    if(r->count == 8 || /* max possible index reached. something is wrong */
       UINT64_MAX - r->total < block->i_buffer)
    {
        vp9_repacker_Clean(r);
        vp9_repacker_Init(r);
    }

    r->count++;
    r->total += block->i_buffer;
    if(block->i_buffer > r->maxsize)
        r->maxsize = block->i_buffer;
    block_ChainLastAppend(&r->pp_append, block);

    if(show_frame)
    {
        /* make superframe from aggregated content B.2 */
        uint8_t bpf = 1;
        for(size_t maxsize = r->maxsize >> 8; maxsize; maxsize >>= 8)
            bpf++;
        block_t *idxblock = block_Alloc(1 + r->count * bpf + 1);
        if(!idxblock)
            return NULL;
        /* create index B.2.1 */
        /* set superframe headers B.2.2 */
        idxblock->p_buffer[0] = 0xC0 | ((bpf - 1) << 3) | (r->count - 1);
        idxblock->p_buffer[idxblock->i_buffer - 1] = idxblock->p_buffer[0];
        /* fill frames size index */
        uint8_t *p = &idxblock->p_buffer[1];
        for(block = r->p_head; block; block = block->p_next)
        {
            uint8_t buf[8];
            SetDWBE(buf, block->i_buffer);
            memcpy(p, &buf[8 - bpf], bpf);
            p += bpf;
        }
        /* merge */
        block_ChainLastAppend(&r->pp_append, idxblock);
        block = block_ChainGather(r->p_head);
        vp9_repacker_Init(r);
        if(block)
            return block;
    }

    return NULL;
}
