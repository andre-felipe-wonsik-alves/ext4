/**
 * Classe Ext4FS — interface para leitura e navegação em imagens ext4.
 *
 * Encapsula toda a lógica de parsing do sistema de arquivos: leitura do
 * superbloco, da Group Descriptor Table (GDT), de inodes, do conteúdo de
 * arquivos e diretórios via extent tree.
 */

#ifndef EXT4_UTILS_H
#define EXT4_UTILS_H

#include <cstdint>
#include <fstream>
#include <vector>
#include <string>
#include "ext4_structs.h"

class Ext4FS {
private:
    std::fstream image; // Arquivo de imagem aberto em modo binário
    super_block sb; // Superbloco lido da imagem
    std::vector<group_description> gdt; // Group Descriptor Table completa

    uint64_t block_size; // Tamanho de bloco em bytes, calculado a partir de s_log_block_size
    uint64_t blocks_count; // Total de blocos do SA
    uint64_t num_groups; // Número de grupos de blocos
    uint16_t desc_size; // Tamanho de cada group descriptor em bytes (32 ou 64)
    uint64_t gdt_offset; // Offset em bytes da GDT na imagem

    /**
     * Lê o superbloco de uma imagem de um SA ext4
     * @returns true se o superbloco foi lido com sucesso; false caso contrário
     */
    bool read_superblock();

    /**
     * Lê a GDT de um SA ext4
     * @returns true se a GDT foi lida com sucesso; false caso contrário
     */
    bool read_gdt();

public:
    Ext4FS() = default;
    
    ~Ext4FS() {
        if (image.is_open()) {
            image.close();
        }
    }

    /**
     * Inicializa os valores necessários para operar sobre uma imagem ext4t
     * @param img_path: caminho da imagem
     * @returns true se a imagem for lida com sucesso; false caso contrário
     */
    bool init(const std::string& img_path);

    /**
     * Lê um inodede um SA ext4
     * @param inode_num: número do inode
     * @param inode: objeto inode em que o inode será armazenado
     * @returns true se o inode for lido com sucesso; false caso contrário
     */
    bool read_inode(uint32_t inode_num, inode& inode);

    /**
     * Lê todo o conteúdo de um arquivo a partir do seu inode.
     * @param inode: objeto inode que representa um arquivo
     * @returns std::vector<char> contendo os bytes do arquivo
     */
    std::vector<char> read_inode_content(const inode& inode);

    /**
     * Lê todo o conteúdo em uma extent tree
     * @param header: objeto ext4_extent_header raiz da extent tree
     * @param leaf_extents: vetor de objetos ext4_extents onde os extents lidos serão armazenados
     * @returns bool se os extents forem lidos com sucesso; false caso contrário
     */
    bool read_leaf_extents(const ext4_extent_header* header, std::vector<ext4_extent>& leaf_extents);

    /**
     * Busca o número do inode correspondente a um caminho de arquivo
     * @param path: o caminho a ser buscado (relativo/absoluto)
     * @param inode_num: o inode inicial para busca. Se não informado, inode_num = 2 (raiz)
     * @returns o número do inode encontrado; 0 caso contrário
     */
    uint32_t find_inode_by_path(const std::string& path, uint32_t inode_num = 2);
    
    /**
     * Busca o inode a partir de um dir_entry
     * @param dir_content: o vetor de bytes contendo os blocos do diretório lido
     * @param file_name: o nome do arquivo sendo buscado
     * @returns o número do inode do arquivo buscado; ou 0 caso não seja encontrado
     */
    uint32_t find_inode_by_dir(const std::vector<char>& dir_content, const std::string& file_name);

    /**
     * Verifica se um inode está sem uso
     * @param inode_num: número do inode
     * @returns true se o inode estiver em uso; false caso contrário
     */
    bool inode_is_used(uint32_t inode_num);

    /**
     * Verifica se um bloco está sem uso
     * @param block_num: número do bloco
     * @returns true se o inode estiver em uso; false caso contrário
     */
    bool block_is_used(uint64_t block_num);

    /**
     * Imprime todos os campos do superbloco na saída padrão.
     */
    void print_superblock() const;

    /**
     * Imprime todos os campos de cada group descriptor da GDT na saída padrão.
     */
    void print_gdt() const;

    /**
     * Imprime um inode
     * @param inode: o inode a ser imprimido
     * @param inode_num: o número do inode
     */
    void print_inode(const inode& inode, uint32_t inode_num) const;

    /**
     * Retorna o tamanho de um bloco em um SA
     * @returns 2 ^ (10 + s_log_block_size)
     */
    inline uint64_t get_block_size() const {
        return static_cast<uint64_t>(1024) << sb.s_log_block_size;
    }

    /**
     * Retorna a quantidade de blocos em um SA
     * @returns s_blocks_count_hi << 32 | s_blocks_count_lo
     */
    inline uint64_t get_blocks_count() const {
        return (static_cast<uint64_t>(sb.s_blocks_count_hi) << 32) | sb.s_blocks_count_lo;
    }

    /**
     * Retorna a quantidade de blocos livres em um SA
     * @returns s_free_blocks_count_hi << 32 | s_free_blocks_count_lo
     */
    inline uint64_t get_free_blocks_count() const {
        return (static_cast<uint64_t>(sb.s_free_blocks_count_hi) << 32) | sb.s_free_blocks_count_lo;
    }

    /**
     * Retorna a quantidade de inodes em um SA
     * @returns s_inodes_count
     */
    inline uint32_t get_inodes_count() const {
        return sb.s_inodes_count;
    }

    /**
     * Retorna a quantidade de inodes livres em um SA
     * @returns s_free_inodes_count
     */
    inline uint32_t get_free_inodes_count() const {
        return sb.s_free_inodes_count;
    }

    /**
     * Retorna a quantidade de grupos de blocos em um SA
     * @returns ceil(blocks_count / s_blocks_per_group)
     */
    inline uint64_t get_num_groups() const {
        uint64_t total_blocks = get_blocks_count();
        return (total_blocks + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    }

    /**
     * Retorna o offset, em bytes, de um bloco no SA
     * @param block: número do bloco
     * @returns block * block_size
     */
    inline uint64_t get_block_offset(uint64_t block_num) const {
        return block_num * block_size;
    }

    /**
     * Retorna o número do grupo de blocos de um inode
     * @param inode_num: número do inode
     * @returns (inode_num - 1) / s_inodes_per_group
     */
    inline uint32_t get_inode_block_group(uint32_t inode_num) const {
        return (inode_num - 1) / sb.s_inodes_per_group;
    }

    inline uint32_t get_block_block_group(uint64_t block_num) const {
        return (block_num - sb.s_first_data_block) / sb.s_blocks_per_group;
    }

    /**
     * Retorna o índice de um inode no seu respectivo grupo de blocos
     * @param inode_num: número do inode
     * @returns (inode_num - 1) % s_inodes_per_group
     */
    inline uint32_t get_inode_index(uint32_t inode_num) const {
        return (inode_num - 1) % sb.s_inodes_per_group;
    }

    /**
     * Retorna o bloco inicial da tabela de inodes de um grupo de blocos
     * @param bg: número do grupo de blocos
     * @returns bg_inode_table_hi << 32 | bg_inode_table_lo
     */
    inline uint64_t get_inode_table_block(uint32_t bg) const {
        const group_description& gd = gdt[bg];
        return (static_cast<uint64_t>(gd.bg_inode_table_hi) << 32) | gd.bg_inode_table_lo;
    }

    /**
     * Retorna o número de bytes do arquivo representado por um inode
     * @param inode: objeto inode com o inode desejado
     * @returns i_size_hi << 32 | i_size_lo
     */
    inline uint64_t get_file_size(const inode& inode) const {
        return (static_cast<uint64_t>(inode.i_size_high) << 32) | inode.i_size_lo;
    }

    /**
     * Retorna o número de blocos de um extent
     * @param extent: objeto extent com o extent desejado
     * @returns ee_start_hi << 32 | ee_start_lo
     */
    inline uint64_t get_extent_phys_block(const ext4_extent& extent) const {
        return (static_cast<uint64_t>(extent.ee_start_hi) << 32) | extent.ee_start_lo;
    }

    /**
     * Retorna o número de blocos de um extent node
     * @param extent_idx: objeto extent_idx com o extent_idx desejado
     * @returns ei_leaf_hi << 32 | ei_leaf_lo
     */
    inline uint64_t get_extent_idx_phys_block(const ext4_extent_idx& extent_idx) const {
        return (static_cast<uint64_t>(extent_idx.ei_leaf_hi) << 32) | extent_idx.ei_leaf_lo;
    }

    /**
     * Valida se um inode representa um diretório (i_mode == 0x4000)
     * @param inode: objeto inode a ser validado
     * @returns true se o inode for um diretório; false caso contrário
     */
    inline bool inode_is_dir(const inode& inode) const {
        return (inode.i_mode & 0x4000) == 0x4000;
    }

    /**
     * Retorna o bloco onde está o mapa de bits de inodes um grupo de blocos
     * @param bg: número do grupo de blocos
     * @returns bg_inode_bitmap_hi << 32 | bg_inode_bitmap_lo
     */
    inline uint64_t get_inode_bitmap_block(uint32_t bg) const {
        const group_description& gd = gdt[bg];
        return (static_cast<uint64_t>(gd.bg_inode_bitmap_hi) << 32) | gd.bg_inode_bitmap_lo;
    }

    /**
     * Retorna o bloco onde está o mapa de bits de blocos de um grupo de blocos
     * @param bg: número do grupo de blocos
     * @returns bg_block_bitmap_hi << 32 | bg_block_bitmap_lo
     */
    inline uint64_t get_block_bitmap_block(uint32_t bg) const {
        const group_description& gd = gdt[bg];
        return (static_cast<uint64_t>(gd.bg_block_bitmap_hi) << 32) | gd.bg_block_bitmap_lo;
    }

     /**
     * Retorna o offset do bit que representa um inode no mapa de bits de inodes
     * @param inode_num: número do inode
     * @returns (inode_num - 1) % s_inodes_per_group;
     */
    inline uint32_t get_inode_bitmap_offset(uint32_t inode_num) const {
        return (inode_num - 1) % sb.s_inodes_per_group;
    }

    /**
     * Retorna o offset do bit que representa um bloco no mapade bits de blocos
     * @param block_num: número do bloco
     * @returns (block_num - s_first_data_block) % s_blocks_per_group
     */
    inline uint32_t get_block_bitmap_offset(uint64_t block_num) const {
        return (block_num - sb.s_first_data_block) % sb.s_blocks_per_group;
    }

    /**
     * Verifica se um bit está ativo em um mapa de bits
     * @param bitmap: vetor contendo o bitmap
     * @param bit_offset: offset do bit a ser validado
     * @returns true se o bit for 1; false caso contrário
     */    
    inline bool test_bit(const std::vector<char>& bitmap, uint32_t bit_offset) const {
        uint32_t byte_idx = bit_offset / 8;
        uint32_t bit_idx = bit_offset % 8;
        return (bitmap[byte_idx] & (1 << bit_idx)) != 0;
    }

    /**
     * Ativa um bit em um mapa de bits
     * @param bitmap: vetor contendo o bitmap
     * @param bit_offset: offset do bit a ser ativado
     */    
    inline void set_bit(std::vector<char>& bitmap, uint32_t bit_offset) const {
        uint32_t byte_idx = bit_offset / 8;
        uint32_t bit_idx = bit_offset % 8;
        bitmap[byte_idx] |= (1 << bit_idx);
    }

    /**
     * Limpa um bit em um mapa de bits
     * @param bitmap: vetor contendo o bitmap
     * @param bit_offset: offset do bit a ser limpo
     */       
    inline void clear_bit(std::vector<char>& bitmap, uint32_t bit_offset) const {
        uint32_t byte_idx = bit_offset / 8;
        uint32_t bit_idx = bit_offset % 8;
        bitmap[byte_idx] &= ~(1 << bit_idx);
    }
};

#endif
