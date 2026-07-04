#ifndef EXT4_UTILS_H
#define EXT4_UTILS_H

#include <cstdint>
#include <fstream>
#include <vector>
#include <string>
#include "ext4_structs.h"

class Ext4FS {
private:
    std::fstream image;
    super_block sb;
    std::vector<group_description> gdt;

    uint64_t block_size;
    uint64_t blocks_count;
    uint64_t num_groups;
    uint16_t desc_size;
    uint64_t gdt_offset;

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
     * @param header: objeto ext4_extent_header raiz da extent tere
     * @param leaf_extents: vetor de objetos ext4_extents onde os extents lidos serão armazenados
     * @returns bool se os extents forem lidos com sucesso; false caso contrário
     */
    bool read_leaf_extents(const ext4_extent_header* header, std::vector<ext4_extent>& leaf_extents);

    /**
     * Busca o número do inode correspondente a um caminho de arquivo
     * @param path: o caminho a ser buscado (relativo/asboltuo)
     * @param inode_num: o inode inicial para busca. Se não informado, inode_num = 2 
     * @returns o número do inode encontrado; 0 caso contrário
     */
    uint32_t find_inode_by_path(const std::string& path, uint32_t inode_num = 2);
    
    /**
     * Busca o inode a partri de um dir_entry
     * @param dir_content: o vetor de bytes contendo os blocos do diretório lido
     * @param file_name: o nome do arquivo sendo buscado
     * @returns o número do inode do arquivo buscado; ou 0 caso não seja encontrado
     */
    uint32_t find_inode_by_dir(const std::vector<char>& dir_content, const std::string& file_name);

    /**
     * Imprime um superbloco
     */
    void print_superblock() const;

    /**
     * Imprime uma GDT
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
     * Retorna a quantidade de blocos em umx' SA
     * @returns s_blocks_count_hi << 32 | s_blocks_count_lo
     */
    inline uint64_t get_blocks_count() const {
        return (static_cast<uint64_t>(sb.s_blocks_count_hi) << 32) | sb.s_blocks_count_lo;
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
    inline uint64_t get_block_offset(uint64_t block) const {
        return block * block_size;
    }

    /**
     * Retorna o número do grupo de blocos de um inode
     * @param inode_num: número do inode
     * @returns (inode_num - 1) / s_inodes_per_group
     */
    inline uint32_t get_inode_block_group(uint32_t inode_num) const {
        return (inode_num - 1) / sb.s_inodes_per_group;
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
     * Retorna o bloco inicial da tabela de inodes de um grupo de blocas
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
};

#endif
