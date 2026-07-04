#ifndef EXT4_UTILS_H
#define EXT4_UTILS_H

#include <cstdint>
#include <fstream>
#include <vector>
#include "ext4_structs.h"

/**
 *  Lê o superbloco de uma imagem de um SA ext4
 *  @param image: objeto std::fstream em que a imagem está aberta
 *  @param sb: objeto super_block em que o superbloco será armazenado
 *  @returns true se o superbloco foi lido com sucesso; false caso contrário
 */
bool read_superblock(std::fstream& image, super_block& sb);

/**
 *  Lê a GDT de um SA ext4
 *  @param ext4_info: objeto ext4_sb_info com as informações do SA
 *  @returns true se a GDT foi lida com sucesso; false caso contrário
 */
bool read_gdt(ext4_sb_info& ext4_info);

/**
 * Lê um inodede um SA ext4
 * @param info: objeto ext4_sb_info com as informações do SA
 * @param inode_num: número do inode
 * @param inode: objeto inode em que o inode será armazenado
 * @returns true se o inode for lido com sucesso; false caso contrário
 */
bool read_inode(ext4_sb_info& info, uint32_t inode_num, inode& inode);

/**
 * Lê todo o conteúdo de um arquivo a partir do seu inode.
 * @param ext4_info: objeto ext4_sb_info com as informações do SA
 * @param inode: objeto inode que representa um arquivo
 * @returns std::vector<char> contendo os bytes do arquivo
 */
std::vector<char> read_inode_content(ext4_sb_info& ext4_info, const inode& inode);

/**
 * Lê todo o conteúdo em uma extent tree
 * @param ext4_info: objeto ext4_sb_info com as informações do SA
 * @param header: objeto ext4_extent_header raiz da extent tere
 * @param leaf_extents: vetor de objetos ext4_extents onde os extents lidos serão armazenados
 * @returns bool se os extents forem lidos com sucesso; false caso contrário
 */
bool read_leaf_extents(ext4_sb_info& ext4_info, const ext4_extent_header* header, std::vector<ext4_extent>& leaf_extents);
/**
 *  Imprime um superbloco
 *  @param sb: objeto super_block em que o superbloco está armazenado
 */
void print_superblock(const ext4_sb_info& ext4_info);

/**
 *  Imprime uma GDT
 *  @param ext4_info: objeto ext4_sb_info com as informações do SA
 */
void print_gdt(const ext4_sb_info& ext4_info);

/**
 * Imprime um inode
 * @param inode: o inode a ser imprimido
 * @param inode_num: o número do inode
 */
void print_inode(const inode& inode, uint32_t inode_num);

/**
 *  Inicializa todos os valores constantes necessários para operar sobre uma imagem ext4
 *  @param image_path: caminho da imagem
 *  @param ext4_info: objeto ext4_sb_info em que as informações do SA serão armazenadas
 *  @returns true se a imagem for lida com sucesso; false caso contrário
 */
bool init_ext4(const std::string& img_path, ext4_sb_info& ext4_info);

/**
 *  Retorna o tamanho de um bloco em um SA
 *  @param sb: objeto super_block em que o superbloco está armazenado
 *  @returns 2 ^ (10 + s_log_block_size)
 */
inline uint64_t get_block_size(const super_block& sb) {
    return static_cast<uint64_t>(1024) << sb.s_log_block_size;
}

/**
 *  Retorna a quantidade de blocos em umx' SA
 *  @param sb: objeto super_block em que o superbloco está armazenado
 *  @returns s_blocks_count_hi << 32 | s_blocks_count_lo
 */
inline uint64_t get_blocks_count(const super_block& sb) {
    return (static_cast<uint64_t>(sb.s_blocks_count_hi) << 32) | sb.s_blocks_count_lo;
}

/**
 *  Retorna a quantidade de grupos de blocos em um SA
 *  @param sb: objeto super_block em que o superbloco está armazenado
 *  @returns (total_blocks + s_blocks_per_group - 1) / s_blocks_per_group
 */
inline uint64_t get_num_groups(const super_block& sb) {
    uint64_t total_blocks = get_blocks_count(sb);

    return (total_blocks + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
}

/**
 *  Retorna o offset de um bloco em um SA
 *  @param block: número do bloco
 *  @param block_size: tamanho do bloco no SA
 *  @returns block * block_size
 */
inline uint64_t get_block_offset(uint64_t block,uint64_t block_size) {
    return block * block_size;
}

/**
 * Retorna o grupo ao qual um inode pertence
 * @param inode_num: número do inode
 * @param sb: objeto super_block em que o superbloco está armazenado
 * @returns (inode_num - 1) / s_inodes_per_group
 */
inline uint32_t get_inode_block_group(uint32_t inode_num, const super_block& sb) {
    return (inode_num - 1) / sb.s_inodes_per_group;
}

/**
 * Retorna o índice de um inode dentro da tabela de inodes do seu grupo
 * @param inode_num: número do inode
 * @param sb: objeto super_block em que o superbloco está armazenado
 * @returns (inode_num - 1) % s_inodes_per_group
 */
inline uint32_t get_inode_index(uint32_t inode_num, const super_block& sb) {
    return (inode_num - 1) % sb.s_inodes_per_group;
}

/**
 * Retorna o bloco inicial da tabela de inodes de um grupo de blocas
 * @param ext4_info: número do inode
 * @param bg: número do grupo de blocos
 * @returns bg_inode_table_hi << 32 | bg_inode_table_lo
 */
inline uint64_t get_inode_table_block(const ext4_sb_info& ext4_info, uint32_t bg) {
    const group_description& gd = ext4_info.gdt[bg];

    return (static_cast<uint64_t>(gd.bg_inode_table_hi) << 32) | gd.bg_inode_table_lo;
}

/**
 * Retorna o número de bytes do arquivo representado por um inode
 * @param inode: objeto inode com o inode desejado
 * @returns i_size_hi << 32 | i_size_lo
 */
inline uint64_t get_file_size(const inode& inode) {
    return (static_cast<uint64_t>(inode.i_size_high) << 32) | inode.i_size_lo;
}

/**
 * Retorna o número de blocos de um extent
 * @param extent: objeto extent com o extent desejado
 * @returns ee_start_hi << 32 | ee_start_lo
 */
inline uint64_t get_extent_phys_block(const ext4_extent& extent) {
    return (static_cast<uint64_t>(extent.ee_start_hi) << 32) | extent.ee_start_lo;
}

/**
 * Retorna o número de blcoos de um índice extent
 * @param entent_idx: objeto ext4_extent_idx com o índice extent desejado
 * @returns ei_leaf_hi << 32 | ei_leaf_lo
 */
inline uint64_t get_extent_idx_phys_block(const ext4_extent_idx& extent_idx) {
    return (static_cast<uint64_t>(extent_idx.ei_leaf_hi) << 32) | extent_idx.ei_leaf_lo;
}

#endif
