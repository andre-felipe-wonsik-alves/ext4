#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

/**
 *  Abre a imagem de um sistema de arquivos
 *  @param path: caminho para o arquivo da imagem
 *  @param image: objeto std::fstream onde a imagem será aberta
 *  @returns true se a imagem foi aberta com sucesso; false caso contrário
 */
bool open_image(const std::string& path, std::fstream& image);

/**
 *  Lê n bytes de uma imagem a partir de um offset 
 *  @param image: objeto std::fstream onde a imagem está aberta
 *  @param offset: byte inicial para leitura
 *  @param buf: objeto para armazenar os bytes lidos
 *  @param n: quantidade de bytes lidos
 *  @returns true se os bytes foram lidos com sucesso; false caso contrário
 */
bool read_bytes(std::fstream& image, uint64_t offset, void* buf, size_t n);

/**
 *  Escreve n bytes em uma imagem a partir de um offset 
 *  @param image: objeto std::fstream onde a imagem está aberta
 *  @param offset: byte inicial para escrita
 *  @param buf: objeto contendo os bytes a serem escritos
 *  @param n: quantidade de bytes escritos
 *  @returns true se os bytes foram escritos com sucesso; false caso contrário
 */
bool write_bytes(std::fstream& image, uint64_t offset, void* buf, size_t n);

/**
 * Divide uma string em substrings conforme delimitador
 * @param str: string
 * @param delimiter: delimitador. Por padrão. delimiter = '/'
 * @returns objeto std::vector<std::string> contendo as substrings separadas
 */
std::vector<std::string> split_tokens(const std::string& str, char delimiter = '/');

#endif
