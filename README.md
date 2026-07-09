# Projeto - Implementação de Sistema de Arquivos EXT4

Estudantes: André Felipe Wonsik Alves, Eber Felipe Louback, Erick Molina Gehring e Gustavo Martins França

O objetivo deste projeto é implementar um shell interativo para a maipulação de uma imagem EXT4, permitindo a leitura e escrita de arquivos na imagem.

## Compilar e Executar

- ```$ make build```: compila todos os arquivos
cpp em um binário executável; salvando-o no diretório ```build```.

- ```$ make run```: executa o binário executável. Por padrão, será considerada a imagem ```resources/myext4image4k.img```.

- ```$ make all```: clean + build + run

-  ```$ make run img=<path_to_image>``` ou ```$ make all img=<path_to_image>```: executar o programa carregando uma imagem específica.

## Comandos Implementados

- `$ commands`: lista todos os comandos disponíveis.
- `$ info`: exibe informações da imagem e do sistema de arquivos.
- `$ cat <file>`: exibe o conteúdo de um arquivo de texto.
- `$ attr <file | dir>`: exibe os atributos de um arquivo ou diretório.
- `$ cd <path>`: altera o diretório atual para o caminho informado.
- `$ ls`: lista os arquivos e diretórios do diretório atual.
- `$ testi <inode_number>`: verifica se um inode está livre ou ocupado.
- `$ testb <block_number>`: verifica se um bloco está livre ou ocupado.
- `$ export <source_path> <target_path>`: copia um arquivo da imagem para o sistema de arquivos da máquina.
- `$ pwd`: exibe o caminho absoluto do diretório atual.
- `$ touch <file>`: cria um arquivo vazio.
- `$ mkdir <dir>`: cria um diretório vazio.
- `$ rm <file>`: remove um arquivo.
- `$ rmdir <dir>`: remove um diretório vazio.
- `$ rename <file> <newfilename>`: renomeia um arquivo.
- `$ import <source_path> <target_path>`: copia um arquivo do sistema da máquina para a imagem.

## Dependências

- `libcrypto++-dev`
