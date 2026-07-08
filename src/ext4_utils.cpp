/**
 * Implementação da classe Ext4FS — parsing e navegação em imagens ext4.
 */

#include "ext4_utils.h"
#include "ext4checksum.h"
#include "io_utils.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>

// init: abre a imagem e inicializa todos os metadados do SA
bool Ext4FS::init(const std::string &img_path) {
  if (!open_image(img_path, image)) {
    std::cerr << "error on open_image() in init()\n";
    return false;
  }

  if (!read_superblock()) {
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

  if (!read_gdt()) {
    std::cerr << "error on read_gdt() in init() \n";
    return false;
  }

  return true;
}

// read_superblock: lê os 1024 bytes do superbloco no offset fixo 1024
bool Ext4FS::read_superblock() {
  // O superbloco do ext4 sempre reside no offset 1024 bytes da partição */
  if (!read_bytes(image, 1024, &sb, sizeof(super_block))) {
    return false;
  }

  // Validação de Checksum
  uint32_t calculated = checksum_superblock(reinterpret_cast<char *>(&sb));
  if (calculated != sb.s_checksum) {
    std::cerr << "\n[CRITICAL ERROR] Superblock checksum mismatch! The "
                 "filesystem structure is corrupted.\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << sb.s_checksum << "\n"
              << "Terminating terminal execution immediately to prevent data "
                 "corruption.\n\n";
    std::exit(1);
  }

  return true;
}

// read_gdt: lê todos os group descriptors da Group Descriptor Table
bool Ext4FS::read_gdt() {
  /**
   * Se s_desc_size for 0 (SA sem a feature 64bit), cada descriptor tem 32
   * bytes. Com a feature 64bit ativa, s_desc_size geralmente é 64.
   */
  uint16_t current_desc_size = sb.s_desc_size == 0 ? 32 : sb.s_desc_size;
  uint64_t offset = gdt_offset;

  // Lê cada group descriptor e armazena no vetor gdt
  for (uint64_t i = 0; i < num_groups; i++) {
    group_description gd{};

    if (!read_bytes(image, offset, &gd, current_desc_size)) {
      return false;
    }

    // Validação de Checksum do Group Descriptor
    uint16_t calculated = checksum_group(reinterpret_cast<char *>(sb.s_uuid), i,
                                         reinterpret_cast<char *>(&gd));
    if (calculated != gd.bg_checksum) {
      std::cerr << "\n[CRITICAL ERROR] Group Descriptor " << i
                << " checksum mismatch! The GDT structure is corrupted.\n"
                << "Calculated Checksum: " << calculated << "\n"
                << "Stored Checksum:     " << gd.bg_checksum << "\n"
                << "Terminating terminal execution immediately to prevent data "
                   "corruption.\n\n";
      std::exit(1);
    }

    gdt.push_back(gd);

    // Avança o offset para o próximo descriptor
    offset += current_desc_size;
  }

  return true;
}

// read_inode: localiza e lê um inode específico pelo seu número
bool Ext4FS::read_inode(uint32_t inode_num, inode &inode_out) {
  // Inode 0 não existe; inodes válidos começam em 1
  if (inode_num == 0 || inode_num > sb.s_inodes_count) {
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

  // Criamos um buffer de 256 bytes preenchido com zero para rodar o checksum
  // com segurança
  std::vector<char> inode_buf(256, 0);
  size_t bytes_to_read =
      std::min(static_cast<size_t>(sb.s_inode_size), inode_buf.size());
  if (!read_bytes(image, block_offset, inode_buf.data(), bytes_to_read)) {
    return false;
  }

  // Copia os dados lidos para a estrutura do inode
  size_t copy_size =
      std::min(sizeof(inode), static_cast<size_t>(sb.s_inode_size));
  std::memcpy(&inode_out, inode_buf.data(), copy_size);

  // Validação de Checksum
  uint32_t calculated =
      checksum_inode(reinterpret_cast<char *>(sb.s_uuid), inode_num,
                     inode_out.i_generation, inode_buf.data());
  uint32_t stored_checksum =
      (static_cast<uint32_t>(inode_out.i_checksum_hi) << 16) |
      inode_out.osd2.linux2.l_i_checksum_lo;
  if (calculated != stored_checksum) {
    std::cerr
        << "\n[ERROR] Inode " << inode_num
        << " checksum mismatch! The file or directory entry is corrupted.\n"
        << "Calculated Checksum: " << calculated << "\n"
        << "Stored Checksum:     " << stored_checksum << "\n"
        << "Skipping reading to prevent using corrupted inode metadata.\n\n";
    return false;
  }

  return true;
}

// read_inode_content: lê o conteúdo completo de um arquivo via extent tree
std::vector<char> Ext4FS::read_inode_content(const inode &inode_in,
                                             uint32_t inode_num) {
  /**
   * O campo i_block[] do inode contém a raiz da extent tree.
   * Fazemos um cast direto para ext4_extent_header*, pois usamos #pragma
   * pack(1) e os bytes estão contíguos.
   */
  ext4_extent_header *header = (ext4_extent_header *)inode_in.i_block;
  std::vector<ext4_extent> leaf_extents;

  if (!read_leaf_extents(header, leaf_extents, inode_num,
                         inode_in.i_generation)) {
    std::cerr << "error on read_leaf_extents() in read_inode_content()\n";
    return {};
  }

  uint64_t file_size = get_file_size(inode_in);
  uint64_t rem_bytes = file_size;
  std::vector<char>
      inode_content; // i_block pode ser maior que file_size em algum momento?
  inode_content.reserve(file_size);

  uint64_t next_logical_block = 0;

  for (const auto &extent : leaf_extents) {
    if (rem_bytes == 0)
      break;

    // se o próximo extent começa depois de onde parou, há um buraco e o
    // preenche com zeros antes de ler o extent
    if (extent.ee_block > next_logical_block) {
      uint64_t hole_blocks = extent.ee_block - next_logical_block;
      uint64_t hole_bytes = hole_blocks * block_size;
      uint64_t bytes = hole_bytes < rem_bytes ? hole_bytes : rem_bytes;

      inode_content.insert(inode_content.end(), bytes, 0);
      rem_bytes -= bytes;
      next_logical_block = extent.ee_block;

      if (rem_bytes == 0)
        break;
    }

    uint64_t extent_phys_block = get_extent_phys_block(extent);
    uint64_t offset = get_block_offset(extent_phys_block);

    uint64_t extent_len_blocks =
        (extent.ee_len <= 32768 ? extent.ee_len : extent.ee_len - 32768);
    uint64_t extent_bytes = extent_len_blocks * block_size;
    uint64_t bytes = extent_bytes < rem_bytes ? extent_bytes : rem_bytes;
    std::vector<char> buf(bytes);

    if (!read_bytes(image, offset, buf.data(), bytes)) {
      std::cerr << "error on read_bytes() in read_inode_content()\n";
      return {};
    }

    if (inode_is_dir(inode_in)) {
      for (uint64_t b_offset = 0; b_offset < bytes; b_offset += block_size) {
        uint64_t curr_block_size = std::min(block_size, bytes - b_offset);
        if (curr_block_size < block_size) {
          break; // directory blocks must be full blocks
        }
        uint32_t calculated = checksum_dir(reinterpret_cast<char *>(sb.s_uuid),
                                           inode_num, inode_in.i_generation,
                                           buf.data() + b_offset, block_size);
        uint32_t stored_checksum =
            bytearray_to_int32_le(reinterpret_cast<unsigned char *>(
                buf.data() + b_offset + block_size - 4));
        if (calculated != stored_checksum) {
          std::cerr << "\n[ERROR] Directory block checksum mismatch for Inode "
                    << inode_num << "! The directory structure is corrupted.\n"
                    << "Calculated Checksum: " << calculated << "\n"
                    << "Stored Checksum:     " << stored_checksum << "\n"
                    << "Skipping reading to prevent using corrupted directory "
                       "contents.\n\n";
          return {};
        }
      }
    }

    for (uint64_t i = 0; i < bytes; i++) {
      inode_content.push_back(buf[i]);
    }

    rem_bytes -= bytes;
    next_logical_block = extent.ee_block + extent_len_blocks;
  }

  return inode_content;
}

// read_leaf_extents: percorre recursivamente a extent tree coletando folhas
bool Ext4FS::read_leaf_extents(const ext4_extent_header *header,
                               std::vector<ext4_extent> &leaf_extents,
                               uint32_t inode_num, uint32_t inode_gen) {
  if (header->eh_depth == 0) {
    /**
     * Caso base: nó folha.
     * Logo após o header estão eh_entries structs ext4_extent contíguos.
     * Adicionamos cada extent ao vetor de folhas.
     */
    ext4_extent *extents = (ext4_extent *)(header + 1);

    for (uint16_t i = 0; i < header->eh_entries; i++) {
      leaf_extents.push_back(extents[i]);
    }
  } else {
    /**
     * Caso recursivo: nó interno (índice).
     * Logo após o header estão eh_entries structs ext4_extent_idx.
     * Cada índice aponta para um bloco físico que contém outro nó da árvore.
     * Lemos esse bloco e recursamos nele.
     */
    ext4_extent_idx *indices = (ext4_extent_idx *)(header + 1);
    std::vector<char> buf(block_size);

    for (uint16_t i = 0; i < header->eh_entries; i++) {
      uint64_t extent_phys_block = get_extent_idx_phys_block(indices[i]);
      uint64_t offset = get_block_offset(extent_phys_block);

      if (!read_bytes(image, offset, buf.data(), block_size)) {
        std::cerr << "error on read_bytes() in read_leaf_extents()\n";
        return false;
      }

      // Validação de Checksum do Bloco Extent
      if (inode_num != 0) {
        uint32_t calculated =
            checksum_extent(reinterpret_cast<char *>(sb.s_uuid), inode_num,
                            inode_gen, buf.data(), block_size);
        uint32_t stored_checksum = bytearray_to_int32_le(
            reinterpret_cast<unsigned char *>(&buf[block_size - 4]));
        if (calculated != stored_checksum) {
          std::cerr << "\n[ERROR] Extent block checksum mismatch for Inode "
                    << inode_num << "! The extent tree metadata is corrupted.\n"
                    << "Calculated Checksum: " << calculated << "\n"
                    << "Stored Checksum:     " << stored_checksum << "\n"
                    << "Skipping reading to prevent using corrupted extent "
                       "metadata.\n\n";
          return false;
        }
      }

      // O primeiro byte do bloco lido é o início do header do nó filho
      if (!read_leaf_extents((ext4_extent_header *)buf.data(), leaf_extents,
                             inode_num, inode_gen)) {
        std::cerr << "error on read_leaf_extents() in read_leaf_extents()\n";
        return false;
      };
    }
  }

  return true;
}

// find_inode_by_path: resolve um caminho para um número de inode
uint32_t Ext4FS::find_inode_by_path(const std::string &path,
                                    uint32_t inode_num) {
  if (path.empty()) {
    return inode_num;
  }

  // Caminho "/" resolve direto para o inode raiz (sempre inode 2 no ext4)
  if (path == "/") {
    return 2;
  }

  inode curr_inode;
  uint32_t curr_inode_num = inode_num;

  // Divide o caminho em componentes: "a/b/c" -> {"a", "b", "c"}
  std::vector<std::string> tokens = split_tokens(path);

  // Percorre cada componente do caminho, descendo um nível por iteração
  for (size_t i = 0; i < tokens.size(); i++) {
    if (!read_inode(curr_inode_num, curr_inode)) {
      return 0;
    }

    // Cada componente intermediário deve ser um diretório
    if (!inode_is_dir(curr_inode)) {
      return 0;
    }

    std::vector<char> dir_content =
        read_inode_content(curr_inode, curr_inode_num);
    curr_inode_num = find_inode_by_dir(dir_content, tokens[i]);

    if (curr_inode_num == 0) {
      return 0;
    }
  }

  return curr_inode_num;
}

// find_inode_by_dir: busca uma entrada de diretório pelo nome
uint32_t Ext4FS::find_inode_by_dir(const std::vector<char> &dir_content,
                                   const std::string &file_name) {
  size_t offset = 0;

  /**
   * Varre as entradas de diretório sequencialmente.
   * Cada entrada tem tamanho variável; rec_len indica quantos bytes pular
   * para chegar na próxima entrada.
   */
  while (offset < dir_content.size()) {
    ext4_dir_entry_2 *dir_entry = (ext4_dir_entry_2 *)(&dir_content[offset]);

    if (dir_entry->rec_len == 0) {
      break;
    }

    if (dir_entry->inode != 0) {
      // name NÃO é null-terminated: usar name_len para construir a string
      std::string dir_entry_name(dir_entry->name, dir_entry->name_len);

      if (dir_entry_name == file_name) {
        return dir_entry->inode;
      }
    }

    // rec_len == 0 indica corrupção ou fim antecipado do diretório
    if (dir_entry->rec_len == 0) {
      break;
    }

    offset += dir_entry->rec_len;
  }

  return 0;
}

bool Ext4FS::inode_is_used(uint32_t inode_num) {
  if (inode_num == 0 || inode_num > sb.s_inodes_count) {
    std::cerr << "invalid inode given to inode_is_used()\n";
    return false;
  }

  uint32_t bg = get_inode_block_group(inode_num);
  uint64_t bitmap_block = get_inode_bitmap_block(bg);
  uint64_t offset = get_block_offset(bitmap_block);
  std::vector<char> bitmap(block_size);
  uint32_t inode_bit_offset = get_inode_bitmap_offset(inode_num);

  if (!read_bytes(image, offset, bitmap.data(), block_size)) {
    return false;
  }

  // Validação de Checksum
  uint32_t calculated =
      checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid), bitmap.data(),
                      sb.s_inodes_per_group / 8);
  const group_description &gd = gdt[bg];
  uint32_t stored = (static_cast<uint32_t>(gd.bg_inode_bitmap_csum_hi) << 16) |
                    gd.bg_inode_bitmap_csum_lo;
  if (calculated != stored) {
    std::cerr
        << "\n[ERROR] Inode bitmap for block group " << bg
        << " checksum mismatch! The bitmap metadata is corrupted.\n"
        << "Calculated Checksum: " << calculated << "\n"
        << "Stored Checksum:     " << stored << "\n"
        << "Skipping operation to prevent using corrupted bitmap state.\n\n";
    return false;
  }

  return test_bit(bitmap, inode_bit_offset);
}

bool Ext4FS::block_is_used(uint64_t block_num) {
  if (block_num < sb.s_first_data_block || block_num >= blocks_count) {
    std::cerr << "invalid block given to block_is_used()\n";
    return false;
  }

  uint32_t bg = get_block_block_group(block_num);
  uint64_t bitmap_block = get_block_bitmap_block(bg);
  uint64_t offset = get_block_offset(bitmap_block);
  std::vector<char> bitmap(block_size);
  uint32_t block_bit_offset = get_block_bitmap_offset(block_num);

  if (!read_bytes(image, offset, bitmap.data(), block_size)) {
    return false;
  }

  // Validação de Checksum
  uint32_t calculated =
      checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid), bitmap.data(),
                      sb.s_blocks_per_group / 8);
  const group_description &gd = gdt[bg];
  uint32_t stored = (static_cast<uint32_t>(gd.bg_block_bitmap_csum_hi) << 16) |
                    gd.bg_block_bitmap_csum_lo;
  if (calculated != stored) {
    std::cerr
        << "\n[ERROR] Block bitmap for block group " << bg
        << " checksum mismatch! The bitmap metadata is corrupted.\n"
        << "Calculated Checksum: " << calculated << "\n"
        << "Stored Checksum:     " << stored << "\n"
        << "Skipping operation to prevent using corrupted bitmap state.\n\n";
    return false;
  }

  return test_bit(bitmap, block_bit_offset);
}

// update_sb: persiste o superbloco em memória na imagem
bool Ext4FS::update_sb() {
  // 1. Read current superblock from disk for validation
  super_block on_disk_sb;
  if (!read_bytes(image, 1024, &on_disk_sb, sizeof(super_block))) {
    std::cerr << "update_sb: erro ao ler superbloco para validação\n";
    return false;
  }

  // 2. Validate current checksum
  uint32_t calculated =
      checksum_superblock(reinterpret_cast<char *>(&on_disk_sb));
  if (calculated != on_disk_sb.s_checksum) {
    std::cerr
        << "\n[ERROR] Superblock checksum mismatch on disk before write!\n"
        << "Calculated Checksum: " << calculated << "\n"
        << "Stored Checksum:     " << on_disk_sb.s_checksum << "\n"
        << "Skipping write to prevent corruption.\n\n";
    return false;
  }

  // 3. Compute new checksum and update sb
  sb.s_checksum = checksum_superblock(reinterpret_cast<char *>(&sb));

  // 4. Do the write
  if (!write_bytes(image, 1024, &sb, sizeof(super_block))) {
    std::cerr << "update_sb: erro ao escrever superbloco\n";
    return false;
  }
  return true;
}

// update_gdt_entry: persiste um group descriptor em memória na imagem
bool Ext4FS::update_gdt_entry(uint64_t bg) {
  uint16_t current_desc_size =
      (sb.s_desc_size == 0)
          ? 32
          : sb.s_desc_size; // 32 bytes se a feature 64bit não estiver ativa, 64
                            // bytes caso contrário
  uint64_t offset = get_gdt_entry_offset(bg);

  // 1. Read current group descriptor from disk for validation
  group_description on_disk_gd{};
  std::memset(&on_disk_gd, 0, sizeof(on_disk_gd));
  if (!read_bytes(image, offset, &on_disk_gd, current_desc_size)) {
    std::cerr << "update_gdt_entry: erro ao ler GDT do grupo " << bg
              << " para validação\n";
    return false;
  }

  // 2. Validate current checksum
  uint16_t calculated = checksum_group(reinterpret_cast<char *>(sb.s_uuid), bg,
                                       reinterpret_cast<char *>(&on_disk_gd));
  if (calculated != on_disk_gd.bg_checksum) {
    std::cerr << "\n[ERROR] Group Descriptor " << bg
              << " checksum mismatch on disk before write!\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << on_disk_gd.bg_checksum << "\n"
              << "Skipping write to prevent corruption.\n\n";
    return false;
  }

  // 3. Compute new checksum and update GDT entry in memory
  gdt[bg].bg_checksum = checksum_group(reinterpret_cast<char *>(sb.s_uuid), bg,
                                       reinterpret_cast<char *>(&gdt[bg]));

  // 4. Do the write
  if (!write_bytes(image, offset, &gdt[bg], current_desc_size)) {
    std::cerr << "update_gdt_entry: erro ao escrever GDT entry do grupo " << bg
              << "\n";
    return false;
  }
  return true;
}

// update_inode_bitmap: persiste o bitmap de inodes de um grupo na imagem
bool Ext4FS::update_inode_bitmap(uint64_t bg, const std::vector<char> &bitmap) {
  uint64_t bitmap_block = get_inode_bitmap_block(static_cast<uint32_t>(bg));
  uint64_t offset = get_block_offset(bitmap_block);

  // 1. Read current bitmap from disk for validation
  std::vector<char> on_disk_bitmap(block_size);
  if (!read_bytes(image, offset, on_disk_bitmap.data(), block_size)) {
    std::cerr << "update_inode_bitmap: erro ao ler bitmap do grupo " << bg
              << " para validação\n";
    return false;
  }

  // 2. Validate current checksum
  uint32_t calculated =
      checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid),
                      on_disk_bitmap.data(), sb.s_inodes_per_group / 8);
  const group_description &gd = gdt[bg];
  uint32_t stored = (static_cast<uint32_t>(gd.bg_inode_bitmap_csum_hi) << 16) |
                    gd.bg_inode_bitmap_csum_lo;
  if (calculated != stored) {
    std::cerr << "\n[ERROR] Inode bitmap for block group " << bg
              << " checksum mismatch on disk before write!\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << stored << "\n"
              << "Skipping write to prevent corruption.\n\n";
    return false;
  }

  // 3. Compute new checksum and update memory state in GDT entry
  uint32_t new_calculated = checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid),
                                            const_cast<char *>(bitmap.data()),
                                            sb.s_inodes_per_group / 8);
  gdt[bg].bg_inode_bitmap_csum_lo =
      static_cast<uint16_t>(new_calculated & 0xFFFF);
  gdt[bg].bg_inode_bitmap_csum_hi =
      static_cast<uint16_t>((new_calculated >> 16) & 0xFFFF);

  // 4. Do the write
  if (!write_bytes(image, offset, const_cast<char *>(bitmap.data()),
                   block_size)) {
    std::cerr << "update_inode_bitmap: erro ao escrever bitmap do grupo " << bg
              << "\n";
    return false;
  }
  return true;
}

// update_block_bitmap: persiste o bitmap de blocos de um grupo na imagem
bool Ext4FS::update_block_bitmap(uint64_t bg, const std::vector<char> &bitmap) {
  uint64_t bitmap_block = get_block_bitmap_block(static_cast<uint32_t>(bg));
  uint64_t offset = get_block_offset(bitmap_block);

  // 1. Read current bitmap from disk for validation
  std::vector<char> on_disk_bitmap(block_size);
  if (!read_bytes(image, offset, on_disk_bitmap.data(), block_size)) {
    std::cerr << "update_block_bitmap: erro ao ler bitmap do grupo " << bg
              << " para validação\n";
    return false;
  }

  // 2. Validate current checksum
  uint32_t calculated =
      checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid),
                      on_disk_bitmap.data(), sb.s_blocks_per_group / 8);
  const group_description &gd = gdt[bg];
  uint32_t stored = (static_cast<uint32_t>(gd.bg_block_bitmap_csum_hi) << 16) |
                    gd.bg_block_bitmap_csum_lo;
  if (calculated != stored) {
    std::cerr << "\n[ERROR] Block bitmap for block group " << bg
              << " checksum mismatch on disk before write!\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << stored << "\n"
              << "Skipping write to prevent corruption.\n\n";
    return false;
  }

  // 3. Compute new checksum and update memory state in GDT entry
  uint32_t new_calculated = checksum_bitmap(reinterpret_cast<char *>(sb.s_uuid),
                                            const_cast<char *>(bitmap.data()),
                                            sb.s_blocks_per_group / 8);
  gdt[bg].bg_block_bitmap_csum_lo =
      static_cast<uint16_t>(new_calculated & 0xFFFF);
  gdt[bg].bg_block_bitmap_csum_hi =
      static_cast<uint16_t>((new_calculated >> 16) & 0xFFFF);

  // 4. Do the write
  if (!write_bytes(image, offset, const_cast<char *>(bitmap.data()),
                   block_size)) {
    std::cerr << "update_block_bitmap: erro ao escrever bitmap do grupo " << bg
              << "\n";
    return false;
  }
  return true;
}

// update_inode: persiste um inode na tabela de inodes do seu grupo na imagem
bool Ext4FS::update_inode(uint32_t inode_num, const inode &inode_in) {
  if (inode_num == 0 || inode_num > sb.s_inodes_count) {
    std::cerr << "update_inode: número de inode inválido: " << inode_num
              << "\n";
    return false;
  }

  uint32_t bg = get_inode_block_group(inode_num);
  uint32_t index = get_inode_index(inode_num);
  uint64_t inode_table_block = get_inode_table_block(bg);
  uint64_t offset =
      get_block_offset(inode_table_block) + index * sb.s_inode_size;

  // 1. Read current inode from disk for validation
  std::vector<char> on_disk_inode_buf(256, 0);
  size_t bytes_to_read =
      std::min(static_cast<size_t>(sb.s_inode_size), on_disk_inode_buf.size());
  if (!read_bytes(image, offset, on_disk_inode_buf.data(), bytes_to_read)) {
    std::cerr << "update_inode: erro ao ler inode " << inode_num
              << " para validação\n";
    return false;
  }

  // 2. Validate current checksum
  inode on_disk_inode{};
  size_t copy_size =
      std::min(sizeof(inode), static_cast<size_t>(sb.s_inode_size));
  std::memcpy(&on_disk_inode, on_disk_inode_buf.data(), copy_size);

  uint32_t calculated =
      checksum_inode(reinterpret_cast<char *>(sb.s_uuid), inode_num,
                     on_disk_inode.i_generation, on_disk_inode_buf.data());
  uint32_t stored_checksum =
      (static_cast<uint32_t>(on_disk_inode.i_checksum_hi) << 16) |
      on_disk_inode.osd2.linux2.l_i_checksum_lo;
  if (calculated != stored_checksum) {
    std::cerr << "\n[ERROR] Inode " << inode_num
              << " checksum mismatch on disk before write!\n"
              << "Calculated Checksum: " << calculated << "\n"
              << "Stored Checksum:     " << stored_checksum << "\n"
              << "Skipping write to prevent corruption.\n\n";
    return false;
  }

  // 3. Compute new checksum and update fields in our copy
  inode new_inode = inode_in;
  std::vector<char> new_inode_buf(256, 0);
  std::memcpy(new_inode_buf.data(), &new_inode, copy_size);

  uint32_t new_calculated =
      checksum_inode(reinterpret_cast<char *>(sb.s_uuid), inode_num,
                     new_inode.i_generation, new_inode_buf.data());
  new_inode.osd2.linux2.l_i_checksum_lo =
      static_cast<uint16_t>(new_calculated & 0xFFFF);
  new_inode.i_checksum_hi =
      static_cast<uint16_t>((new_calculated >> 16) & 0xFFFF);

  // 4. Do the write
  size_t write_size =
      std::min(sizeof(inode), static_cast<size_t>(sb.s_inode_size));
  if (!write_bytes(image, offset, &new_inode, write_size)) {
    std::cerr << "update_inode: erro ao escrever inode " << inode_num << "\n";
    return false;
  }
  return true;
}

bool Ext4FS::write_inode(uint32_t inode_num, const inode &inode_in) {
  return update_inode(inode_num, inode_in);
}

bool Ext4FS::update_inode_size(uint32_t inode_num, inode &inode_in,
                               uint64_t new_size) {
  if (inode_num == 0 || inode_num > sb.s_inodes_count) {
    std::cerr << "update_inode_size: número de inode inválido: " << inode_num
              << "\n";
    return false;
  }

  inode_in.i_size_lo = static_cast<uint32_t>(new_size & 0xFFFFFFFFULL);
  inode_in.i_size_high =
      static_cast<uint32_t>((new_size >> 32) & 0xFFFFFFFFULL);

  return update_inode(inode_num, inode_in);
}

// alloc_inode: aloca o primeiro inode livre no SA, atualizando bitmaps e
// contadores
uint32_t Ext4FS::alloc_inode() {
  /**
   * Percorre cada grupo consultando o GDT antes de ler o bitmap,
   * evitando I/O desnecessário em grupos sem inodes livres.
   */
  for (uint64_t bg = 0; bg < num_groups; bg++) {
    uint32_t free_in_group = get_gd_free_inodes_count(bg);

    if (free_in_group == 0) {
      continue; // grupo sem inodes livres — pula
    }

    // Lê o bitmap de inodes do grupo
    uint64_t bitmap_block = get_inode_bitmap_block(static_cast<uint32_t>(bg));
    uint64_t bitmap_offset = get_block_offset(bitmap_block);
    std::vector<char> bitmap(block_size);

    if (!read_bytes(image, bitmap_offset, bitmap.data(), block_size)) {
      std::cerr << "alloc_inode: erro ao ler bitmap do grupo " << bg << "\n";
      continue;
    }

    // Varre os bits do bitmap procurando o primeiro 0 (inode livre)
    for (uint32_t i = 0; i < sb.s_inodes_per_group; i++) {
      if (!test_bit(bitmap, i)) {

        // prepara modificações em memória

        // Bitmap com o bit do inode marcado como usado
        std::vector<char> new_bitmap = bitmap;
        set_bit(new_bitmap, i);

        // Cópia do group descriptor com contador de inodes livres decrementado
        group_description new_gd = gdt[bg];
        uint32_t free_count = free_in_group - 1;
        new_gd.bg_free_inodes_count_lo =
            static_cast<uint16_t>(free_count & 0xFFFF);
        new_gd.bg_free_inodes_count_hi =
            static_cast<uint16_t>((free_count >> 16) & 0xFFFF);

        // Cópia do superbloco com s_free_inodes_count decrementado
        super_block new_sb = sb;
        new_sb.s_free_inodes_count--;

        // grava na imagem (ordem: bitmap → GDT → superbloco)
        // Se qualquer escrita falhar, aborta sem aplicar as seguintes,
        // mantendo o estado anterior intacto.

        gdt[bg] = new_gd;
        sb = new_sb;

        if (!update_inode_bitmap(bg, new_bitmap)) {
          std::cerr << "alloc_inode: erro ao escrever bitmap do grupo " << bg
                    << "\n";
          return 0;
        }

        if (!update_gdt_entry(bg)) {
          std::cerr << "alloc_inode: erro ao escrever GDT do grupo " << bg
                    << "\n";
          return 0;
        }

        if (!update_sb()) {
          std::cerr << "alloc_inode: erro ao escrever superbloco\n";
          return 0;
        }

        // Converte (grupo, índice) → número de inode (base 1)
        uint32_t inode_num =
            static_cast<uint32_t>(bg) * sb.s_inodes_per_group + i + 1;
        return inode_num;
      }
    }
  }

  std::cerr << "alloc_inode: sem inodes livres no SA\n";
  return 0;
}

// alloc_blocks: aloca até 'count' blocos contíguos livres no SA.
// Percorre os grupos em ordem; dentro de cada grupo varre o bitmap procurando
// a maior sequência contígua de bits 0, limitada a 'count'. Marca todos de uma
// vez e atualiza GDT e superbloco. Retorna o primeiro bloco alocado e escreve
// em 'allocated_count' a quantidade efetivamente alocada.
uint64_t Ext4FS::alloc_blocks(uint64_t count, uint64_t &allocated_count) {
  allocated_count = 0;

  if (count == 0) {
    return 0;
  }

  // Percorre cada grupo consultando o GDT antes de ler o bitmap,
  // evitando I/O desnecessário em grupos sem blocos livres.
  for (uint64_t bg = 0; bg < num_groups; bg++) {
    uint32_t free_in_group = get_gd_free_blocks_count(bg);

    if (free_in_group == 0) {
      continue; // grupo sem blocos livres — pula
    }

    // Lê o bitmap de blocos do grupo
    uint64_t bitmap_block = get_block_bitmap_block(static_cast<uint32_t>(bg));
    uint64_t bitmap_offset = get_block_offset(bitmap_block);
    std::vector<char> bitmap(block_size);

    if (!read_bytes(image, bitmap_offset, bitmap.data(), block_size)) {
      std::cerr << "alloc_blocks: erro ao ler bitmap do grupo " << bg << "\n";
      continue;
    }

    // Varre o bitmap procurando a melhor sequência contígua de bits livres.
    // Estratégia: encontra a primeira sequência de comprimento >= 1 e para
    // assim que atingir 'count' ou acabarem os bits do grupo.
    uint32_t best_start = 0;
    uint64_t best_len = 0;
    uint32_t run_start = 0;
    uint64_t run_len = 0;

    // Varre cada bit do bitmap do grupo
    for (uint32_t i = 0; i < sb.s_blocks_per_group; i++) {
      if (!test_bit(bitmap, i)) {
        if (run_len == 0) {
          run_start = i; // início de uma nova sequência
        }
        run_len++;

        // Assim que acharmos uma run com pelo menos 'count' blocos, para na
        // hora
        if (run_len >= count) {
          best_start = run_start;
          best_len = count;
          break;
        }
      } else {
        // fim de uma run — guarda se for a maior vista até agora
        if (run_len > best_len) {
          best_start = run_start;
          best_len = run_len;
        }
        run_len = 0;
      }
    }

    // Fecha a última run caso o loop termine com uma sequência em aberto
    if (run_len > best_len) {
      best_start = run_start;
      best_len = run_len;
    }

    if (best_len == 0) {
      continue; // nenhum bloco livre encontrado neste grupo
    }

    // Limita ao máximo solicitado
    uint64_t to_alloc = (best_len < count) ? best_len : count;

    // prepara modificações em memória

    // Bitmap com os bits dos blocos marcados como usados
    std::vector<char> new_bitmap = bitmap;
    for (uint64_t k = 0; k < to_alloc; k++) {
      set_bit(new_bitmap, best_start + static_cast<uint32_t>(k));
    }

    // Cópia do group descriptor com contador de blocos livres decrementado
    group_description new_gd = gdt[bg];
    uint32_t free_count = free_in_group - static_cast<uint32_t>(to_alloc);
    new_gd.bg_free_blocks_count_lo = static_cast<uint16_t>(free_count & 0xFFFF);
    new_gd.bg_free_blocks_count_hi =
        static_cast<uint16_t>((free_count >> 16) & 0xFFFF);

    // Atualiza estado em memória antes de gravar
    uint64_t free_blocks = get_free_blocks_count() - to_alloc;
    gdt[bg] = new_gd;
    sb.s_free_blocks_count_lo = static_cast<uint32_t>(free_blocks & 0xFFFFFFFF);
    sb.s_free_blocks_count_hi = static_cast<uint32_t>(free_blocks >> 32);

    // grava na imagem (ordem: bitmap → GDT → superbloco)
    if (!update_block_bitmap(bg, new_bitmap)) {
      std::cerr << "alloc_blocks: erro ao escrever bitmap do grupo " << bg
                << "\n";
      return 0;
    }

    if (!update_gdt_entry(bg)) {
      std::cerr << "alloc_blocks: erro ao escrever GDT do grupo " << bg << "\n";
      return 0;
    }

    if (!update_sb()) {
      std::cerr << "alloc_blocks: erro ao escrever superbloco\n";
      return 0;
    }

    // Converte (grupo, índice local) → número absoluto do primeiro bloco
    // alocado
    uint64_t first_block = get_abs_block(bg, best_start);

    allocated_count = to_alloc;
    return first_block;
  }

  std::cerr << "alloc_blocks: sem blocos livres no SA\n";
  return 0;
}

bool Ext4FS::write_extent_to_inode(uint32_t inode_num, inode &inode_in,
                                   uint32_t logical_block, uint64_t phys_block,
                                   uint16_t len) {

  // Validações básicas
  if (inode_num == 0 || inode_num > sb.s_inodes_count) {
    std::cerr << "write_extent_to_inode: inode_num inválido\n";
    return false;
  }
  if (len == 0) {
    std::cerr << "write_extent_to_inode: len == 0\n";
    return false;
  }

  // Magic number da extent tree
  static constexpr uint16_t EXT4_EXT_MAGIC = 0xF30A;
  static constexpr uint16_t MAX_INLINE_EXTENTS = 4;

  ext4_extent_header *hdr =
      reinterpret_cast<ext4_extent_header *>(inode_in.i_block);

  // Inicializa a extent tree se o inode ainda não a tem
  if (hdr->eh_magic != EXT4_EXT_MAGIC) {
    hdr->eh_magic = EXT4_EXT_MAGIC;
    hdr->eh_entries = 0;
    hdr->eh_max = MAX_INLINE_EXTENTS;
    hdr->eh_depth = 0;
    hdr->eh_generation = 0;
  }

  // árvore inline de folhas (depth == 0)
  if (hdr->eh_depth == 0) {
    ext4_extent *extents = reinterpret_cast<ext4_extent *>(hdr + 1);

    // 1. Coalescing (Tenta agrupar com o último bloco se forem contíguos)
    if (hdr->eh_entries > 0) {
      ext4_extent &last = extents[hdr->eh_entries - 1];
      uint64_t last_phys = get_extent_phys_block(last);
      uint16_t last_len = last.ee_len;

      bool logically_contiguous = (last.ee_block + last_len == logical_block);
      bool physically_contiguous = (last_phys + last_len == phys_block);

      if (logically_contiguous && physically_contiguous) {
        uint32_t new_len = static_cast<uint32_t>(last_len) + len;
        if (new_len <= 32768) {
          last.ee_len = static_cast<uint16_t>(new_len);
          return update_inode(inode_num, inode_in);
        }
      }
    }

    // Procura se o bloco lógico já está mapeado (Caso de Substituição)
    // Isso deve vir ANTES do teste de "bloco cheio", pois substituir não
    // aumenta o número de entries!
    for (uint16_t i = 0; i < hdr->eh_entries; i++) {
      if (extents[i].ee_block == logical_block) {
        extents[i].ee_len = len;
        extents[i].ee_start_lo = static_cast<uint32_t>(phys_block & 0xFFFFFFFF);
        extents[i].ee_start_hi =
            static_cast<uint16_t>((phys_block >> 32) & 0xFFFF);
        return update_inode(inode_num, inode_in); // Atualiza memória e disco
      }
    }

    // Se não foi substituição e já atingiu o limite de 4, retorna false
    if (hdr->eh_entries >= MAX_INLINE_EXTENTS) {
      std::cerr << "write_extent_to_inode: limite de extents inline atingido "
                   "(máx 4). Split não suportado.\n";
      return false;
    }

    // Insere um novo extent
    ext4_extent &slot = extents[hdr->eh_entries];
    slot.ee_block = logical_block;
    slot.ee_len = len;
    slot.ee_start_lo = static_cast<uint32_t>(phys_block & 0xFFFFFFFF);
    slot.ee_start_hi = static_cast<uint16_t>((phys_block >> 32) & 0xFFFF);
    hdr->eh_entries++;

    return update_inode(inode_num, inode_in);
  }

  // Árvores com depth >= 1 não são suportadas
  std::cerr
      << "write_extent_to_inode: Árvores com depth > 0 não são suportadas.\n";
  return false;
}

bool Ext4FS::find_mapped_block(const inode &inode_in, uint32_t logical_block,
                               uint64_t &out_phys_block,
                               uint16_t &out_len) const {
  static constexpr uint16_t EXT4_EXT_MAGIC = 0xF30A;

  const ext4_extent_header *hdr =
      reinterpret_cast<const ext4_extent_header *>(inode_in.i_block);

  if (hdr->eh_magic != EXT4_EXT_MAGIC || hdr->eh_depth != 0) {
    return false;
  }

  const ext4_extent *extents = reinterpret_cast<const ext4_extent *>(hdr + 1);

  for (uint16_t i = 0; i < hdr->eh_entries; i++) {
    const ext4_extent &ext = extents[i];
    uint16_t real_len = (ext.ee_len <= 32768)
                            ? ext.ee_len
                            : static_cast<uint16_t>(ext.ee_len - 32768);

    if (logical_block >= ext.ee_block &&
        logical_block < ext.ee_block + real_len) {
      uint64_t phys_start = get_extent_phys_block(ext);
      out_phys_block = phys_start + (logical_block - ext.ee_block);
      out_len = real_len;

      return true;
    }
  }

  return false;
}

void Ext4FS::print_superblock() const {
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
void Ext4FS::print_gdt() const {
  const int w = 30;
  std::cout << std::left << std::setfill(' ');

  for (size_t i = 0; i < gdt.size(); i++) {
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
void Ext4FS::print_inode(const inode &inode_in, uint32_t /*inode_num*/) const {
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
  for (int i = 0; i < 15; i++) {
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

// write_block_bytes: escreve bytes em um bloco físico da imagem ext4
bool Ext4FS::write_block_bytes(uint64_t phys_block,
                               const std::vector<char> &buffer) {
  if (!image.is_open()) {
    return false;
  }

  uint64_t offset = get_block_offset(phys_block);

  // Escreve até o limite do tamanho do bloco ou o tamanho do buffer enviado
  uint64_t bytes_to_write =
      std::min(block_size, static_cast<uint64_t>(buffer.size()));

  // Reutiliza o write_bytes de io_utils.h.
  // Como o buffer é const, usamos const_cast para casar com o ponteiro void*
  return write_bytes(image, offset, const_cast<char *>(buffer.data()),
                     bytes_to_write);
}

bool Ext4FS::write_to_file(uint32_t inode_num, inode &inode_in,
                           uint32_t logical_block,
                           const std::vector<char> &buffer) {
  // 1. Validações iniciais básicas
  if (inode_num == 0 || !image.is_open() || buffer.empty()) {
    std::cerr << "write_to_file: parâmetros inválidos ou imagem fechada\n";
    return false;
  }

  uint64_t total_bytes = buffer.size();
  uint64_t bytes_written = 0;
  uint32_t curr_logical_block = logical_block;

  while (bytes_written < total_bytes) {
    uint64_t chunk_size =
        std::min<uint64_t>(block_size, total_bytes - bytes_written);
    std::vector<char> chunk(buffer.begin() + bytes_written,
                            buffer.begin() + bytes_written + chunk_size);

    // verifica se esse bloco lógico já está mapeado, se estiver eaproveita o
    // bloco físico ao invés de alocar um novo
    uint64_t phys_block = 0;
    uint16_t existing_len = 0;
    bool already_mapped = find_mapped_block(inode_in, curr_logical_block,
                                            phys_block, existing_len);

    if (!already_mapped) {
      uint64_t alloc_count = 0;
      phys_block = alloc_blocks(1, alloc_count);
      if (phys_block == 0 || alloc_count == 0) {
        std::cerr << "write_to_file: falha ao alocar bloco físico (sem espaço "
                     "livre)\n";
        return false;
      }
    }

    if (!write_block_bytes(phys_block, chunk)) {
      std::cerr << "write_to_file: erro físico ao escrever dados no bloco "
                << phys_block << "\n";
      return false;
    }

    if (!already_mapped) {
      if (!write_extent_to_inode(inode_num, inode_in, curr_logical_block,
                                 phys_block, 1)) {
        std::cerr
            << "write_to_file: falha ao atualizar a árvore de extents do inode "
            << inode_num << "\n";
        return false;
      }
    }

    bytes_written += chunk_size;
    curr_logical_block++;
  }

  uint64_t current_size = get_file_size(inode_in);

  uint64_t new_potential_size =
      (static_cast<uint64_t>(logical_block) * block_size) + total_bytes;

  if (current_size < new_potential_size) {
    if (!update_inode_size(inode_num, inode_in, new_potential_size)) {
      std::cerr
          << "write_to_file: falha ao atualizar tamanho do inode no disco\n";
      return false;
    }
  }

  return true;
}

bool Ext4FS::write_dir_entry(uint32_t parent_inode_num, uint32_t new_inode_num,
                             const std::string &name, uint8_t file_type) {
  inode parent_inode;
  if (!read_inode(parent_inode_num, parent_inode)) {
    std::cerr << "write_dir_entry: erro ao ler inode pai\n";
    return false;
  }

  uint32_t req_len = dir_ent_min_len(name.length());

  ext4_extent_header *hdr =
      reinterpret_cast<ext4_extent_header *>(parent_inode.i_block);

  bool space_found = false;
  uint64_t target_phys_block = 0;
  std::vector<char> block_buf(block_size, 0);

  // tenta reaproveitar o último bloco
  if (hdr->eh_magic == 0xF30A && hdr->eh_depth == 0 && hdr->eh_entries > 0) {
    ext4_extent *exts = reinterpret_cast<ext4_extent *>(hdr + 1);
    ext4_extent &last_ext = exts[hdr->eh_entries - 1];

    uint64_t phys_start = (static_cast<uint64_t>(last_ext.ee_start_hi) << 32) |
                          last_ext.ee_start_lo;
    target_phys_block = phys_start + last_ext.ee_len - 1;

    if (read_bytes(image, get_block_offset(target_phys_block), block_buf.data(),
                   block_size)) {
      uint32_t offset = 0;
      ext4_dir_entry_2 *entry = nullptr;

      // encontra a última entrada no diretório pai
      while (offset < block_size) {
        entry = reinterpret_cast<ext4_dir_entry_2 *>(block_buf.data() + offset);
        if (offset + entry->rec_len == block_size || entry->rec_len == 0) {
          break;
        }
        offset += entry->rec_len;
      }

      if (entry != nullptr && entry->rec_len > 0) {
        uint32_t min_len = 0;
        if (entry->inode != 0) {
          min_len = (8 + entry->name_len + 3) & ~3;
        }

        uint32_t free_space = entry->rec_len - min_len;

        // se houver espaço
        if (free_space >= req_len) {
          if (min_len > 0) {
            entry->rec_len = min_len;

            ext4_dir_entry_2 *new_entry = reinterpret_cast<ext4_dir_entry_2 *>(
                block_buf.data() + offset + min_len);
            new_entry->inode = new_inode_num;
            new_entry->rec_len =
                free_space; // ocupa o resto do bloco; deve ser revertido ao
                            // escrever outro dir_entry (ou remover esse)
            new_entry->name_len = static_cast<uint8_t>(name.length());
            new_entry->file_type = file_type;
            std::memcpy(new_entry->name, name.c_str(), name.length());
          } else {
            // reaproveita inode deletado previametne
            entry->inode = new_inode_num;
            entry->name_len = static_cast<uint8_t>(name.length());
            entry->file_type = file_type;
            std::memcpy(entry->name, name.c_str(), name.length());
          }

          if (!write_block_bytes(target_phys_block, block_buf))
            return false;
          space_found = true;
        }
      }
    }
  }

  // se não ouver espaço, aloca um bloco novo e escreve nele
  if (!space_found) {
    uint64_t alloc_count = 0;
    uint64_t new_phys_block = alloc_blocks(1, alloc_count);
    if (new_phys_block == 0)
      return false;

    std::fill(block_buf.begin(), block_buf.end(), 0);

    ext4_dir_entry_2 *new_entry =
        reinterpret_cast<ext4_dir_entry_2 *>(block_buf.data());
    new_entry->inode = new_inode_num;
    new_entry->rec_len = block_size;
    new_entry->name_len = static_cast<uint8_t>(name.length());
    new_entry->file_type = file_type;
    std::memcpy(new_entry->name, name.c_str(), name.length());

    if (!write_block_bytes(new_phys_block, block_buf))
      return false;

    // mapeia o novo bloco lógico no i_block do diretório pai
    uint32_t logical_block =
        (get_file_size(parent_inode) + block_size - 1) / block_size;
    if (!write_extent_to_inode(parent_inode_num, parent_inode, logical_block,
                               new_phys_block, 1)) {
      return false;
    }

    uint64_t new_size = get_file_size(parent_inode) + block_size;
    parent_inode.i_size_lo = static_cast<uint32_t>(new_size & 0xFFFFFFFF);
    parent_inode.i_size_high =
        static_cast<uint32_t>((new_size >> 32) & 0xFFFFFFFF);
    parent_inode.i_blocks_lo += (block_size / 512);
  }

  parent_inode.i_mtime = time(NULL);
  parent_inode.i_ctime = time(NULL);

  static constexpr uint8_t EXT4_FT_DIR = 2;
  if (file_type == EXT4_FT_DIR) {
    parent_inode.i_links_count++;

    uint32_t parent_bg = get_inode_block_group(parent_inode_num);
    set_gd_used_dirs_count(parent_bg, get_gd_used_dirs_count(parent_bg) + 1);
    if (!update_gdt_entry(parent_bg)) {
      return false;
    }
  }

  return update_inode(parent_inode_num, parent_inode);
}

bool Ext4FS::free_inode(uint32_t inode_num, bool is_dir) {
  if (inode_num == 0 || inode_num > sb.s_inodes_count) {
    std::cerr << "free_inode: Número de Inode inválido.\n";
    return false;
  }

  uint32_t bg = get_inode_block_group(inode_num);
  uint32_t inode_bit_offset = get_inode_bitmap_offset(inode_num);

  uint64_t bitmap_block = get_inode_bitmap_block(bg);
  uint64_t offset = get_block_offset(bitmap_block);
  std::vector<char> bitmap(block_size, 0);

  if (!read_bytes(image, offset, bitmap.data(), block_size)) {
    std::cerr << "free_inode: Erro ao ler Inode Bitmap\n";
    return false;
  }

  clear_bit(bitmap, inode_bit_offset);

  if (!update_inode_bitmap(bg, bitmap)) {
    return false;
  }

  set_gd_free_inodes_count(bg, get_gd_free_inodes_count(bg) + 1);
  if (is_dir) {
    uint32_t used_dirs = get_gd_used_dirs_count(bg);
    if (used_dirs > 0) {
      set_gd_used_dirs_count(bg, used_dirs - 1);
    }
  }
  update_gdt_entry(bg);

  sb.s_free_inodes_count++;
  return update_sb();
}

bool Ext4FS::free_blocks(uint64_t start_phys_block, uint64_t count) {
  if (start_phys_block == 0 || count == 0)
    return false;

  uint32_t current_bg = 0xFFFFFFFF;
  std::vector<char> bitmap(block_size, 0);
  uint64_t freed_in_current_group = 0;
  uint64_t total_freed = 0;

  for (uint64_t i = 0; i < count; i++) {
    uint64_t current_block = start_phys_block + i;

    uint32_t bg = get_block_block_group(current_block);
    uint32_t block_bit_offset = get_block_bitmap_offset(current_block);

    if (bg != current_bg) {
      if (current_bg != 0xFFFFFFFF) {
        update_block_bitmap(current_bg, bitmap);
        set_gd_free_blocks_count(current_bg,
                                 get_gd_free_blocks_count(current_bg) +
                                     freed_in_current_group);
        update_gdt_entry(current_bg);
      }

      current_bg = bg;
      freed_in_current_group = 0;
      uint64_t bitmap_block = get_block_bitmap_block(bg);
      if (!read_bytes(image, get_block_offset(bitmap_block), bitmap.data(),
                      block_size)) {
        return false;
      }
    }

    clear_bit(bitmap, block_bit_offset);

    freed_in_current_group++;
    total_freed++;
  }

  if (current_bg != 0xFFFFFFFF) {
    update_block_bitmap(current_bg, bitmap);
    set_gd_free_blocks_count(current_bg, get_gd_free_blocks_count(current_bg) +
                                             freed_in_current_group);
    update_gdt_entry(current_bg);
  }

  uint64_t new_free_blocks = get_free_blocks_count() + total_freed;
  sb.s_free_blocks_count_lo =
      static_cast<uint32_t>(new_free_blocks & 0xFFFFFFFFULL);
  sb.s_free_blocks_count_hi =
      static_cast<uint32_t>((new_free_blocks >> 32) & 0xFFFFFFFFULL);

  return update_sb();
}

uint32_t Ext4FS::remove_dir_entry(uint32_t parent_inode_num,
                                  const std::string &target_name) {
  inode parent_inode;
  if (!read_inode(parent_inode_num, parent_inode)) {
    return 0;
  }

  ext4_extent_header *hdr =
      reinterpret_cast<ext4_extent_header *>(parent_inode.i_block);

  if (hdr->eh_magic != 0xF30A || hdr->eh_depth != 0) {
    return 0;
  }

  ext4_extent *exts = reinterpret_cast<ext4_extent *>(hdr + 1);

  for (uint16_t i = 0; i < hdr->eh_entries; i++) {
    uint64_t phys_start = (static_cast<uint64_t>(exts[i].ee_start_hi) << 32) |
                          exts[i].ee_start_lo;

    for (uint16_t j = 0; j < exts[i].ee_len; j++) {
      uint64_t phys_block = phys_start + j;

      std::vector<char> block_buf(block_size, 0);
      if (!read_bytes(image, get_block_offset(phys_block), block_buf.data(),
                      block_size)) {
        continue;
      }

      uint32_t offset = 0;
      ext4_dir_entry_2 *prev_entry = nullptr;

      while (offset < block_size) {
        ext4_dir_entry_2 *entry =
            reinterpret_cast<ext4_dir_entry_2 *>(block_buf.data() + offset);

        if (entry->rec_len == 0)
          break;

        if (entry->inode != 0 && entry->name_len == target_name.length()) {
          if (std::string(entry->name, entry->name_len) == target_name) {
            uint32_t removed_inode_num = entry->inode;
            uint8_t removed_file_type = entry->file_type;

            if (prev_entry != nullptr) {
              prev_entry->rec_len += entry->rec_len;
            } else {
              entry->inode = 0;
            }

            if (!write_block_bytes(phys_block, block_buf)) {
              return 0;
            }

            parent_inode.i_mtime = time(NULL);
            parent_inode.i_ctime = time(NULL);

            static constexpr uint8_t EXT4_FT_DIR = 2;
            if (removed_file_type == EXT4_FT_DIR &&
                parent_inode.i_links_count > 0) {
              parent_inode.i_links_count--;
            }

            update_inode(parent_inode_num, parent_inode);

            return removed_inode_num;
          }
        }

        prev_entry = entry;
        offset += entry->rec_len;
      }
    }
  }

  return 0;
}

uint32_t Ext4FS::find_inode_in_dir(uint32_t parent_inode_num,
                                   const std::string &name) {
  inode parent_inode;
  if (!read_inode(parent_inode_num, parent_inode)) {
    std::cerr << "find_inode_in_dir: erro ao ler inode pai " << parent_inode_num
              << "\n";
    return 0;
  }

  if (!inode_is_dir(parent_inode)) {
    std::cerr << "find_inode_in_dir: inode " << parent_inode_num
              << " não é um diretório\n";
    return 0;
  }

  std::vector<char> dir_content =
      read_inode_content(parent_inode, parent_inode_num);
  if (dir_content.empty()) {
    return 0;
  }

  return find_inode_by_dir(dir_content, name);
}
