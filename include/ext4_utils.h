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

bool read_gdt(std::fstream& image, const super_block& sb, std::vector<uint8_t>& buf);

/**
 *  Imprime um superbloco
 *  @param sb: objeto super_block onde o superbloco está armazenado
 */
void print_superblock(const super_block& sb);

#endif
