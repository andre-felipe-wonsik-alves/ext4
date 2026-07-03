#ifndef EXT4_UTILS_H
#define EXT4_UTILS_H

#include <cstdint>
#include <fstream>
#include <vector>
#include "ext4_structs.h"

/**
 *  Lê o superbloco de uma imagem de um SA ext4
 *  @param image: objeto std::fstream onde a imagem está aberta
 *  @param sb: objeto super_block onde o superbloco será armazenado
 *  @returns true se o superbloco foi lido com sucesso; false caso contrário
 */
bool read_superblock(std::fstream& image, super_block& sb);

/**
 *  Lê a GDT de um SA ext4
 *  @param ext4_info: objeto ext4_sb_info com as informações estáticas do SA
 *  @returns true se a GDT foi lida com sucesso; false caso contrário
 */
bool read_gdt(ext4_sb_info& ext4_info);

/**
 *  Imprime um superbloco
 *  @param sb: objeto super_block onde o superbloco está armazenado
 */
void print_superblock(const ext4_sb_info& ext4_info);

/**
 *  Imprime uma GDT
 *  @param ext4_info: objeto ext4_sb_info com as informações estáticas do SA
 */
void print_gdt(const ext4_sb_info& ext4_info);

/**
 *  Inicializa todos os valores constantes necessários para operar sobre uma imagem ext4
 *  @param image: objeto std::fstream onde a imagem está aberta
 *  @param sb: objeto super_block onde o superbloco está armazenado
 *  @returns um objeto ext4_sb_info
 */
ext4_sb_info init(std::fstream& image, const super_block& sb);

/**
 *  Retorna o tamanho de um bloco em um SA
 *  @param sb: objeto super_block onde o superbloco está armazenado
 *  @returns 2 ^ (10 + s_log_block_size)
 */
inline uint64_t get_block_size(const super_block& sb) {
    return static_cast<uint64_t>(1024) << sb.s_log_block_size;
}

/**
 *  Retorna a quantidade de blocos em umx' SA
 *  @param sb: objeto super_block onde o superbloco está armazenado
 *  @returns s_blocks_count_hi << 32 | s_blocks_count_lo
 */
inline uint64_t get_blocks_count(const super_block& sb) {
    return (static_cast<uint64_t>(sb.s_blocks_count_hi) << 32) | sb.s_blocks_count_lo;
}

/**
 *  Retorna a quantidade de grupos de blocos em um SA
 *  @param sb: objeto super_block onde o superbloco está armazenado
 *  @returns (total_blocks + s_blocks_per_group - 1) / s_blocks_per_group
 */
inline uint64_t get_num_groups(const super_block& sb) {
    uint64_t total_blocks = get_blocks_count(sb);

    return (total_blocks + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
}

/**
 *  Retorna o offset de um bloco em um SA
 *  @param block: número do bloco
 *  @param sb: objeto super_block onde o superbloco está armazenado
 *  @returns block * block_size
 */
inline uint64_t get_block_offset(uint64_t block, const super_block& sb) {
    return block * get_block_size(sb);
}

#endif
