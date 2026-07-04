#include "ext4_shell.h"
#include "ext4_utils.h"
#include "io_utils.h"
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: " << argv[0] << " <path_to_imdg>\n";
    return 1;
  }

  std::string img_path = argv[1];

  Ext4FS fs;

  if (!fs.init(img_path)) {
    std::cerr << "error on fs.init() in main()\n";
    return 1;
  }

  // criando shell e rodando
  Ext4Shell shell;
  shell.run();

  // DEBUGGING/TESTES
  // ler um inode
  // ler o conteúdo do inode (extents)
  // buscar inode por caminho
  // inode inode;

  // uint32_t test_inode_num = 12;

  // if (!fs.read_inode(test_inode_num, inode)) {
  //     std::cerr << "error on read_inode() in main()\n";
  //     return 1;
  // }

  // std::vector<char> i_content = fs.read_inode_content(inode);

  // if (i_content.empty()) {
  //     std::cerr << "error on read_inode_content() in main(): empty inoed\n";
  //     return 1;
  // }

  // std::cout << "Inode " << test_inode_num << ":\n";

  // uint32_t inode_test = fs.find_inode_by_path("./textos/test.txt", 12);
  // std::cout << inode_test << " is where the magic happens\n";

  return 0;
}
