
/**
 * @file ext4checksum.cpp
 * @author Rodrigo Campiolo (@rcampiolo)
 * @brief Calcula o checksum (crc32c) para estruturas do ext4
 * @version 0.1
 * @date 2023-10-12
 *
 * @copyright Copyright (c) 2023
 *
 */

#include "ext4checksum.h"
#include <cryptopp/crc.h>

using namespace CryptoPP;

// Usando instâncias locais/estáticas thread-safe ou apenas uma instância global
// Como o shell é single-threaded, uma instância global simples funciona
// perfeitamente.
static CRC32C crc;
static byte digest[4];

/**
 * Converte um array byte em int32 little endian
 * @param b: vetor de tamanho 4 que representa o checksum em bytes
 * @returns inteiro 32 bits que corresponde ao checksum
 */
uint32_t bytearray_to_int32_le(unsigned char b[4]) {
  return (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | (b[0]);
}

/**
 *  Calcula o checksum do superbloco
 *  @param super: vetor de bytes que representa o superbloco do ext4
 *  @returns  inteiro 32 bits que corresponde ao checksum
 */
uint32_t checksum_superblock(char *super) {
  crc.Restart();
  crc.Update((byte *)super, 1020);
  crc.Final(digest);

  return (bytearray_to_int32_le(digest) ^ 0xFFFFFFFF);
}

/**
 *  Calcula o checksum do descritor de grupos
 *  @param uuid: vetor de bytes (tamanho 16) que corresponde ao uuid do
 * superbloco
 *  @param group_number: número do grupo
 *  @param group: vetor de bytes que corresponde ao descritor do grupo
 * group_number
 *  @returns  inteiro 16 bits que corresponde ao checksum
 */
uint16_t checksum_group(char *uuid, int32_t group_number, char *group) {
  uint16_t dummy_csum = 0;

  crc.Restart();
  crc.Update((byte *)uuid, 16);
  crc.Update((byte *)&group_number, 4);
  crc.Update((byte *)group, 30);      // posicao do checksum
  crc.Update((byte *)&dummy_csum, 2); // checksum zerado
  crc.Update((byte *)&group[32], 32); // apos checksum
  crc.Final(digest);

  return ((bytearray_to_int32_le(digest) ^ 0xFFFFFFFF) & 0XFFFF);
}

/**
 *  Calcula o checksum dos mapa de bits
 *  @param uuid: vetor de bytes (tamanho 16) que corresponde ao uuid do
 * superbloco
 *  @param bitmap: vetor de bytes que correponde ao bitmap
 *  @param size: tamanho do bitmap (geralmente 1 bloco)
 *  @returns  inteiro 32 bits que corresponde ao checksum
 */
uint32_t checksum_bitmap(char *uuid, char *bitmap, int size) {
  crc.Restart();
  crc.Update((byte *)uuid, 16);
  crc.Update((byte *)bitmap, size);
  crc.Final(digest);

  return (bytearray_to_int32_le(digest) ^ 0xFFFFFFFF);
}

/**
 *  Calcula o checksum dos inodes
 *  @param uuid: vetor de bytes (tamanho 16) que corresponde ao uuid do
 * superbloco
 *  @param inode_number: número do inode
 *  @param inode_gen: campo do inode i_generation
 *  @param inode: vetor de bytes que corresponde ao inode
 *  @returns  inteiro 32 bits que corresponde ao checksum
 */
uint32_t checksum_inode(char *uuid, uint32_t inode_number, uint32_t inode_gen,
                        char *inode) {
  uint16_t dummy_csum = 0;

  crc.Restart();
  crc.Update((byte *)uuid, 16);
  crc.Update((byte *)&inode_number, 4); // inode number
  crc.Update((byte *)&inode_gen, 4);    // i_generation
  crc.Update((byte *)inode, 124);
  crc.Update((byte *)&dummy_csum, 2); // checksum zerado
  crc.Update((byte *)&inode[126], 4);
  crc.Update((byte *)&dummy_csum, 2); // checksum zerado
  crc.Update((byte *)&inode[132], 124);
  crc.Final(digest);

  return (bytearray_to_int32_le(digest) ^ 0xFFFFFFFF);
}

/**
 *  Calcula o checksum dos diretórios
 *  @param uuid: vetor de bytes (tamanho 16) que corresponde ao uuid do
 * superbloco
 *  @param inode_number: número do inode
 *  @param inode_gen: campo do inode i_generation
 *  @param dir: vetor de bytes que corresponde ao bloco do diretório
 *  @param blocksize: tamanho do bloco
 *  @returns  inteiro 32 bits que corresponde ao checksum
 */
uint32_t checksum_dir(char *uuid, uint32_t inode_number, uint32_t inode_gen,
                      char *dir, int blocksize) {
  crc.Restart();
  crc.Update((byte *)uuid, 16);
  crc.Update((byte *)&inode_number, 4);    // inode number
  crc.Update((byte *)&inode_gen, 4);       // i_generation
  crc.Update((byte *)dir, blocksize - 12); // tamanho bloco - 12 (checksum)
  crc.Final(digest);

  return (bytearray_to_int32_le(digest) ^ 0xFFFFFFFF);
}

/**
 *  Calcula o checksum dos extents
 *  @param uuid: vetor de bytes (tamanho 16) que corresponde ao uuid do
 * superbloco
 *  @param inode_number: número do inode
 *  @param inode_gen: campo do inode i_generation
 *  @param extent: vetor de bytes que corresponde ao extent
 *  @param blocksize: tamanho do bloco
 *  @returns  inteiro 32 bits que corresponde ao checksum
 */
uint32_t checksum_extent(char *uuid, uint32_t inode_number, uint32_t inode_gen,
                         char *extent, int blocksize) {
  crc.Restart();
  crc.Update((byte *)uuid, 16);
  crc.Update((byte *)&inode_number, 4); // inode number
  crc.Update((byte *)&inode_gen, 4);    // i_generation
  crc.Update((byte *)extent,
             blocksize - (blocksize % 12)); // ate a posicao do checksum
  crc.Final(digest);

  return (bytearray_to_int32_le(digest) ^ 0xFFFFFFFF);
}
