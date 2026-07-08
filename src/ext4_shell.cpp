/**
 * Implementação do shell interativo para navegação em imagens ext4.
 */

#include "ext4_shell.h"
#include "io_utils.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>

// Construtor: inicializa o SA e registra os comandos no mapa de dispatch
Ext4Shell::Ext4Shell(const std::string &img_path)
    : curr_inode(2), curr_path("/"), img_path(img_path) {

  if (!fs.init(img_path)) {
    std::cerr << "[!] Erro ao abrir a imagem: " << img_path << "\n";
  }

  /**
   * Registra cada comando no unordered_map usando lambdas.
   * O dispatch busca pelo nome do comando em O(1) e chama o handler
   * correspondente com o vetor de argumentos.
   */

  commandsMap.emplace(
      "commands",
      [this](const std::vector<std::string> &) -> void { commands(); });

  commandsMap.emplace(
      "info", [this](const std::vector<std::string> &) -> void { info(); });

  commandsMap.emplace(
      "cat", [this](const std::vector<std::string> &args) -> void {
        if (args.empty()) {
          std::cout << "[!] Caminho precisa ser informado para o comando cat\n";
          return;
        }
        cat(args[0]);
      });

  commandsMap.emplace(
      "attr", [this](const std::vector<std::string> &args) -> void {
        if (args.empty()) {
          std::cout
              << "[!] Caminho precisa ser informado para o comando attr\n";
          return;
        }
        attr(args[0]);
      });

  commandsMap.emplace(
      "cd", [this](const std::vector<std::string> &args) -> void {
        if (args.empty()) {
          std::cout << "[!] Caminho precisa ser informado para o comando cd\n";
          return;
        }
        cd(args[0]);
      });

  commandsMap.emplace(
      "ls", [this](const std::vector<std::string> &) -> void { ls(); });

  commandsMap.emplace(
      "testi", [this](const std::vector<std::string> &args) -> void {
        if (args.empty()) {
          std::cout << "[!] O número do inode precisa ser informado para o "
                       "comando testi\n";
          return;
        }
        try {
          uint32_t inode_num = static_cast<uint32_t>(std::stoul(args[0]));
          testi(inode_num);
        } catch (const std::exception &) {
          std::cout << "[!] Erro: o argumento do testi precisa ser um número "
                       "válido\n";
        }
      });

  commandsMap.emplace(
      "testb", [this](const std::vector<std::string> &args) -> void {
        if (args.empty()) {
          std::cout << "[!] O número do bloco precisa ser informado para o "
                       "comando testb\n";
          return;
        }
        try {
          uint32_t block_num = static_cast<uint32_t>(std::stoul(args[0]));
          testb(block_num);
        } catch (const std::exception &) {
          std::cout << "[!] Erro: o argumento do testb precisa ser um número "
                       "válido\n";
        }
      });

  commandsMap.emplace("export",
                      [this](const std::vector<std::string> &args) -> void {
                        if (args.size() < 2) {
                          std::cout << "[!] Origem e destino precisam ser "
                                       "informados para o comando export\n";
                          return;
                        }
                        ext4_export(args[0], args[1]);
                      });

  commandsMap.emplace(
      "pwd", [this](const std::vector<std::string> &) -> void { pwd(); });

  commandsMap.emplace("touch",
                      [this](const std::vector<std::string> &args) -> void {
                        if (args.empty()) {
                          std::cout << "[!] Nome do arquivo precisa ser "
                                       "informado para o comando touch\n";
                          return;
                        }
                        touch(args[0]);
                      });

  commandsMap.emplace("mkdir",
                      [this](const std::vector<std::string> &args) -> void {
                        if (args.empty()) {
                          std::cout << "[!] Nome do diretório precisa ser "
                                       "informado para o comando mkdir\n";
                          return;
                        }
                        mkdir(args[0]);
                      });

  commandsMap.emplace("rm",
                      [this](const std::vector<std::string> &args) -> void {
                        if (args.empty()) {
                          std::cout << "[!] Caminho do arquivo precisa ser "
                                       "informado para o comando rm\n";
                          return;
                        }
                        rm(args[0]);
                      });

  commandsMap.emplace("rmdir",
                      [this](const std::vector<std::string> &args) -> void {
                        if (args.empty()) {
                          std::cout << "[!] Caminho do diretório precisa ser "
                                       "informado para o comando rmdir\n";
                          return;
                        }
                        rmdir(args[0]);
                      });

  commandsMap.emplace(
      "rename", [this](const std::vector<std::string> &args) -> void {
        if (args.size() < 2) {
          std::cout << "[!] Uso: rename <arquivo> <novo_nome>\n";
          return;
        }
        ext4_rename(args[0], args[1]);
      });

  commandsMap.emplace(
      "import", [this](const std::vector<std::string> &args) -> void {
        if (args.size() < 2) {
          std::cout << "[!] Origem (SO) e destino (EXT4) precisam ser "
                       "informados para o comando import\n";
          return;
        }
        ext4_import(args[0], args[1]);
      });
}

// run: loop principal — lê input, tokeniza e despacha para o comando correto
void Ext4Shell::run() {
  std::cout << "\n\n  EXT4 FILESYSTEM SHELL  \n\n";
  std::cout
      << "[*] Use 'commands' para listar todos os comandos disponíveis\n\n";

  std::string input;

  while (true) {
    std::cout << curr_path << " >> ";
    std::getline(std::cin, input);

    // Divide a linha por espaços: "cat /etc/passwd" -> {"cat", "/etc/passwd"}
    const char delimiter = ' ';
    std::vector<std::string> splitString = split_tokens(input, delimiter);

    dispatch(splitString);

    std::cout << "\n";
  }
}

// dispatch: busca o comando no mapa e o executa com os argumentos fornecidos
void Ext4Shell::dispatch(std::vector<std::string> splitString) {
  if (splitString.empty()) {
    return;
  }

  const std::string &commandName = splitString[0];

  auto it = commandsMap.find(commandName);

  if (it == commandsMap.end()) {
    std::cout << "[!] Comando '" << commandName << "' não encontrado.\n";
    return;
  }

  // Remove o nome do comando, deixando apenas os argumentos para o handler
  std::vector<std::string> args(splitString.begin() + 1, splitString.end());

  it->second(args);
}

// commands: lista todos os comandos registrados no mapa
void Ext4Shell::commands() {
  std::cout << "Comandos disponiveis:\n";

  // 1. Copia apenas os nomes dos comandos para um vetor
  std::vector<std::string> sorted_names;
  for (const auto &pair : commandsMap) {
    sorted_names.push_back(pair.first);
  }

  // 2. Ordena o vetor em ordem alfabética
  std::sort(sorted_names.begin(), sorted_names.end());

  // 3. Exibe bonitinho
  for (const auto &name : sorted_names) {
    std::cout << "  - " << name << "\n";
  }
}

// resolve_path: converte caminho relativo ou absoluto em número de inode
uint32_t Ext4Shell::resolve_path(const std::string &path) {
  if (path.empty()) {
    return curr_inode;
  }

  // "/" sempre resolve para o inode raiz do ext4 (inode 2)
  if (path == "/") {
    return 2;
  }

  if (path[0] == '/') {
    // Caminho absoluto: busca a partir do inode raiz, pulando a barra inicial
    return fs.find_inode_by_path(path.substr(1), 2);
  }

  // Caminho relativo: busca a partir do diretório corrente
  return fs.find_inode_by_path(path, curr_inode);
}

// info: exibe o resumo da imagem, do espaço do SA e o dump completo do
// superbloco
void Ext4Shell::info() {
  std::cout << "==================================================\n";
  std::cout << "        INFORMAÇÕES DA IMAGEM (ARQUIVO SO)        \n";
  std::cout << "==================================================\n";
  std::cout << "Caminho da Imagem:   " << img_path << "\n";

  uint64_t block_size = fs.get_block_size();
  uint64_t total_blocks = fs.get_blocks_count();
  uint64_t free_blocks = fs.get_free_blocks_count();
  uint64_t used_blocks = total_blocks - free_blocks;

  uint32_t total_inodes = fs.get_inodes_count();
  uint32_t free_inodes = fs.get_free_inodes_count();
  uint32_t used_inodes = total_inodes - free_inodes;

  std::cout << "\n==================================================\n";
  std::cout << "       RESUMO DE ESPAÇO (SISTEMA DE ARQUIVOS)     \n";
  std::cout << "==================================================\n";
  std::cout << "Tamanho do Bloco: " << block_size << " bytes ("
            << (block_size / 1024.0) << " KiB)\n";
  std::cout << "Espaço Total: " << (total_blocks * block_size / 1024.0)
            << " KiB (" << (total_blocks * block_size / 1024.0 / 1024.0)
            << " MiB)\n";
  std::cout << "Espaço Usado: " << (used_blocks * block_size / 1024.0)
            << " KiB (" << (used_blocks * block_size / 1024.0 / 1024.0)
            << " MiB)\n";
  std::cout << "Espaço Disponível: " << (free_blocks * block_size / 1024.0)
            << " KiB (" << (free_blocks * block_size / 1024.0 / 1024.0)
            << " MiB)\n";
  std::cout << "Uso de Blocos: " << used_blocks << " / " << total_blocks << " ("
            << (total_blocks ? (used_blocks * 100.0 / total_blocks) : 0.0)
            << "%)\n";

  std::cout << "\nUso de Inodes: " << used_inodes << " / " << total_inodes
            << " ("
            << (total_inodes ? (used_inodes * 100.0 / total_inodes) : 0.0)
            << "%)\n";
}

// pwd: exibe o caminho absoluto do diretório corrente
void Ext4Shell::pwd() { std::cout << curr_path << "\n"; }

// ls: lista as entradas do diretório corrente com tipo e número de inode
void Ext4Shell::ls() {
  inode dir_inode;

  if (!fs.read_inode(curr_inode, dir_inode)) {
    std::cout << "[!] Erro ao ler o inode " << curr_inode << "\n";
    return;
  }

  if (!fs.inode_is_dir(dir_inode)) {
    std::cout << "[!] O inode corrente não é um diretório\n";
    return;
  }

  std::vector<char> dir_content = fs.read_inode_content(dir_inode, curr_inode);

  if (dir_content.empty()) {
    std::cout << "[!] Diretório vazio ou erro ao ler o conteúdo\n";
    return;
  }

  size_t offset = 0;
  while (offset < dir_content.size()) {
    const ext4_dir_entry_2 *entry =
        reinterpret_cast<const ext4_dir_entry_2 *>(&dir_content[offset]);

    if (entry->rec_len == 0) {
      break;
    }

    if (entry->inode != 0 && entry->name_len > 0) {
      // name não é null-terminated: usar name_len para construir a string
      std::string name(entry->name, entry->name_len);

      /**
       * file_type conforme ext4_dir_entry_2:
       *   1 = arquivo regular, 2 = diretório, 7 = link simbólico
       */
      char type_char = ' ';
      if (entry->file_type == 2)
        type_char = 'd';
      else if (entry->file_type == 1)
        type_char = 'f';
      else if (entry->file_type == 7)
        type_char = 'l';

      std::cout << type_char << "  " << std::setw(6) << entry->inode << "  "
                << name << "\n";
    }

    offset += entry->rec_len;
  }
}

// cat: imprime o conteúdo de um arquivo como texto na saída padrão
void Ext4Shell::cat(const std::string &path) {
  if (path.empty()) {
    std::cout << "[!] Caminho vazio\n";
    return;
  }

  uint32_t target_inode_num = resolve_path(path);
  if (target_inode_num == 0) {
    std::cout << "[!] Caminho não encontrado: " << path << "\n";
    return;
  }

  inode target_inode;
  if (!fs.read_inode(target_inode_num, target_inode)) {
    std::cout << "[!] Erro ao ler o inode " << target_inode_num << "\n";
    return;
  }

  if (fs.inode_is_dir(target_inode)) {
    std::cout << "[!] '" << path << "' é um diretório, não um arquivo\n";
    return;
  }

  std::vector<char> content =
      fs.read_inode_content(target_inode, target_inode_num);

  if (content.empty()) {
    // Arquivo pode estar legitimamente vazio
    return;
  }

  // Imprime os bytes diretamente; cout.write respeita bytes nulos no conteúdo
  std::cout.write(content.data(), static_cast<std::streamsize>(content.size()));
  std::cout << "\n";
}

// attr: exibe todos os campos do inode de um arquivo ou diretório
void Ext4Shell::attr(const std::string &path) {
  if (path.empty()) {
    std::cout << "[!] Caminho vazio\n";
    return;
  }

  uint32_t target_inode_num = resolve_path(path);
  if (target_inode_num == 0) {
    std::cout << "[!] Caminho não encontrado: " << path << "\n";
    return;
  }

  inode target_inode;
  if (!fs.read_inode(target_inode_num, target_inode)) {
    std::cout << "[!] Erro ao ler o inode " << target_inode_num << "\n";
    return;
  }

  fs.print_inode(target_inode, target_inode_num);
}

// cd: navega para um diretório, atualizando curr_inode e curr_path
void Ext4Shell::cd(const std::string &path) {
  if (path.empty()) {
    std::cout << "[!] Caminho vazio\n";
    return;
  }

  if (path == "..") {
    // Já estamos na raiz: não há para onde subir
    if (curr_path == "/") {
      return;
    }

    /**
     * Remove o último componente do caminho corrente para obter o pai.
     * Ex: "/home/user" -> "/home"
     * Caso especial: "/home" -> "/"
     */
    size_t last_slash =
        curr_path.rfind('/'); // Encontra a última barra no caminho corrente
    std::string parent_path =
        (last_slash == 0)
            ? "/"
            : curr_path.substr(0,
                               last_slash); // Se a última barra for a primeira,
                                            // o pai é a raiz; caso contrário,
                                            // pega o prefixo até a última barra

    uint32_t parent_inode_num = resolve_path(parent_path);
    if (parent_inode_num == 0) {
      std::cout << "[!] Erro ao resolver o diretório pai\n";
      return;
    }

    curr_inode = parent_inode_num;
    curr_path = parent_path;
    return;
  }

  // "." representa o diretório corrente — não faz nada
  if (path == ".") {
    return;
  }

  uint32_t target_inode_num = resolve_path(path);
  if (target_inode_num == 0) {
    std::cout << "[!] Caminho não encontrado: " << path << "\n";
    return;
  }

  inode target_inode;
  if (!fs.read_inode(target_inode_num, target_inode)) {
    std::cout << "[!] Erro ao ler o inode " << target_inode_num << "\n";
    return;
  }

  if (!fs.inode_is_dir(target_inode)) {
    std::cout << "[!] '" << path << "' não é um diretório\n";
    return;
  }

  curr_inode = target_inode_num;

  if (path[0] == '/') {
    // Caminho absoluto: substitui curr_path diretamente
    curr_path = path;
    // Remove barra final (exceto se for a raiz "/")
    if (curr_path.size() > 1 && curr_path.back() == '/') {
      curr_path.pop_back();
    }
  } else {
    // Caminho relativo: concatena ao caminho corrente
    if (curr_path.back() != '/') {
      curr_path +=
          '/'; // Adiciona barra se não houver no final do caminho corrente
    }
    curr_path += path;
  }
}

// testi: verifica se um inode está livre/ocupado no bitmap de inodes
void Ext4Shell::testi(uint32_t inode_num) {
  bool used = fs.inode_is_used(inode_num);
  std::cout << "Inode " << inode_num << " : " << (used ? "em uso" : "livre")
            << "\n";
}

// testb: verifica se um bloco está livre/ocupado no bitmap de blocos
void Ext4Shell::testb(uint32_t block_num) {
  bool used = fs.block_is_used(block_num);
  std::cout << "Bloco " << block_num << " : " << (used ? "em uso" : "livre")
            << "\n";
}

// ext4_export: copia um arquivo da imagem ext4 para o SO
void Ext4Shell::ext4_export(const std::string &source,
                            const std::string &target) {
  if (source.empty() || target.empty()) {
    std::cout << "[!] Origem ou destino vazios\n";
    return;
  }

  uint32_t src_inode_num = resolve_path(source);
  if (src_inode_num == 0) {
    std::cout << "[!] Arquivo de origem não encontrado: " << source << "\n";
    return;
  }

  inode src_inode;
  if (!fs.read_inode(src_inode_num, src_inode)) {
    std::cout << "[!] Erro ao ler o inode de origem\n";
    return;
  }

  if (fs.inode_is_dir(src_inode)) {
    std::cout << "[!] '" << source
              << "' é um diretório. export só suporta arquivos regulares\n";
    return;
  }

  std::vector<char> content = fs.read_inode_content(src_inode, src_inode_num);

  // Cria (ou sobrescreve) o arquivo de destino no SO em modo binário
  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    std::cout << "[!] Não foi possível criar o arquivo de destino: " << target
              << "\n";
    return;
  }

  out.write(content.data(), static_cast<std::streamsize>(content.size()));

  if (!out.good()) {
    std::cout << "[!] Erro ao escrever no arquivo de destino\n";
    return;
  }

  std::cout << "[*] Exportado: " << source << " -> " << target << " ("
            << content.size() << " bytes)\n";
}

// touch: cria um arquivo vazio no diretório corrente ou em um caminho absoluto
void Ext4Shell::touch(const std::string &path) {
  if (path.empty())
    return;

  // Verifica se já existe
  if (resolve_path(path) != 0) {
    std::cout << "[!] Arquivo ou diretório já existe.\n";
    return;
  }

  // Separa o nome do arquivo do caminho do diretório pai
  size_t last_slash = path.rfind('/');
  std::string parent_path =
      (last_slash == std::string::npos)
          ? "."
          : (last_slash == 0 ? "/" : path.substr(0, last_slash));
  std::string file_name =
      (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

  uint32_t parent_inode = resolve_path(parent_path);
  if (parent_inode == 0) {
    std::cout << "[!] Diretório pai não encontrado.\n";
    return;
  }

  // Aloca um novo inode e adiciona a entrada de diretório via Ext4FS
  uint32_t new_inode = fs.create_file_entry(
      parent_inode, file_name, 1); // 1 = arquivo regular no ext4_dir_entry_2
  if (new_inode == 0) {
    std::cout << "[!] Erro ao criar arquivo no sistema de arquivos.\n";
    return;
  }

  std::cout << "[*] Arquivo '" << file_name << "' criado com sucesso (Inode "
            << new_inode << ").\n";
}

// mkdir: cria um diretório vazio
void Ext4Shell::mkdir(const std::string &path) {
  if (path.empty())
    return;

  if (resolve_path(path) != 0) {
    std::cout << "[!] Diretório ou arquivo já existe.\n";
    return;
  }

  // Separa o nome do diretório do caminho do diretório pai
  size_t last_slash = path.rfind('/');
  std::string parent_path =
      (last_slash == std::string::npos)
          ? "."
          : (last_slash == 0 ? "/" : path.substr(0, last_slash));
  std::string dir_name =
      (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

  uint32_t parent_inode = resolve_path(parent_path);
  if (parent_inode == 0) {
    std::cout << "[!] Diretório pai não encontrado.\n";
    return;
  }

  // 2 = diretório no ext4_dir_entry_2. Deve inicializar com as entradas "." e
  // ".." internamente
  uint32_t new_inode = fs.create_file_entry(parent_inode, dir_name, 2);
  if (new_inode == 0) {
    std::cout << "[!] Erro ao criar diretório.\n";
    return;
  }

  std::cout << "[*] Diretório '" << dir_name << "' criado com sucesso (Inode "
            << new_inode << ").\n";
}

// rm: remove um arquivo regular
void Ext4Shell::rm(const std::string &path) {
  // Verifica se o arquivo existe e obtém seu inode
  uint32_t target_inode_num = resolve_path(path);
  if (target_inode_num == 0) {
    std::cout << "[!] Arquivo não encontrado.\n";
    return;
  }

  // Verifica se o inode é um diretório; se for, sugere usar rmdir
  inode target_inode;
  fs.read_inode(target_inode_num, target_inode);
  if (fs.inode_is_dir(target_inode)) {
    std::cout << "[!] '" << path << "' é um diretório. Use rmdir.\n";
    return;
  }

  // Separa o nome do arquivo do caminho do diretório pai
  size_t last_slash = path.rfind('/');
  std::string parent_path =
      (last_slash == std::string::npos)
          ? "."
          : (last_slash == 0 ? "/" : path.substr(0, last_slash));
  std::string file_name =
      (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
  uint32_t parent_inode = resolve_path(parent_path);

  // fs.unlink_entry deve remover a entrada do diretório pai, liberar os blocos
  // e o inode
  if (!fs.unlink_entry(parent_inode, file_name, target_inode_num)) {
    std::cout << "[!] Erro ao remover o arquivo.\n";
    return;
  }

  std::cout << "[*] Arquivo '" << file_name << "' removido.\n";
}

// rmdir: remove um diretório se estiver vazio
void Ext4Shell::rmdir(const std::string &path) {
  // Verifica se o diretório existe e obtém seu inode
  uint32_t target_inode_num = resolve_path(path);
  if (target_inode_num == 0) {
    std::cout << "[!] Diretório não encontrado.\n";
    return;
  }

  // Não permite remover o diretório corrente ou a raiz
  if (target_inode_num == curr_inode || target_inode_num == 2) {
    std::cout << "[!] Não é possível remover o diretório corrente ou a raiz.\n";
    return;
  }

  // Verifica se o inode é realmente um diretório
  inode target_inode;
  fs.read_inode(target_inode_num, target_inode);
  if (!fs.inode_is_dir(target_inode)) {
    std::cout << "[!] '" << path << "' não é um diretório.\n";
    return;
  }

  // Verifica se o diretório está vazio (só possui "." e "..")
  if (!fs.is_dir_empty(target_inode_num)) {
    std::cout << "[!] Diretório não está vazio.\n";
    return;
  }

  // Separa o nome do diretório do caminho do diretório pai
  size_t last_slash = path.rfind('/');
  std::string parent_path =
      (last_slash == std::string::npos)
          ? "."
          : (last_slash == 0 ? "/" : path.substr(0, last_slash));
  std::string dir_name =
      (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
  uint32_t parent_inode = resolve_path(parent_path);

  // fs.unlink deve remover a entrada do diretório pai, liberar o inode e
  // atualizar o bitmap
  if (!fs.unlink_entry(parent_inode, dir_name, target_inode_num)) {
    std::cout << "[!] Erro ao remover o diretório.\n";
    return;
  }

  std::cout << "[*] Diretório '" << dir_name << "' removido.\n";
}

// rename: altera o nome de um arquivo/diretório
void Ext4Shell::ext4_rename(const std::string &old_path,
                            const std::string &new_name) {
  // Verifica se o arquivo/diretório de origem existe
  uint32_t target_inode_num = resolve_path(old_path);
  if (target_inode_num == 0) {
    std::cout << "[!] Arquivo/Diretório de origem não encontrado.\n";
    return;
  }

  // Verifica se o novo nome é válido (não deve conter barras)
  if (new_name.find('/') != std::string::npos) {
    std::cout << "[!] O novo nome não deve conter barras (caminhos). O rename "
                 "apenas renomeia localmente.\n";
    return;
  }

  // Separa o nome do arquivo/diretório do caminho do diretório pai
  size_t last_slash = old_path.rfind('/');
  std::string parent_path =
      (last_slash == std::string::npos)
          ? "."
          : (last_slash == 0 ? "/" : old_path.substr(0, last_slash));
  std::string old_name = (last_slash == std::string::npos)
                             ? old_path
                             : old_path.substr(last_slash + 1);
  uint32_t parent_inode = resolve_path(parent_path);

  // fs.rename_entry busca a entrada antiga no diretório pai e atualiza a string
  // de texto e name_len
  if (!fs.rename_entry(parent_inode, old_name, new_name)) {
    std::cout << "[!] Erro ao renomear a entrada no diretório.\n";
    return;
  }

  std::cout << "[*] Renomeado com sucesso: " << old_name << " -> " << new_name
            << "\n";
}

// ext4_import: importa um arquivo do SO real para o arquivo de imagem EXT4
void Ext4Shell::ext4_import(const std::string &source,
                            const std::string &target) {
  // 1. Abre o arquivo local do sistema operacional
  std::ifstream in(source, std::ios::binary | std::ios::ate);
  if (!in.is_open()) {
    std::cout << "[!] Não foi possível abrir o arquivo de origem no SO: "
              << source << "\n";
    return;
  }

  // Obtém o tamanho do arquivo e lê todo o conteúdo em um buffer
  std::streamsize size = in.tellg();
  in.seekg(0, std::ios::beg);

  std::vector<char> buffer(static_cast<size_t>(size));
  if (!in.read(buffer.data(), size)) {
    std::cout << "[!] Erro ao ler o conteúdo do arquivo no SO.\n";
    return;
  }

  // 2. Garante/Cria a entrada destino na imagem EXT4 usando a lógica semelhante
  // ao touch
  uint32_t target_inode_num = resolve_path(target);

  if (target_inode_num == 0) {
    size_t last_slash = target.rfind('/');
    std::string parent_path =
        (last_slash == std::string::npos)
            ? "."
            : (last_slash == 0 ? "/" : target.substr(0, last_slash));
    std::string file_name = (last_slash == std::string::npos)
                                ? target
                                : target.substr(last_slash + 1);

    uint32_t parent_inode = resolve_path(parent_path);
    if (parent_inode == 0) {
      std::cout << "[!] Diretório pai no EXT4 não existe.\n";
      return;
    }

    // Cria o arquivo se ele não existir
    target_inode_num = fs.create_file_entry(parent_inode, file_name, 1);
    if (target_inode_num == 0) {
      std::cout << "[!] Falha ao criar arquivo de destino na imagem.\n";
      return;
    }
  }

  // 3. Reutiliza o método fs.write_to_file
  // Ele vai iterar e salvar o buffer nos blocos lógicos usando a árvore de
  // extents!
  inode file_inode;
  fs.read_inode(target_inode_num, file_inode);

  // Divide o buffer em blocos se necessário ou passa para a rotina
  // write_to_file
  uint32_t block_size = fs.get_block_size();
  uint32_t total_logical_blocks = (buffer.size() + block_size - 1) / block_size;
  bool success = true;

  // Iteração sobre cada bloco lógico e escrita no EXT4
  for (uint32_t i = 0; i < total_logical_blocks; ++i) {
    size_t offset = i * block_size;
    size_t chunk_size =
        std::min(static_cast<size_t>(block_size), buffer.size() - offset);
    std::vector<char> sub_buffer(buffer.begin() + offset,
                                 buffer.begin() + offset + chunk_size);

    if (!fs.write_to_file(target_inode_num, file_inode, i, sub_buffer)) {
      success = false;
      break;
    }
    // Recarrega o inode atualizado para o próximo bloco ver o novo
    // tamanho/árvore
    fs.read_inode(target_inode_num, file_inode);
  }

  if (success) {
    std::cout << "[*] Importado com sucesso: " << source << " -> " << target
              << " (" << buffer.size() << " bytes)\n";
  } else {
    std::cout << "[!] Ocorreu um erro durante a escrita dos blocos no EXT4.\n";
  }
}
