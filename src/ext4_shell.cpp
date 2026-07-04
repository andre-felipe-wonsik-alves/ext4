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
  // commandsMap.emplace("info");
  // commandsMap.emplace("cat");
  // commandsMap.emplace("attr");
  // commandsMap.emplace("cd");
  // commandsMap.emplace("ls");
  // commandsMap.emplace("testi");
  // commandsMap.emplace("testb");
  // commandsMap.emplace("export");
  // commandsMap.emplace("pwd");
  // commandsMap.emplace("touch");
  // commandsMap.emplace("mkdir");
  // commandsMap.emplace("rm");
  // commandsMap.emplace("rmdir");
  // commandsMap.emplace("rename");
  // commandsMap.emplace("import");
}

// lê o input do usuário, separa em um vetor de strings e manda para o dispatch
void Ext4Shell::run() {
  std::cout << "\n\n  EXT4 FILESYSTEM SHELL  \n\n";
  std::cout << "Use 'commands' to list all usable commands\n\n";

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

void Ext4Shell::commands() {
  for (const auto &command : commandsMap) {
    std::cout << command.first << "\n";
  }
}
