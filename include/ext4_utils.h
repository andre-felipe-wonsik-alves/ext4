/**
 * Classe Ext4FS — interface para leitura e navegação em imagens ext4.
 *
 * Encapsula toda a lógica de parsing do sistema de arquivos: leitura do
 * superbloco, da Group Descriptor Table (GDT), de inodes, do conteúdo de
 * arquivos e diretórios via extent tree.
 */
#ifndef EXT4_UTILS_H
#define EXT4_UTILS_H

#include "ext4_structs.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class Ext4FS {
private:
  std::fstream image; // Arquivo de imagem aberto em modo binário
  super_block sb;     // Superbloco lido da imagem
  std::vector<group_description> gdt; // Group Descriptor Table completa

  uint64_t block_size;   // Tamanho de bloco em bytes, calculado a partir de
                         // s_log_block_size
  uint64_t blocks_count; // Total de blocos do SA
  uint64_t num_groups;   // Número de grupos de blocos
  uint16_t desc_size;    // Tamanho de cada group descriptor em bytes (32 ou 64)
  uint64_t gdt_offset;   // Offset em bytes da GDT na imagem

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

  /**
   * @param inode_in: inode 
   * @param logical_block: bloco lógico buscado
   * @param out_phys_block: bloco físico já mapeado, se encontrado
   * @param out_len: comprimento do extent encontrado
   * @returns true se o bloco lógico já estiver mapeado; false caso contrário
   */
  bool find_mapped_block(const inode &inode_in, uint32_t logical_block,
                         uint64_t &out_phys_block, uint16_t &out_len) const;

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
  bool init(const std::string &img_path);

  /**
   * Lê um inodede um SA ext4
   * @param inode_num: número do inode
   * @param inode: objeto inode em que o inode será armazenado
   * @returns true se o inode for lido com sucesso; false caso contrário
   */
  bool read_inode(uint32_t inode_num, inode &inode);

  /**
   * Lê todo o conteúdo de um arquivo a partir do seu inode.
   * @param inode: objeto inode que representa um arquivo
   * @returns std::vector<char> contendo os bytes do arquivo
   */
  std::vector<char> read_inode_content(const inode &inode);

  /**
   * Lê todo o conteúdo em uma extent tree
   * @param header: objeto ext4_extent_header raiz da extent tree
   * @param leaf_extents: vetor de objetos ext4_extents onde os extents lidos
   * serão armazenados
   * @returns bool se os extents forem lidos com sucesso; false caso contrário
   */
  bool read_leaf_extents(const ext4_extent_header *header,
                         std::vector<ext4_extent> &leaf_extents);

  /**
   * Busca o número do inode correspondente a um caminho de arquivo
   * @param path: o caminho a ser buscado (relativo/absoluto)
   * @param inode_num: o inode inicial para busca. Se não informado, inode_num =
   * 2 (raiz)
   * @returns o número do inode encontrado; 0 caso contrário
   */
  uint32_t find_inode_by_path(const std::string &path, uint32_t inode_num = 2);

  /**
   * Busca o inode a partir de um dir_entry
   * @param dir_content: o vetor de bytes contendo os blocos do diretório lido
   * @param file_name: o nome do arquivo sendo buscado
   * @returns o número do inode do arquivo buscado; ou 0 caso não seja
   * encontrado
   */
  uint32_t find_inode_by_dir(const std::vector<char> &dir_content,
                             const std::string &file_name);

  /**
   * Verifica se um inode está sem uso
   * @param inode_num: número do inode
   * @returns true se o inode estiver em uso; false caso contrário
   */
  bool inode_is_used(uint32_t inode_num);

  /**
   * Verifica se um bloco está sem uso
   * @param block_num: número do bloco
   * @returns true se o bloco estiver em uso; false caso contrário
   */
  bool block_is_used(uint64_t block_num);

  /**
   * Aloca o primeiro inode livre encontrado no SA
   * @returns o número do inode alocado; 0 caso contrário
   */
  uint32_t alloc_inode();

  /**
   * Persiste o superbloco em memória (sb) na imagem, no offset fixo 1024.
   * @returns true se a escrita foi bem-sucedida; false caso contrário
   */
  bool update_sb();

  /**
   * Persiste um group descriptor da GDT em memória na imagem.
   * @param bg: número do grupo de blocos
   * @returns true se a escrita foi bem-sucedida; false caso contrário
   */
  bool update_gdt_entry(uint64_t bg);

  /**
   * Persiste o bitmap de inodes de um grupo na imagem.
   * @param bg: número do grupo de blocos
   * @param bitmap: vetor com o conteúdo atualizado do bitmap (block_size bytes)
   * @returns true se a escrita foi bem-sucedida; false caso contrário
   */
  bool update_inode_bitmap(uint64_t bg, const std::vector<char> &bitmap);

  /**
   * Persiste o bitmap de blocos de um grupo na imagem.
   * @param bg: número do grupo de blocos
   * @param bitmap: vetor com o conteúdo atualizado do bitmap (block_size bytes)
   * @returns true se a escrita foi bem-sucedida; false caso contrário
   */
  bool update_block_bitmap(uint64_t bg, const std::vector<char> &bitmap);

    /**
   * Persiste um inode na tabela de inodes do seu grupo na imagem.
   * @param inode_num: número do inode (base 1)
   * @param inode_in: inode com os dados atualizados
   * @returns true se a escrita foi bem-sucedida; false caso contrário
   */
  bool update_inode(uint32_t inode_num, const inode& inode_in);

    /**
     * Atualiza o tamanho de um inode em memória e na imagem.
     * @param inode_num: número do inode (base 1)
     * @param inode_in: inode a ser atualizado
     * @param new_size: novo tamanho do arquivo em bytes
     * @returns true se a atualização foi bem-sucedida; false caso contrário
     */
    bool update_inode_size(uint32_t inode_num, inode& inode_in, uint64_t new_size);

    /**
     * Aloca até 'count' blocos de dados livres e contíguos no SA
     * @param count: quantidade máxima de blocos contíguos a alocar
     * @param allocated_count: quantidade de blocos alocados
     * @returns o número do primeiro bloco alocado; 0 caso contrário
     */
    uint64_t alloc_blocks(uint64_t count, uint64_t& allocated_count);

    /**
     * Escreve um inode de volta na imagem, no offset correto da inode table.
     * @param inode_num: número do inode
     * @param inode_in: objeto inode com os dados a serem gravados
     * @returns true em caso de sucesso; false caso contrário
     */
    bool write_inode(uint32_t inode_num, const inode& inode_in);

    /**
     * Insere um novo extent na extent tree de um inode
     *
     *
     * @param inode_num: número do inode a ser atualizado
     * @param inode_in: inode lido; será atualizado em memória e na imagem
     * @param logical_block: bloco lógico inicial do extent
     * @param phys_block: bloco físico inicial do extent
     * @param len: comprimento em blocos do extent
     * @returns true em caso de sucesso; false caso contrário
     */
    bool write_extent_to_inode(uint32_t inode_num, inode& inode_in,
                               uint32_t logical_block,
                               uint64_t phys_block,
                               uint16_t len);

    /**
     * Escreve um buffer de dados diretamente em um bloco físico do sistema de arquivos.
     * @param phys_block Número do bloco físico absoluto
     * @param data Vetor de bytes a ser gravado
     * @returns true se a escrita foi bem-sucedida; false caso contrário
     */
    bool write_block_bytes(uint64_t phys_block, const std::vector<char>& buffer);

    /**
     * Escreve dados em um bloco lógico de um arquivo, gereneciando alocação física,
     * atualização da árvore de extents e ajuste do tamanho do arquivo de forma consistente.
     * @param inode_num Número do inode do arquivo destino
     * @param inode_in Referência para a struct inode em memória
     * @param logical_block O bloco lógico onde o dado deve começar
     * @param buffer Vetor contendo os bytes brutos a serem escritos
     * @return true se toda a operação foi gravada e persistida com sucesso; false caso contrário
     */
    bool write_to_file(uint32_t inode_num, inode& inode_in, 
                       uint32_t logical_block, const std::vector<char>& buffer);

  /** Localiza o inode bitmap e zera o bit de um inode e 
   * atualiza os contadores de inodes livres na GDT e no SB
   * @param inode_num Número do inode a ser removido
   * @param is_dir indica se o inode pertencia a um diretório 
   * @return true se o inode for removido; false caso contrário
   */
    bool free_inode(uint32_t inode_num, bool is_dir);

  /** Localiza o block bitmap e zera os bits de count blocos, 
   * atualiza os contadores de blocos livres na GDT e no SB
   * @param inode_num número do inode a ser removido
   * @param count quantidade de blocos contíguos a serem liberados 
   * @return true se os blocos forem removidos; false caso contrário
   */
    bool free_blocks(uint64_t start_phys_block, uint64_t count);

    /**
   * Remove uma entrada de um diretório.
   * @param parent_inode_num inode do diretório 
   * @param target_name nome do arquivo ou diretório a ser removido
   * @return inode do arquivo que acabou de ser removido da pasta; retorna 0 se o arquivo não for encontrado
   */
    uint32_t remove_dir_entry(uint32_t parent_inode_num, const std::string &target_name);

    uint32_t find_inode_in_dir(uint32_t parent_inode_num, const std::string &name);

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
  void print_inode(const inode &inode, uint32_t inode_num) const;

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
    return (static_cast<uint64_t>(sb.s_blocks_count_hi) << 32) |
           sb.s_blocks_count_lo;
  }

  /**
   * Retorna a quantidade de blocos livres em um SA
   * @returns s_free_blocks_count_hi << 32 | s_free_blocks_count_lo
   */
  inline uint64_t get_free_blocks_count() const {
    return (static_cast<uint64_t>(sb.s_free_blocks_count_hi) << 32) |
           sb.s_free_blocks_count_lo;
  }

  /**
   * Retorna a quantidade de inodes em um SA
   * @returns s_inodes_count
   */
  inline uint32_t get_inodes_count() const { return sb.s_inodes_count; }

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
    const group_description &gd = gdt[bg];
    return (static_cast<uint64_t>(gd.bg_inode_table_hi) << 32) |
           gd.bg_inode_table_lo;
  }

  /**
   * Retorna o número de bytes do arquivo representado por um inode
   * @param inode: objeto inode com o inode desejado
   * @returns i_size_hi << 32 | i_size_lo
   */
  inline uint64_t get_file_size(const inode &inode) const {
    return (static_cast<uint64_t>(inode.i_size_high) << 32) | inode.i_size_lo;
  }

  /**
   * Retorna o número de blocos de um extent
   * @param extent: objeto extent com o extent desejado
   * @returns ee_start_hi << 32 | ee_start_lo
   */
  inline uint64_t get_extent_phys_block(const ext4_extent &extent) const {
    return (static_cast<uint64_t>(extent.ee_start_hi) << 32) |
           extent.ee_start_lo;
  }

  /**
   * Retorna o número de blocos de um extent node
   * @param extent_idx: objeto extent_idx com o extent_idx desejado
   * @returns ei_leaf_hi << 32 | ei_leaf_lo
   */
  inline uint64_t
  get_extent_idx_phys_block(const ext4_extent_idx &extent_idx) const {
    return (static_cast<uint64_t>(extent_idx.ei_leaf_hi) << 32) |
           extent_idx.ei_leaf_lo;
  }

  /**
   * Valida se um inode representa um diretório (i_mode == 0x4000)
   * @param inode: objeto inode a ser validado
   * @returns true se o inode for um diretório; false caso contrário
   */
  inline bool inode_is_dir(const inode &inode) const {
    return (inode.i_mode & 0x4000) == 0x4000;
  }

  /**
   * Retorna o bloco onde está o mapa de bits de inodes um grupo de blocos
   * @param bg: número do grupo de blocos
   * @returns bg_inode_bitmap_hi << 32 | bg_inode_bitmap_lo
   */
  inline uint64_t get_inode_bitmap_block(uint32_t bg) const {
    const group_description &gd = gdt[bg];
    return (static_cast<uint64_t>(gd.bg_inode_bitmap_hi) << 32) |
           gd.bg_inode_bitmap_lo;
  }

  /**
   * Retorna o bloco onde está o mapa de bits de blocos de um grupo de blocos
   * @param bg: número do grupo de blocos
   * @returns bg_block_bitmap_hi << 32 | bg_block_bitmap_lo
   */
  inline uint64_t get_block_bitmap_block(uint32_t bg) const {
    const group_description &gd = gdt[bg];
    return (static_cast<uint64_t>(gd.bg_block_bitmap_hi) << 32) |
           gd.bg_block_bitmap_lo;
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
  inline bool test_bit(const std::vector<char> &bitmap,
                       uint32_t bit_offset) const {
    uint32_t byte_idx = bit_offset / 8;
    uint32_t bit_idx = bit_offset % 8;
    return (bitmap[byte_idx] & (1 << bit_idx)) != 0;
  }

  /**
   * Ativa um bit em um mapa de bits
   * @param bitmap: vetor contendo o bitmap
   * @param bit_offset: offset do bit a ser ativado
   */
  inline void set_bit(std::vector<char> &bitmap, uint32_t bit_offset) const {
    uint32_t byte_idx = bit_offset / 8;
    uint32_t bit_idx = bit_offset % 8;
    bitmap[byte_idx] |= (1 << bit_idx);
  }

  /**
   * Limpa um bit em um mapa de bits
   * @param bitmap: vetor contendo o bitmap
   * @param bit_offset: offset do bit a ser limpo
   */
  inline void clear_bit(std::vector<char> &bitmap, uint32_t bit_offset) const {
    uint32_t byte_idx = bit_offset / 8;
    uint32_t bit_idx = bit_offset % 8;
    bitmap[byte_idx] &= ~(1 << bit_idx);
  }

  /**
   * Retorna o número de inodes livres de um grupo de blocos.
   * @param bg: número do grupo de blocos
   * @returns bg_free_inodes_count_hi << 16 | bg_free_inodes_count_lo
   */
  inline uint32_t get_gd_free_inodes_count(uint64_t bg) const {
    return (static_cast<uint32_t>(gdt[bg].bg_free_inodes_count_hi)
            << 16) | // << 16 pois o campo hi é 16 bits
           gdt[bg].bg_free_inodes_count_lo;
  }

  /**
   * Atualiza o contador de inodes livres de um grupo de blocos em memória.
   * @param bg: número do grupo de blocos
   * @param val: novo valor do contador
   */
  inline void set_gd_free_inodes_count(uint64_t bg, uint32_t val) {
    gdt[bg].bg_free_inodes_count_lo =
        static_cast<uint16_t>(val & 0xFFFF); // 0xFFFF pois o campo lo é 16 bits
    gdt[bg].bg_free_inodes_count_hi =
        static_cast<uint16_t>((val >> 16) & 0xFFFF);
  }

  /**
   * Retorna o número de blocos livres de um grupo de blocos
   * @param bg: número do grupo de blocos
   * @returns bg_free_blocks_count_hi << 16 | bg_free_blocks_count_lo
   */
  inline uint32_t get_gd_free_blocks_count(uint64_t bg) const {
    return (static_cast<uint32_t>(gdt[bg].bg_free_blocks_count_hi) << 16) |
           gdt[bg].bg_free_blocks_count_lo;
  }

  /**
   * Atualiza o contador de blocos livres de um grupo de blocos em memória.
   * @param bg: número do grupo de blocos
   * @param val: novo valor do contador
   */
  inline void set_gd_free_blocks_count(uint64_t bg, uint32_t val) {
    gdt[bg].bg_free_blocks_count_lo = static_cast<uint16_t>(val & 0xFFFF);
    gdt[bg].bg_free_blocks_count_hi =
        static_cast<uint16_t>((val >> 16) & 0xFFFF);
  }

  /**
   * Retorna o número de diretórios usados em um grupo de blocos
   * @param bg: número do grupo de blocos
   * @returns bg_used_dirs_count_hi << 16 | bg_used_dirs_count_lo
   */
  inline uint32_t get_gd_used_dirs_count(uint64_t bg) const {
    return (static_cast<uint32_t>(gdt[bg].bg_used_dirs_count_hi) << 16) |
           gdt[bg].bg_used_dirs_count_lo;
  }

  /**
   * Atualiza o contador de diretórios usados de um grupo de blocos em memória
   * @param bg: número do grupo de blocos
   * @param val: novo valor do contador
   */
  inline void set_gd_used_dirs_count(uint64_t bg, uint32_t val) {
    gdt[bg].bg_used_dirs_count_lo = static_cast<uint16_t>(val & 0xFFFF);
    gdt[bg].bg_used_dirs_count_hi = static_cast<uint16_t>((val >> 16) & 0xFFFF);
  }

  /**
   * Retorna o número do primeiro bloco de dados de um grupo de blocos.
   * @param bg: número do grupo de blocos
   * @returns primeiro bloco do grupo
   */
  inline uint64_t get_group_first_block(uint64_t bg) const {
    return static_cast<uint64_t>(bg) * sb.s_blocks_per_group +
           sb.s_first_data_block;
  }

  /**
   * Converte um índice local de bloco dentro de um grupo para número absoluto.
   * @param bg: número do grupo de blocos
   * @param local_idx: índice do bloco dentro do grupo
   * @returns número absoluto do bloco
   */
  inline uint64_t get_abs_block(uint64_t bg, uint32_t local_idx) const {
    return get_group_first_block(bg) + local_idx;
  }

  /**
   * Retorna o offset em bytes do GDT entry de um grupo de blocos na imagem.
   * @param bg: número do grupo de blocos
   * @returns gdt_offset + bg * desc_size
   */
  inline uint64_t get_gdt_entry_offset(uint64_t bg) const {
    uint16_t current_desc_size = (sb.s_desc_size == 0) ? 32 : sb.s_desc_size;
    return gdt_offset + bg * current_desc_size;
  }

  /**
   * Insere uma nova entrada de diretório (dir_entry) no diretório pai. 
   * Caso não haja espaço suficiente, aloca um novo bloco para o diretório pai.
   *
   * @param parent_inode_num: número do inode do diretório pai
   * @param parent_inode: inode do diretório pai
   * @param new_inode_num: número do inode da nova entrada
   * @param name: nome da nova entrada
   * @param file_type: tipo do arquivo (EXT4_FT_DIR = 2, EXT4_FT_REG_FILE = 1,
   * etc.)
   * @returns true se a entrada foi escrita com sucesso; false caso contrário
   */
  bool write_dir_entry(uint32_t parent_inode_num, uint32_t new_inode_num,
                       const std::string &name, uint8_t file_type);

  /**
   * Cria um novo inode regular ou de diretório e registra a entrada no
   * diretório pai.
   * @param parent_inode_num inode do diretório pai
   * @param name nome da nova entrada
   * @param file_type tipo da entrada (1 = arquivo, 2 = diretório)
   * @returns inode criado, ou 0 em caso de erro
   */
  uint32_t create_file_entry(uint32_t parent_inode_num, const std::string &name,
                             uint8_t file_type);

  /**
   * Remove uma entrada de diretório do diretório pai e libera o inode e os
   * blocos associados.
   * @param parent_inode_num inode do diretório pai
   * @param target_name nome da entrada a ser removida
   * @param target_inode_num inode alvo (opcional, usado pelo shell)
   * @returns true se a entrada foi removida com sucesso; false caso contrário
   */
  bool unlink_entry(uint32_t parent_inode_num, const std::string &target_name,
                    uint32_t target_inode_num);

  /**
   * Verifica se um diretório está vazio, considerando apenas "." e "..".
   * @param inode_num inode do diretório
   * @returns true se o diretório estiver vazio; false caso contrário
   */
  bool is_dir_empty(uint32_t inode_num);

  /**
   * Renomeia uma entrada de diretório no diretório pai.
   * @param parent_inode_num inode do diretório pai
   * @param old_name nome atual da entrada
   * @param new_name novo nome da entrada
   * @returns true se o rename foi bem-sucedido; false caso contrário
   */
  bool rename_entry(uint32_t parent_inode_num, const std::string &old_name,
                    const std::string &new_name);

  /**
   * Calcula rec_len da dir_entry
   */
  static inline uint16_t dir_ent_min_len(uint8_t name_len) {
    return (8 + name_len + 3) & ~3u; // round up to multiple of 4
  }
};

#endif
