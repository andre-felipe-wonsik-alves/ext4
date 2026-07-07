/**
 * Implementação da classe Ext4FS — parsing e navegação em imagens ext4.
 */

#include "ext4_utils.h"
#include "io_utils.h"
#include "ext4checksum.h"
#include <iomanip>
#include <iostream>
#include <string_view>
#include <cstring>
#include <cstdlib>

// init: abre a imagem e inicializa todos os metadados do SA
bool Ext4FS::init(const std::string &img_path)
{
  if (!open_image(img_path, image))
  {
    std::cerr << "error on open_image() in init()\n";
    return false;
  }

  if (!read_superblock())
  {
    std::cerr << "error on read_superblock() in init()\n";
    return false;
  }

  // Calcula os atributos derivados do superbloco para uso frequente
  block_size = get_block_size();
  blocks_count = get_blocks_count();
  num_groups = get_num_groups();
  desc_size = sb.s_desc_size;

  /**
   * A GDT fica no bloco imediatamente após o superbloco.
   * Se block_size == 1024, o superbloco ocupa o bloco 1 (offset 1024),
   * então a GDT começa no bloco 2 (offset 2048).
   * Se block_size > 1024, o superbloco está dentro do bloco 0,
   * e a GDT começa no bloco 1 (offset = block_size).
   */
  gdt_offset = ((block_size == 1024) ? 2 : 1) * block_size;

  if (!read_gdt())
  {
    std::cerr << "error on read_gdt() in init() \n";
    return false;
  }

  return true;
}

// read_superblock: lê os 1024 bytes do superbloco no offset fixo 1024
bool Ext4FS::read_superblock()
{
  // O superbloco do ext4 sempre reside no offset 1024 bytes da partição */
  if (!read_bytes(image, 1024, &sb, sizeof(super_block)))
  {
    return false;
  }

  // Validação de Checksum
  uint32_t calculated = checksum_superblock(reinterpret_cast<char *>(&sb));
  if (calculated != sb.s_checksum)
  {
    std::cerr << "\n[CRITICAL ERROR] Superblock checksum mismatch! The filesystem structure is corrupted.\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << sb.s_checksum << "\n"
              << "Terminating terminal execution immediately to prevent data corruption.\n\n";
    std::exit(1);
  }

  return true;
}

// read_gdt: lê todos os group descriptors da Group Descriptor Table
bool Ext4FS::read_gdt()
{
  /**
   * Se s_desc_size for 0 (SA sem a feature 64bit), cada descriptor tem 32 bytes.
   * Com a feature 64bit activa, s_desc_size geralmente é 64.
   */
  uint16_t current_desc_size = sb.s_desc_size == 0 ? 32 : sb.s_desc_size;
  uint64_t offset = gdt_offset;

  // Lê cada group descriptor e armazena no vetor gdt
  for (uint64_t i = 0; i < num_groups; i++)
  {
    group_description gd{};
    std::memset(&gd, 0, sizeof(gd));

    if (!read_bytes(image, offset, &gd, current_desc_size))
    {
      return false;
    }

    // Validação de Checksum do Group Descriptor
    uint16_t calculated = checksum_group(reinterpret_cast<char *>(sb.s_uuid), i, reinterpret_cast<char *>(&gd));
    if (calculated != gd.bg_checksum)
    {
      std::cerr << "\n[CRITICAL ERROR] Group Descriptor " << i << " checksum mismatch! The GDT structure is corrupted.\n"
                << "Calculated Checksum: " << calculated << "\n"
                << "Stored Checksum:     " << gd.bg_checksum << "\n"
                << "Terminating terminal execution immediately to prevent data corruption.\n\n";
      std::exit(1);
    }

    gdt.push_back(gd);

    // Avança o offset para o próximo descriptor
    offset += current_desc_size;
  }

  return true;
}

// read_inode: localiza e lê um inode específico pelo seu número
bool Ext4FS::read_inode(uint32_t inode_num, inode &inode_out)
{
  // Inode 0 não existe; inodes válidos começam em 1
  if (inode_num == 0 || inode_num > sb.s_inodes_count)
  {
    return false;
  }

  /**
   * Localização do inode no disco:
   *   1. Determinar em qual grupo de blocos o inode reside
   *   2. Determinar o índice do inode dentro desse grupo
   *   3. Calcular o offset absoluto na tabela de inodes do grupo
   */
  uint32_t bg = get_inode_block_group(inode_num);
  uint32_t index = get_inode_index(inode_num);

  uint64_t inode_offset = index * sb.s_inode_size;
  uint64_t inode_table_block = get_inode_table_block(bg);
  uint64_t block_offset = get_block_offset(inode_table_block) + inode_offset;

  // Criamos um buffer de 256 bytes preenchido com zero para rodar o checksum com segurança
  std::vector<char> inode_buf(256, 0);
  size_t bytes_to_read = std::min(static_cast<size_t>(sb.s_inode_size), inode_buf.size());
  if (!read_bytes(image, block_offset, inode_buf.data(), bytes_to_read))
  {
    return false;
  }

  // Copia os dados lidos para a estrutura do inode
  size_t copy_size = std::min(sizeof(inode), static_cast<size_t>(sb.s_inode_size));
  std::memcpy(&inode_out, inode_buf.data(), copy_size);

  // Validação de Checksum
  uint32_t calculated = checksum_inode(reinterpret_cast<char *>(sb.s_uuid), inode_num, inode_out.i_generation, inode_buf.data());
  uint32_t stored_checksum = (static_cast<uint32_t>(inode_out.i_checksum_hi) << 16) | inode_out.osd2.linux2.l_i_checksum_lo;
  if (calculated != stored_checksum)
  {
    std::cerr << "\n[ERROR] Inode " << inode_num << " checksum mismatch! The file or directory entry is corrupted.\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << stored_checksum << "\n"
              << "Skipping reading to prevent using corrupted inode metadata.\n\n";
    return false;
  }

  return true;
}

// read_inode_content: lê o conteúdo completo de um arquivo via extent tree
std::vector<char> Ext4FS::read_inode_content(const inode &inode_in, uint32_t inode_num)
{
  /**
   * O campo i_block[] do inode contém a raiz da extent tree.
   * Fazemos um cast direto para ext4_extent_header*, pois usamos #pragma pack(1)
   * e os bytes estão contíguos.
   */
  ext4_extent_header *header = (ext4_extent_header *)inode_in.i_block;
  std::vector<ext4_extent> leaf_extents;

  if (!read_leaf_extents(header, leaf_extents, inode_num, inode_in.i_generation))
  {
    std::cerr << "error on read_leaf_extents() in read_inode_content()\n";
    return {};
  }

  uint64_t file_size = get_file_size(inode_in);
  uint64_t rem_bytes = file_size;
  std::vector<char> inode_content; // i_block pode ser maior que file_size em algum momento?
  inode_content.reserve(file_size);

  for (const auto &extent : leaf_extents)
  {
    if (rem_bytes == 0)
      break;

    uint64_t extent_phys_block = get_extent_phys_block(extent);
    uint64_t offset = get_block_offset(extent_phys_block);

    /**
     * ee_len > 32768 indica um extent não-inicializado (uninitialized extent).
     * O comprimento real em blocos é ee_len - 32768.
     * Isso evita ler dados de blocos que foram alocados mas ainda não escritos.
     * Compara-se com rem_bytes para não ler além do tamanho real do arquivo,
     * evitando leitura do padding do último bloco.
     */
    uint64_t extent_bytes =
        (extent.ee_len <= 32768 ? extent.ee_len : extent.ee_len - 32768) * block_size;
    uint64_t bytes = extent_bytes < rem_bytes ? extent_bytes : rem_bytes;
    std::vector<char> buf(bytes);

    if (!read_bytes(image, offset, buf.data(), bytes))
    {
      std::cerr << "error on read_bytes() in read_inode_content()\n";
      return {};
    }

    if (inode_is_dir(inode_in) && inode_num != 0)
    {
      for (uint64_t b_offset = 0; b_offset < bytes; b_offset += block_size)
      {
        uint64_t curr_block_size = std::min(block_size, bytes - b_offset);
        if (curr_block_size < block_size)
        {
          break; // directory blocks must be full blocks
        }
        uint32_t calculated = checksum_dir(reinterpret_cast<char *>(sb.s_uuid), inode_num, inode_in.i_generation, buf.data() + b_offset, block_size);
        uint32_t stored_checksum = bytearray_to_int32_le(reinterpret_cast<unsigned char *>(buf.data() + b_offset + block_size - 4));
        if (calculated != stored_checksum)
        {
          std::cerr << "\n[ERROR] Directory block checksum mismatch for Inode " << inode_num << "! The directory structure is corrupted.\n"
                    << "Calculated Checksum: " << calculated << "\n"
                    << "Stored Checksum:     " << stored_checksum << "\n"
                    << "Skipping reading to prevent using corrupted directory contents.\n\n";
          return {};
        }
      }
    }

    for (uint64_t i = 0; i < bytes; i++)
    {
      inode_content.push_back(buf[i]);
    }

    rem_bytes -= bytes;
  }

  return inode_content;
}

// read_leaf_extents: percorre recursivamente a extent tree coletando folhas
bool Ext4FS::read_leaf_extents(const ext4_extent_header *header,
                               std::vector<ext4_extent> &leaf_extents,
                               uint32_t inode_num,
                               uint32_t inode_gen)
{
  if (header->eh_depth == 0)
  {
    /**
     * Caso base: nó folha.
     * Logo após o header estão eh_entries structs ext4_extent contíguos.
     * Adicionamos cada extent ao vetor de folhas.
     */
    ext4_extent *extents = (ext4_extent *)(header + 1);

    for (uint16_t i = 0; i < header->eh_entries; i++)
    {
      leaf_extents.push_back(extents[i]);
    }
  }
  else
  {
    /**
     * Caso recursivo: nó interno (índice).
     * Logo após o header estão eh_entries structs ext4_extent_idx.
     * Cada índice aponta para um bloco físico que contém outro nó da árvore.
     * Lemos esse bloco e recursamos nele.
     */
    ext4_extent_idx *indices = (ext4_extent_idx *)(header + 1);
    std::vector<char> buf(block_size);

    for (uint16_t i = 0; i < header->eh_entries; i++)
    {
      uint64_t extent_phys_block = get_extent_idx_phys_block(indices[i]);
      uint64_t offset = get_block_offset(extent_phys_block);

      if (!read_bytes(image, offset, buf.data(), block_size))
      {
        std::cerr << "error on read_bytes() in read_leaf_extents()\n";
        return false;
      }

      // Validação de Checksum do Bloco Extent
      if (inode_num != 0)
      {
        uint32_t calculated = checksum_extent(reinterpret_cast<char *>(sb.s_uuid), inode_num, inode_gen, buf.data(), block_size);
        uint32_t stored_checksum = bytearray_to_int32_le(reinterpret_cast<unsigned char *>(&buf[block_size - 4]));
        if (calculated != stored_checksum)
        {
          std::cerr << "\n[ERROR] Extent block checksum mismatch for Inode " << inode_num << "! The extent tree metadata is corrupted.\n"
                    << "Calculated Checksum: " << calculated << "\n"
                    << "Stored Checksum:     " << stored_checksum << "\n"
                    << "Skipping reading to prevent using corrupted extent metadata.\n\n";
          return false;
        }
      }

      // O primeiro byte do bloco lido é o início do header do nó filho
      if (!read_leaf_extents((ext4_extent_header *)buf.data(), leaf_extents, inode_num, inode_gen))
      {
        std::cerr << "error on read_leaf_extents() in read_leaf_extents()\n";
        return false;
      };
    }
  }

  return true;
}

// find_inode_by_path: resolve um caminho para um número de inode
uint32_t Ext4FS::find_inode_by_path(const std::string &path,
                                    uint32_t inode_num)
{
  if (path.empty())
  {
    return inode_num;
  }

  // Caminho "/" resolve direto para o inode raiz (sempre inode 2 no ext4)
  if (path == "/")
  {
    return 2;
  }

  inode curr_inode;
  uint32_t curr_inode_num = inode_num;

  // Divide o caminho em componentes: "a/b/c" -> {"a", "b", "c"}
  std::vector<std::string> tokens = split_tokens(path);

  // Percorre cada componente do caminho, descendo um nível por iteração
  for (size_t i = 0; i < tokens.size(); i++)
  {
    if (!read_inode(curr_inode_num, curr_inode))
    {
      return 0;
    }

    // Cada componente intermediário deve ser um diretório
    if (!inode_is_dir(curr_inode))
    {
      return 0;
    }

    std::vector<char> dir_content = read_inode_content(curr_inode, curr_inode_num);
    curr_inode_num = find_inode_by_dir(dir_content, tokens[i]);

    if (curr_inode_num == 0)
    {
      return 0;
    }
  }

  return curr_inode_num;
}

// find_inode_by_dir: busca uma entrada de diretório pelo nome
uint32_t Ext4FS::find_inode_by_dir(const std::vector<char> &dir_content,
                                   const std::string &file_name)
{
  size_t offset = 0;

  /**
   * Varre as entradas de diretório sequencialmente.
   * Cada entrada tem tamanho variável; rec_len indica quantos bytes pular
   * para chegar na próxima entrada.
   */
  while (offset < dir_content.size())
  {
    ext4_dir_entry_2 *dir_entry = (ext4_dir_entry_2 *)(&dir_content[offset]);

    if (dir_entry->rec_len == 0)
    {
      break;
    }

    if (dir_entry->inode != 0)
    {
      // name NÃO é null-terminated: usar name_len para construir a string
      std::string dir_entry_name(dir_entry->name, dir_entry->name_len);

      if (dir_entry_name == file_name)
      {
        return dir_entry->inode;
      }
    }

    // rec_len == 0 indica corrupção ou fim antecipado do diretório
    if (dir_entry->rec_len == 0)
    {
      break;
    }

    offset += dir_entry->rec_len;
  }

  return 0;
}

bool Ext4FS::inode_is_used(uint32_t inode_num)
{
  if (inode_num == 0 || inode_num > sb.s_inodes_count)
  {
    std::cerr << "invalid inode given to inode_is_used()\n";
    return false;
  }

  uint32_t bg = get_inode_block_group(inode_num);
  uint64_t bitmap_block = get_inode_bitmap_block(bg);
  uint64_t offset = get_block_offset(bitmap_block);
  std::vector<char> bitmap(block_size);
  uint32_t inode_bit_offset = get_inode_bitmap_offset(inode_num);

  if (!read_bytes(image, offset, bitmap.data(), block_size))
  {
    return false;
  }

  // Validação de Checksum
  uint32_t calculated = checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid), bitmap.data(), block_size);
  const group_description &gd = gdt[bg];
  uint32_t stored = (static_cast<uint32_t>(gd.bg_inode_bitmap_csum_hi) << 16) | gd.bg_inode_bitmap_csum_lo;
  if (calculated != stored)
  {
    std::cerr << "\n[ERROR] Inode bitmap for block group " << bg << " checksum mismatch! The bitmap metadata is corrupted.\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << stored << "\n"
              << "Skipping operation to prevent using corrupted bitmap state.\n\n";
    return false;
  }

  return test_bit(bitmap, inode_bit_offset);
}

bool Ext4FS::block_is_used(uint64_t block_num)
{
  if (block_num < sb.s_first_data_block || block_num >= blocks_count)
  {
    std::cerr << "invalid block given to block_is_used()\n";
    return false;
  }

  uint32_t bg = get_block_block_group(block_num);
  uint64_t bitmap_block = get_block_bitmap_block(bg);
  uint64_t offset = get_block_offset(bitmap_block);
  std::vector<char> bitmap(block_size);
  uint32_t block_bit_offset = get_block_bitmap_offset(block_num);

  if (!read_bytes(image, offset, bitmap.data(), block_size))
  {
    return false;
  }

  // Validação de Checksum
  uint32_t calculated = checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid), bitmap.data(), block_size);
  const group_description &gd = gdt[bg];
  uint32_t stored = (static_cast<uint32_t>(gd.bg_block_bitmap_csum_hi) << 16) | gd.bg_block_bitmap_csum_lo;
  if (calculated != stored)
  {
    std::cerr << "\n[ERROR] Block bitmap for block group " << bg << " checksum mismatch! The bitmap metadata is corrupted.\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << stored << "\n"
              << "Skipping operation to prevent using corrupted bitmap state.\n\n";
    return false;
  }

  return test_bit(bitmap, block_bit_offset);
}

void Ext4FS::print_superblock() const
{
  const int w = 30;
  std::cout << std::left << std::setfill(' ');

  std::cout << std::setw(w) << "s_inodes_count:" << sb.s_inodes_count << "\n";
  std::cout << std::setw(w) << "s_blocks_count_lo:" << sb.s_blocks_count_lo
            << "\n";
  std::cout << std::setw(w) << "s_r_blocks_count_lo:" << sb.s_r_blocks_count_lo
            << "\n";
  std::cout << std::setw(w)
            << "s_free_blocks_count_lo:" << sb.s_free_blocks_count_lo << "\n";
  std::cout << std::setw(w) << "s_free_inodes_count:" << sb.s_free_inodes_count
            << "\n";
  std::cout << std::setw(w) << "s_first_data_block:" << sb.s_first_data_block
            << "\n";
  std::cout << std::setw(w) << "s_log_block_size:" << sb.s_log_block_size
            << "\n";
  std::cout << std::setw(w) << "s_log_cluster_size:" << sb.s_log_cluster_size
            << "\n";
  std::cout << std::setw(w) << "s_blocks_per_group:" << sb.s_blocks_per_group
            << "\n";
  std::cout << std::setw(w)
            << "s_clusters_per_group:" << sb.s_clusters_per_group << "\n";
  std::cout << std::setw(w) << "s_inodes_per_group:" << sb.s_inodes_per_group
            << "\n";
  std::cout << std::setw(w) << "s_mtime:" << sb.s_mtime << "\n";
  std::cout << std::setw(w) << "s_wtime:" << sb.s_wtime << "\n";
  std::cout << std::setw(w) << "s_mnt_count:" << sb.s_mnt_count << "\n";
  std::cout << std::setw(w) << "s_max_mnt_count:" << sb.s_max_mnt_count << "\n";
  std::cout << std::setw(w) << "s_magic:" << "0x" << std::hex << sb.s_magic
            << std::dec << "\n";
  std::cout << std::setw(w) << "s_state:" << "0x" << std::hex << sb.s_state
            << std::dec << "\n";
  std::cout << std::setw(w) << "s_errors:" << sb.s_errors << "\n";
  std::cout << std::setw(w) << "s_minor_rev_level:" << sb.s_minor_rev_level
            << "\n";
  std::cout << std::setw(w) << "s_lastcheck:" << sb.s_lastcheck << "\n";
  std::cout << std::setw(w) << "s_checkinterval:" << sb.s_checkinterval << "\n";
  std::cout << std::setw(w) << "s_creator_os:" << sb.s_creator_os << "\n";
  std::cout << std::setw(w) << "s_rev_level:" << sb.s_rev_level << "\n";
  std::cout << std::setw(w) << "s_def_resuid:" << sb.s_def_resuid << "\n";
  std::cout << std::setw(w) << "s_def_resgid:" << sb.s_def_resgid << "\n";

  std::cout << std::setw(w) << "s_first_ino:" << sb.s_first_ino << "\n";
  std::cout << std::setw(w) << "s_inode_size:" << sb.s_inode_size << "\n";
  std::cout << std::setw(w) << "s_block_group_nr:" << sb.s_block_group_nr
            << "\n";
  std::cout << std::setw(w) << "s_feature_compat:" << "0x" << std::hex
            << sb.s_feature_compat << std::dec << "\n";
  std::cout << std::setw(w) << "s_feature_incompat:" << "0x" << std::hex
            << sb.s_feature_incompat << std::dec << "\n";
  std::cout << std::setw(w) << "s_feature_ro_compat:" << "0x" << std::hex
            << sb.s_feature_ro_compat << std::dec << "\n";
  std::cout << std::setw(w) << "s_uuid:"
            << std::string_view(reinterpret_cast<const char *>(sb.s_uuid), 16)
            << "\n";
  std::cout << std::setw(w)
            << "s_volume_name:" << std::string_view(sb.s_volume_name, 16)
            << "\n";
  std::cout << std::setw(w)
            << "s_last_mounted:" << std::string_view(sb.s_last_mounted, 64)
            << "\n";
  std::cout << std::setw(w) << "s_algorithm_usage_bitmap:" << "0x" << std::hex
            << sb.s_algorithm_usage_bitmap << std::dec << "\n";

  std::cout << std::setw(w) << "s_prealloc_blocks:" << (int)sb.s_prealloc_blocks
            << "\n";
  std::cout << std::setw(w)
            << "s_prealloc_dir_blocks:" << (int)sb.s_prealloc_dir_blocks
            << "\n";
  std::cout << std::setw(w)
            << "s_reserved_gdt_blocks:" << sb.s_reserved_gdt_blocks << "\n";

  std::cout << std::setw(w) << "s_journal_uuid:"
            << std::string_view(
                   reinterpret_cast<const char *>(sb.s_journal_uuid), 16)
            << "\n";
  std::cout << std::setw(w) << "s_journal_inum:" << sb.s_journal_inum << "\n";
  std::cout << std::setw(w) << "s_journal_dev:" << sb.s_journal_dev << "\n";
  std::cout << std::setw(w) << "s_last_orphan:" << sb.s_last_orphan << "\n";
  std::cout << std::setw(w) << "s_hash_seed:" << "0x" << std::hex
            << sb.s_hash_seed[0] << " 0x" << sb.s_hash_seed[1] << " 0x"
            << sb.s_hash_seed[2] << " 0x" << sb.s_hash_seed[3] << std::dec
            << "\n";
  std::cout << std::setw(w)
            << "s_def_hash_version:" << (int)sb.s_def_hash_version << "\n";
  std::cout << std::setw(w) << "s_jnl_backup_type:" << (int)sb.s_jnl_backup_type
            << "\n";
  std::cout << std::setw(w) << "s_desc_size:" << sb.s_desc_size << "\n";
  std::cout << std::setw(w) << "s_default_mount_opts:" << "0x" << std::hex
            << sb.s_default_mount_opts << std::dec << "\n";
  std::cout << std::setw(w) << "s_first_meta_bg:" << sb.s_first_meta_bg << "\n";
  std::cout << std::setw(w) << "s_mkfs_time:" << sb.s_mkfs_time << "\n";
  std::cout << std::setw(w) << "s_jnl_blocks[0]:" << sb.s_jnl_blocks[0] << "\n";

  std::cout << std::setw(w) << "s_blocks_count_hi:" << sb.s_blocks_count_hi
            << "\n";
  std::cout << std::setw(w) << "s_r_blocks_count_hi:" << sb.s_r_blocks_count_hi
            << "\n";
  std::cout << std::setw(w)
            << "s_free_blocks_count_hi:" << sb.s_free_blocks_count_hi << "\n";
  std::cout << std::setw(w) << "s_min_extra_isize:" << sb.s_min_extra_isize
            << "\n";
  std::cout << std::setw(w) << "s_want_extra_isize:" << sb.s_want_extra_isize
            << "\n";
  std::cout << std::setw(w) << "s_flags:" << "0x" << std::hex << sb.s_flags
            << std::dec << "\n";
  std::cout << std::setw(w) << "s_raid_stride:" << sb.s_raid_stride << "\n";
  std::cout << std::setw(w)
            << "s_mmp_update_interval:" << sb.s_mmp_update_interval << "\n";
  std::cout << std::setw(w) << "s_mmp_block:" << sb.s_mmp_block << "\n";
  std::cout << std::setw(w) << "s_raid_stripe_width:" << sb.s_raid_stripe_width
            << "\n";
  std::cout << std::setw(w)
            << "s_log_groups_per_flex:" << (int)sb.s_log_groups_per_flex
            << "\n";
  std::cout << std::setw(w) << "s_checksum_type:" << (int)sb.s_checksum_type
            << "\n";
  std::cout << std::setw(w)
            << "s_encryption_level:" << (int)sb.s_encryption_level << "\n";
  std::cout << std::setw(w) << "s_reserved_pad:" << (int)sb.s_reserved_pad
            << "\n";
  std::cout << std::setw(w) << "s_kbytes_written:" << sb.s_kbytes_written
            << "\n";
  std::cout << std::setw(w) << "s_snapshot_inum:" << sb.s_snapshot_inum << "\n";
  std::cout << std::setw(w) << "s_snapshot_id:" << sb.s_snapshot_id << "\n";
  std::cout << std::setw(w)
            << "s_snapshot_r_blocks_count:" << sb.s_snapshot_r_blocks_count
            << "\n";
  std::cout << std::setw(w) << "s_snapshot_list:" << sb.s_snapshot_list << "\n";
  std::cout << std::setw(w) << "s_error_count:" << sb.s_error_count << "\n";
  std::cout << std::setw(w) << "s_first_error_time:" << sb.s_first_error_time
            << "\n";
  std::cout << std::setw(w) << "s_first_error_ino:" << sb.s_first_error_ino
            << "\n";
  std::cout << std::setw(w) << "s_first_error_block:" << sb.s_first_error_block
            << "\n";
  std::cout << std::setw(w) << "s_first_error_func:"
            << std::string_view(
                   reinterpret_cast<const char *>(sb.s_first_error_func), 32)
            << "\n";
  std::cout << std::setw(w) << "s_first_error_line:" << sb.s_first_error_line
            << "\n";
  std::cout << std::setw(w) << "s_last_error_time:" << sb.s_last_error_time
            << "\n";
  std::cout << std::setw(w) << "s_last_error_ino:" << sb.s_last_error_ino
            << "\n";
  std::cout << std::setw(w) << "s_last_error_line:" << sb.s_last_error_line
            << "\n";
  std::cout << std::setw(w) << "s_last_error_block:" << sb.s_last_error_block
            << "\n";
  std::cout << std::setw(w) << "s_last_error_func:"
            << std::string_view(
                   reinterpret_cast<const char *>(sb.s_last_error_func), 32)
            << "\n";
  std::cout << std::setw(w) << "s_mount_opts:"
            << std::string_view(reinterpret_cast<const char *>(sb.s_mount_opts),
                                64)
            << "\n";
  std::cout << std::setw(w) << "s_usr_quota_inum:" << sb.s_usr_quota_inum
            << "\n";
  std::cout << std::setw(w) << "s_grp_quota_inum:" << sb.s_grp_quota_inum
            << "\n";
  std::cout << std::setw(w) << "s_overhead_clusters:" << sb.s_overhead_clusters
            << "\n";
  std::cout << std::setw(w) << "s_backup_bgs:" << "0x" << std::hex
            << sb.s_backup_bgs[0] << " 0x" << sb.s_backup_bgs[1] << std::dec
            << "\n";
  std::cout << std::setw(w) << "s_encrypt_algos:" << (int)sb.s_encrypt_algos[0]
            << " " << (int)sb.s_encrypt_algos[1] << "\n";
  std::cout << std::setw(w) << "s_encrypt_pw_salt:"
            << std::string_view(
                   reinterpret_cast<const char *>(sb.s_encrypt_pw_salt), 16)
            << "\n";
  std::cout << std::setw(w) << "s_lpf_ino:" << sb.s_lpf_ino << "\n";
  std::cout << std::setw(w) << "s_prj_quota_inum:" << sb.s_prj_quota_inum
            << "\n";
  std::cout << std::setw(w) << "s_checksum_seed:" << "0x" << std::hex
            << sb.s_checksum_seed << std::dec << "\n";
  std::cout << std::setw(w) << "s_wtime_hi:" << (int)sb.s_wtime_hi << "\n";
  std::cout << std::setw(w) << "s_mtime_hi:" << (int)sb.s_mtime_hi << "\n";
  std::cout << std::setw(w) << "s_mkfs_time_hi:" << (int)sb.s_mkfs_time_hi
            << "\n";
  std::cout << std::setw(w) << "s_lastcheck_hi:" << (int)sb.s_lastcheck_hi
            << "\n";
  std::cout << std::setw(w)
            << "s_first_error_time_hi:" << (int)sb.s_first_error_time_hi
            << "\n";
  std::cout << std::setw(w)
            << "s_last_error_time_hi:" << (int)sb.s_last_error_time_hi << "\n";
  std::cout << std::setw(w)
            << "s_first_error_errcode:" << (int)sb.s_first_error_errcode
            << "\n";
  std::cout << std::setw(w)
            << "s_last_error_errcode:" << (int)sb.s_last_error_errcode << "\n";
  std::cout << std::setw(w) << "s_encoding:" << sb.s_encoding << "\n";
  std::cout << std::setw(w) << "s_encoding_flags:" << sb.s_encoding_flags
            << "\n";
  std::cout << std::setw(w) << "s_orphan_file_inum:" << sb.s_orphan_file_inum
            << "\n";
  std::cout << std::setw(w) << "s_def_resuid_hi:" << sb.s_def_resuid_hi << "\n";
  std::cout << std::setw(w) << "s_def_resgid_hi:" << sb.s_def_resgid_hi << "\n";
  std::cout << std::setw(w) << "s_reserved[0]:" << sb.s_reserved[0] << "\n";
  std::cout << std::setw(w) << "s_checksum:" << "0x" << std::hex
            << sb.s_checksum << std::dec << "\n";
}

// print_gdt: imprime todos os group descriptors da GDT na saída padrão
void Ext4FS::print_gdt() const
{
  const int w = 30;
  std::cout << std::left << std::setfill(' ');

  for (size_t i = 0; i < gdt.size(); i++)
  {
    const auto &gd = gdt[i];

    std::cout << std::setw(w) << "bg_block_bitmap_lo:" << gd.bg_block_bitmap_lo
              << "\n";
    std::cout << std::setw(w) << "bg_inode_bitmap_lo:" << gd.bg_inode_bitmap_lo
              << "\n";
    std::cout << std::setw(w) << "bg_inode_table_lo:" << gd.bg_inode_table_lo
              << "\n";
    std::cout << std::setw(w)
              << "bg_free_blocks_count_lo:" << gd.bg_free_blocks_count_lo
              << "\n";
    std::cout << std::setw(w)
              << "bg_free_inodes_count_lo:" << gd.bg_free_inodes_count_lo
              << "\n";
    std::cout << std::setw(w)
              << "bg_used_dirs_count_lo:" << gd.bg_used_dirs_count_lo << "\n";
    std::cout << std::setw(w) << "bg_flags:" << gd.bg_flags << "\n";
    std::cout << std::setw(w)
              << "bg_exclude_bitmap_lo:" << gd.bg_exclude_bitmap_lo << "\n";
    std::cout << std::setw(w)
              << "bg_block_bitmap_csum_lo:" << gd.bg_block_bitmap_csum_lo
              << "\n";
    std::cout << std::setw(w)
              << "bg_inode_bitmap_csum_lo:" << gd.bg_inode_bitmap_csum_lo
              << "\n";
    std::cout << std::setw(w)
              << "bg_itable_unused_lo:" << gd.bg_itable_unused_lo << "\n";
    std::cout << std::setw(w) << "bg_checksum:" << gd.bg_checksum << "\n";

    std::cout << std::setw(w) << "bg_block_bitmap_hi:" << gd.bg_block_bitmap_hi
              << "\n";
    std::cout << std::setw(w) << "bg_inode_bitmap_hi:" << gd.bg_inode_bitmap_hi
              << "\n";
    std::cout << std::setw(w) << "bg_inode_table_hi:" << gd.bg_inode_table_hi
              << "\n";
    std::cout << std::setw(w)
              << "bg_free_blocks_count_hi:" << gd.bg_free_blocks_count_hi
              << "\n";
    std::cout << std::setw(w)
              << "bg_free_inodes_count_hi:" << gd.bg_free_inodes_count_hi
              << "\n";
    std::cout << std::setw(w)
              << "bg_used_dirs_count_hi:" << gd.bg_used_dirs_count_hi << "\n";
    std::cout << std::setw(w)
              << "bg_itable_unused_hi:" << gd.bg_itable_unused_hi << "\n";
    std::cout << std::setw(w)
              << "bg_exclude_bitmap_hi:" << gd.bg_exclude_bitmap_hi << "\n";
    std::cout << std::setw(w)
              << "bg_block_bitmap_csum_hi:" << gd.bg_block_bitmap_csum_hi
              << "\n";
    std::cout << std::setw(w)
              << "bg_inode_bitmap_csum_hi:" << gd.bg_inode_bitmap_csum_hi
              << "\n";
    std::cout << std::setw(w) << "bg_reserved:" << gd.bg_reserved << "\n\n";
  }
}

// print_inode: imprime todos os campos de um inode na saída padrão
void Ext4FS::print_inode(const inode &inode_in, uint32_t /*inode_num*/) const
{
  const int w = 30;
  std::cout << std::left << std::setfill(' ');

  std::cout << std::setw(w) << "i_mode:" << "0x" << std::hex << inode_in.i_mode
            << std::dec << "\n";
  std::cout << std::setw(w) << "i_uid:" << inode_in.i_uid << "\n";
  std::cout << std::setw(w) << "i_size_lo:" << inode_in.i_size_lo << "\n";
  std::cout << std::setw(w) << "i_atime:" << inode_in.i_atime << "\n";
  std::cout << std::setw(w) << "i_ctime:" << inode_in.i_ctime << "\n";
  std::cout << std::setw(w) << "i_mtime:" << inode_in.i_mtime << "\n";
  std::cout << std::setw(w) << "i_dtime:" << inode_in.i_dtime << "\n";
  std::cout << std::setw(w) << "i_gid:" << inode_in.i_gid << "\n";
  std::cout << std::setw(w) << "i_links_count:" << inode_in.i_links_count
            << "\n";
  std::cout << std::setw(w) << "i_blocks_lo:" << inode_in.i_blocks_lo << "\n";
  std::cout << std::setw(w) << "i_flags:" << "0x" << std::hex
            << inode_in.i_flags << std::dec << "\n";

  std::cout << std::setw(w)
            << "l_i_version:" << inode_in.osd1.linux1.l_i_version << "\n";

  std::cout << std::setw(w)
            << "h_i_translator:" << inode_in.osd1.hurd1.h_i_translator << "\n";

  std::cout << std::setw(w)
            << "m_i_reserved1:" << inode_in.osd1.masix1.m_i_reserved1 << "\n";

  // i_block contém a extent tree inline; imprime os valores brutos
  std::cout << std::setw(w) << "i_block:" << "\n";
  for (int i = 0; i < 15; i++)
  {
    std::cout << inode_in.i_block[i];
  }
  std::cout << "\n";

  std::cout << std::setw(w) << "i_generation:" << inode_in.i_generation << "\n";
  std::cout << std::setw(w) << "i_file_acl_lo:" << inode_in.i_file_acl_lo
            << "\n";
  std::cout << std::setw(w) << "i_size_high:" << inode_in.i_size_high << "\n";
  std::cout << std::setw(w) << "i_obso_faddr:" << inode_in.i_obso_faddr << "\n";

  std::cout << std::setw(w)
            << "l_i_blocks_high:" << inode_in.osd2.linux2.l_i_blocks_high
            << "\n";
  std::cout << std::setw(w)
            << "l_i_file_acl_high:" << inode_in.osd2.linux2.l_i_file_acl_high
            << "\n";
  std::cout << std::setw(w)
            << "l_i_uid_high:" << inode_in.osd2.linux2.l_i_uid_high << "\n";
  std::cout << std::setw(w)
            << "l_i_gid_high:" << inode_in.osd2.linux2.l_i_gid_high << "\n";
  std::cout << std::setw(w) << "l_i_checksum_lo:" << "0x" << std::hex
            << inode_in.osd2.linux2.l_i_checksum_lo << std::dec << "\n";
  std::cout << std::setw(w)
            << "l_i_reserved:" << inode_in.osd2.linux2.l_i_reserved << "\n";

  std::cout << std::setw(w)
            << "h_i_reserved1:" << inode_in.osd2.hurd2.h_i_reserved1 << "\n";
  std::cout << std::setw(w)
            << "h_i_mode_high:" << inode_in.osd2.hurd2.h_i_mode_high << "\n";
  std::cout << std::setw(w)
            << "h_i_uid_high:" << inode_in.osd2.hurd2.h_i_uid_high << "\n";
  std::cout << std::setw(w)
            << "h_i_gid_high:" << inode_in.osd2.hurd2.h_i_gid_high << "\n";
  std::cout << std::setw(w) << "h_i_author:" << inode_in.osd2.hurd2.h_i_author
            << "\n";

  std::cout << std::setw(w)
            << "m_i_reserved1:" << inode_in.osd2.masix2.m_i_reserved1 << "\n";
  std::cout << std::setw(w)
            << "m_i_file_acl_high:" << inode_in.osd2.masix2.m_i_file_acl_high
            << "\n";
  std::cout << std::setw(w)
            << "m_i_reserved2:" << inode_in.osd2.masix2.m_i_reserved2[0]
            << inode_in.osd2.masix2.m_i_reserved2[1] << "\n";

  std::cout << std::setw(w) << "i_extra_isize:" << inode_in.i_extra_isize
            << "\n";
  std::cout << std::setw(w) << "i_checksum_hi:" << "0x" << std::hex
            << inode_in.i_checksum_hi << std::dec << "\n";
  std::cout << std::setw(w) << "i_ctime_extra:" << inode_in.i_ctime_extra
            << "\n";
  std::cout << std::setw(w) << "i_mtime_extra:" << inode_in.i_mtime_extra
            << "\n";
  std::cout << std::setw(w) << "i_atime_extra:" << inode_in.i_atime_extra
            << "\n";
  std::cout << std::setw(w) << "i_crtime:" << inode_in.i_crtime << "\n";
  std::cout << std::setw(w) << "i_crtime_extra:" << inode_in.i_crtime_extra
            << "\n";
  std::cout << std::setw(w) << "i_version_hi:" << inode_in.i_version_hi << "\n";
  std::cout << std::setw(w) << "i_projid:" << inode_in.i_projid << "\n";
}
