#include "ext4_shell.h"
#include "io_utils.h"
#include <iostream>
#include <string>
#include <utility>

// criando hashmap com os comandos disponíveis e associando-os com a sua
// respectiva função
Ext4Shell::Ext4Shell() {
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
        attr(args[0]);
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
        } catch (const std::exception &e) {
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
        } catch (const std::exception &e) {
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

  commandsMap.emplace("import",
                      [this](const std::vector<std::string> &args) -> void {
                        if (args.size() < 2) {
                          std::cout << "[!] Origem e destino precisam ser "
                                       "informados para o comando export\n";
                          return;
                        }
                        ext4_import(args[0], args[1]);
                      });
}

// lê o input do usuário, separa em um vetor de strings e manda para o dispatch
void Ext4Shell::run() {
  std::cout << "\n\n  EXT4 FILESYSTEM SHELL  \n\n";
  std::cout
      << "[*] Use 'commands' para listar todos os comandos disponíveis\n\n";

  std::string input;

  while (true) {
    std::cout << " >> ";
    std::getline(std::cin, input);

    const char delimiter = ' ';
    std::vector<std::string> splitString = split_tokens(input, delimiter);

    dispatch(splitString);

    std::cout << "\n";
  }
}

// chama a função certa com base no comando requerido
// não faz nada se o vetor de comandos vier vazio
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

  std::vector<std::string> args(splitString.begin() + 1, splitString.end());

  it->second(args);
}

// lista todos os comandos disponíveis
void Ext4Shell::commands() {
  for (const auto &command : commandsMap) {
    std::cout << command.first << "\n";
  }
}

// exibe informações da imagem e do sistema de arquivos
void Ext4Shell::info() { std::cout << "[!] info() não implementado.\n"; }

// exibe o conteúdo de um arquivo no formato texto
void Ext4Shell::cat(const std::string &path) {
  if (path == "") {
    std::cout << "[!] caminho vazio\n";
  }
  std::cout << "[!] cat() não implementado.\n";
}

// mostra os atributos do arquivo/diretório
void Ext4Shell::attr(const std::string &path) {
  if (path == "") {
    std::cout << "[!] caminho vazio\n";
  }
  std::cout << "[!] attr() não implementado.\n";
}

// navega no sistema de arquivos
void Ext4Shell::cd(const std::string &path) {
  if (path == "") {
    std::cout << "[!] caminho vazio\n";
  }
  std::cout << "[!] cd() não implementado.\n";
}

// lista tudo no diretório
void Ext4Shell::ls() { std::cout << "[!] ls() não implementado.\n"; }

// testa se o inode está livre/ocupado
void Ext4Shell::testi(uint32_t inode_num) {
  // Exibe o número capturado para validar se o parsing funcionou
  std::cout << "[!] testi() não implementado. (Inode: " << inode_num << ")\n";
}

// testa se o bloco está livre/copuado
void Ext4Shell::testb(uint32_t block_num) {
  // Exibe o número capturado para validar se o parsing funcionou
  std::cout << "[!] testb() não implementado. (Bloco: " << block_num << ")\n";
}

// copia arquivo da imagem (source_path) para destino (target_path) no
// sistema operacional da máquina
void Ext4Shell::ext4_export(const std::string &source,
                            const std::string &target) {
  if (source.empty() || target.empty()) {
    std::cout << "[!] Origem ou destino vazios\n";
    return;
  }
  std::cout << "[!] ext4_export() não implementado. (Origem: " << source
            << " -> Destino: " << target << ")\n";
}

// exibe o caminho absoluto de diretório atual
void Ext4Shell::pwd() { std::cout << "[!] pwd() não implementado.\n"; }

// importa um arquivo do SO da m;aquina para a imagem
void Ext4Shell::ext4_import(const std::string &source,
                            const std::string &target) {
  if (source.empty() || target.empty()) {
    std::cout << "[!] Origem ou destino vazios\n";
    return;
  }
  std::cout << "[!] ext4_import() não implementado. (Origem: " << source
            << " -> Destino: " << target << ")\n";
}
