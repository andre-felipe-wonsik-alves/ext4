#include <cstdint>

#ifndef EXT4CHECKSUM_H
#define EXT4CHECKSUM_H

/* converte um array byte em int32 little endian */
uint32_t bytearray_to_int32_le(unsigned char b[4]);

/* calculo dos checksums para ext4 */
uint32_t checksum_superblock(char* super);
uint16_t checksum_group(char* uuid, int32_t group_number, char* group);
uint32_t checksum_bitmap(char* uuid, char* bitmap, int size);
uint32_t checksum_inode(char* uuid, uint32_t inode_number, uint32_t inode_gen, char* inode);
uint32_t checksum_dir(char* uuid, uint32_t inode_number, uint32_t inode_gen, char* dir, int blocksize);
uint32_t checksum_extent (char* uuid, uint32_t inode_number, uint32_t inode_gen, char* extent, int blocksize);

#endif
