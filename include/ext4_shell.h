/**
 * Classe Ext4Shell — shell interativo para navegação em imagens ext4.
 *
 * Fornece uma interface de linha de comando que permite explorar o conteúdo
 * de uma imagem ext4: listar diretórios, ler arquivos, inspecionar inodes,
 * exportar e importar arquivos.
 */

#ifndef SHELL_H
#define SHELL_H

#include "ext4_utils.h"
#include <functional>
#include <string>
#include <unordered_map>

class Ext4Shell
{
private:
  Ext4FS fs;             // Instância do sistema de arquivos ext4
  uint32_t curr_inode;   // Número do inode do diretório corrente (inicia em 2 = raiz)
  std::string curr_path; // Caminho absoluto do diretório corrente (inicia em "/")
  std::string img_path;  // Caminho da imagem de disco do EXT4

  // Mapa de nome de comando -> função handler; permite dispatch O(1)
  std::unordered_map<std::string,
                     std::function<void(const std::vector<std::string> &)>>
      commandsMap;

  /**
   * Lista todos os comandos disponíveis no shell.
   */
  void commands();

  /**
   * Exibe informações da imagem e do sistema de arquivos.
   */
  void info();

  /**
   * Imprime o conteúdo de um arquivo como texto na saída padrão.
   *
   * @param path caminho do arquivo (relativo ou absoluto)
   */
  void cat(const std::string &path);

  /**
   * Exibe todos os campos do inode de um arquivo ou diretório.
   *
   * @param path caminho do arquivo ou diretório (relativo ou absoluto)
   */
  void attr(const std::string &path);

  /**
   * Navega para um diretório, atualizando curr_inode e curr_path.
   * Suporta caminhos absolutos, relativos, "." e "..".
   *
   * @param path caminho do diretório destino (relativo ou absoluto)
   */
  void cd(const std::string &path);

  /**
   * Lista as entradas do diretório corrente.
   * Exibe tipo (d=diretório, f=arquivo, l=link), número de inode e nome.
   */
  void ls();

  /**
   * Verifica se um inode está livre ou ocupado no bitmap de inodes.
   * (Não implementado)
   *
   * @param inode_num número do inode a verificar
   */
  void testi(uint32_t inode_num);

  /**
   * Verifica se um bloco está livre ou ocupado no bitmap de blocos.
   *
   * @param block_num número do bloco a verificar
   */
  void testb(uint32_t block_num);

  /**
   * Copia um arquivo da imagem ext4 para o sistema de arquivos do SO.
   * ("export" é palavra reservada em C++, daí o nome ext4_export)
   *
   * @param source caminho do arquivo de origem na imagem (relativo ou absoluto)
   * @param target caminho de destino no sistema de arquivos do SO
   */
  void ext4_export(const std::string &source, const std::string &target);

  /**
   * Exibe o caminho absoluto do diretório corrente.
   */
  void pwd();

  /**
   * Importa um arquivo do sistema de arquivos do SO para a imagem ext4.
   * ("import" é palavra reservada em C++, daí o nome ext4_import)
   * (Não implementado)
   *
   * @param source caminho do arquivo de origem no SO
   * @param target caminho de destino na imagem ext4 (relativo ou absoluto)
   */
  void ext4_import(const std::string &source, const std::string &target);

  /**
   * Aloca um inode livre no SA e exibe o número do inode alocado.
   */
  void ialloc();

  /**
   * Aloca um ou mais blocos livres no SA e exibe os números alocados.
   *
   * @param count quantidade de blocos a alocar (default: 1)
   */
  void balloc(uint64_t count = 1);

  /**
   * Despacha a entrada do usuário para o comando correto.
   * O primeiro token é o nome do comando; os demais são os argumentos.
   *
   * @param splitString vetor de tokens da linha de comando digitada pelo usuário
   */
  void dispatch(std::vector<std::string> splitString);

  /**
   * Resolve um caminho (relativo ou absoluto) para o número de inode
   * correspondente. Caminhos absolutos partem do inode raiz (2); relativos
   * partem de curr_inode.
   *
   * @param path caminho a ser resolvido
   * @returns o número do inode encontrado; 0 se o caminho não existe.
   */
  uint32_t resolve_path(const std::string &path);

  /**
   * Cria um diretório com o nome especificado
   *
   * @param name nome do diretório a ser criado
   */
  void mkdir(const std::string &path);

  /**
   * Testa a funcionalidade de extents.
   *
   * @param args argumentos para o comando
   */
  void test_extent(const std::vector<std::string> &args);

public:
  /**
   * Constrói o shell e inicializa o sistema de arquivos a partir da imagem.
   *
   * @param img_path caminho para o arquivo de imagem ext4 a ser aberto
   */
  explicit Ext4Shell(const std::string &img_path);

  /**
   * Inicia o loop principal do shell, lendo e executando comandos do usuário
   * até o encerramento do programa.
   */
  void run();
};

#endif
