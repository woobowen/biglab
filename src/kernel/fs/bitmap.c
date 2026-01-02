#include "mod.h"

extern super_block_t sb;

/*
	查询一个block中的所有bit, 找到空闲bit, 设置1并返回
	如果没有空闲bit, 返回-1
    valid_count: 该block中有效的bit数量(用于处理最后一个bitmap block不满的情况)
*/ 
static uint32 bitmap_search_and_set(uint32 bitmap_block_num, uint32 valid_count)
{
    buffer_t *buf = buffer_get(bitmap_block_num);
    uint32 bit_index = -1;

    // 遍历 block 中的每个字节
    for (uint32 i = 0; i < BLOCK_SIZE; i++) {
        // 如果该字节全为1, 且完全在有效范围内, 则直接跳过
        if (buf->data[i] == 0xFF && (i + 1) * BIT_PER_BYTE <= valid_count) {
            continue;
        }

        // 遍历该字节中的每一位
        for (int j = 0; j < BIT_PER_BYTE; j++) {
            uint32 current_bit = i * BIT_PER_BYTE + j;

            // 超过有效范围, 停止搜索
            if (current_bit >= valid_count) {
                buffer_put(buf);
                return -1;
            }

            // 检查该位是否为0 (空闲)
            if (!((buf->data[i] >> j) & 1)) {
                // 将该位设为1 (分配)
                buf->data[i] |= (1U << j);
                bit_index = current_bit;
                // 写回磁盘
                buffer_write(buf);
                buffer_put(buf);
                return bit_index;
            }
        }
    }
    // 增加函数收尾逻辑
    buffer_put(buf); 
    return (uint32)-1;
}

/* 
	将block中第index个bit设为0
*/
static void bitmap_clear(uint32 bitmap_block_num, uint32 index)
{
    buffer_t *buf = buffer_get(bitmap_block_num);

    uint32 byte_offset = index / BIT_PER_BYTE;
    uint32 bit_offset = index % BIT_PER_BYTE;

    // 检查防止越界
    if (byte_offset < BLOCK_SIZE) {
        // 将对应位清0
        buf->data[byte_offset] &= ~(1U << bit_offset);
        // 写回磁盘
        buffer_write(buf);
    }
    buffer_put(buf);
}

/*
	获取一个空闲block, 将data_bitmap对应bit设为1
	返回这个block的全局序号
*/
uint32 bitmap_alloc_block()
{
    uint32 count = sb.data_blocks; // 总数据块数
    uint32 start_bitmap = sb.data_bitmap_firstblock; // data bitmap 起始块
    uint32 num_bitmap_blocks = sb.data_bitmap_blocks; // data bitmap 占用的块数
    uint32 bits_scanned = 0; 

    // 遍历每个 bitmap block
    for (uint32 i = 0; i < num_bitmap_blocks; i++) {
        // 计算当前 block 中有效的 bit 数量
        uint32 valid_bits = BIT_PER_BLOCK;
        if (bits_scanned + BIT_PER_BLOCK > count) {
            // 最后一个 block 可能不满
            valid_bits = count - bits_scanned; 
        }

        // 在当前 bitmap block 中搜索并设置空闲 bit
        uint32 ret = bitmap_search_and_set(start_bitmap + i, valid_bits);

        if (ret != (uint32)-1) {
            // 找到空闲 bit, 计算全局 block 号并返回
            return sb.data_firstblock + bits_scanned + ret;
        }
        bits_scanned += valid_bits;
    }
    // 没有空闲块
    return (uint32)-1;
}

/*
	获取一个空闲inode, 将inode_bitmap对应bit设为1
	返回这个inode的全局序号
*/
uint32 bitmap_alloc_inode()
{
    uint32 count = sb.total_inodes; // 总inode数
    uint32 start_bitmap = sb.inode_bitmap_firstblock; // inode bitmap 起始块
    uint32 num_bitmap_blocks = sb.inode_bitmap_blocks; // inode bitmap 占用的块数
    uint32 bits_scanned = 0; 

    // 遍历每个 bitmap block
    for (uint32 i = 0; i < num_bitmap_blocks; i++) {
        // 计算当前 block 中有效的 bit 数量
        uint32 valid_bits = BIT_PER_BLOCK;
        if (bits_scanned + BIT_PER_BLOCK > count) {
            // 最后一个 block 可能不满
            valid_bits = count - bits_scanned; 
        }

        // 在当前 bitmap block 中搜索并设置空闲 bit
        uint32 ret = bitmap_search_and_set(start_bitmap + i, valid_bits);

        if (ret != (uint32)-1) {
            // 找到空闲 bit, 计算全局 inode 号并返回
            return bits_scanned + ret;
        }
        bits_scanned += valid_bits;
    }
    // 没有空闲inode
    return (uint32)-1;
}

/* 释放一个block, 将data_bitmap对应bit设为0 */
void bitmap_free_block(uint32 block_num)
{
    // 检查合法性
    if (block_num < sb.data_firstblock || 
        block_num >= sb.data_firstblock + sb.data_blocks) {
        return; 
    }

    // 计算位于哪个 bitmap block + 在该 block 中的 bit 偏移
    uint32 offset = block_num - sb.data_firstblock;
    uint32 bitmap_block_index = offset / BIT_PER_BLOCK;
    uint32 bit_index = offset % BIT_PER_BLOCK;

    bitmap_clear(sb.data_bitmap_firstblock + bitmap_block_index, bit_index);
}

/* 释放一个inode, 将inode_bitmap对应bit设为0 */
void bitmap_free_inode(uint32 inode_num)
{
    // 检查合法性
    if (inode_num >= sb.total_inodes) {
        return; 
    }

    // 计算位于哪个 bitmap block + 在该 block 中的 bit 偏移
    uint32 bitmap_block_index = inode_num / BIT_PER_BLOCK;
    uint32 bit_index = inode_num % BIT_PER_BLOCK;

    bitmap_clear(sb.inode_bitmap_firstblock + bitmap_block_index, bit_index);
}

/* 打印某个bitmap中所有分配出去的bit */
void bitmap_print(bool print_data_bitmap)
{
    uint32 first_block, bitmap_blocks, total_bits;
    uint32 global_base, current_bit = 0;

    if (print_data_bitmap) {
		printf("data bitmap alloced bits:\n");
        first_block = sb.data_bitmap_firstblock;
        bitmap_blocks = sb.data_bitmap_blocks;
        total_bits = sb.data_blocks;
        global_base = sb.data_firstblock;
    } else {
		printf("inode bitmap alloced bits:\n");
		first_block = sb.inode_bitmap_firstblock;
        bitmap_blocks = sb.inode_bitmap_blocks;
        total_bits = sb.total_inodes;
        global_base = 0;
    }

    for (uint32 block = 0; block < bitmap_blocks; block++)
	{
        uint32 bitmap_block_num = first_block + block;
        uint32 bits_in_this_block = BIT_PER_BLOCK;

        // 最后一个 block 可能不满
        if (current_bit + BIT_PER_BLOCK > total_bits)
            bits_in_this_block = total_bits - current_bit;

        buffer_t *buf = buffer_get(bitmap_block_num);

        // 遍历该 block 中的有效 bit
        for (uint32 byte = 0; byte < bits_in_this_block / BIT_PER_BYTE; byte++)
		{
            for (uint32 shift = 0; shift < BIT_PER_BYTE; shift++)
			{
                if (current_bit >= total_bits)
					break;

                uint8 mask = (uint8)(1U << shift);
                if (buf->data[byte] & mask)
                    printf("%d ", global_base + current_bit);
                current_bit++;
            }
        }
        buffer_put(buf);
    }
	printf("over!\n\n");
}
