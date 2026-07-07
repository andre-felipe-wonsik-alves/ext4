/**
 * Implementação dos utilitários de I/O de baixo nível para imagens ext4.
 */

#include <sstream>
#include "io_utils.h"

bool open_image(const std::string& path, std::fstream& image) {
    // Abre em modo binário com permissão de leitura e escrita
    image.open(path, std::fstream::in | std::fstream::out | std::fstream::binary);
   
    return image.is_open();
}

bool read_bytes(std::fstream& image, uint64_t offset, void* buf, size_t n) {
    // Posiciona o cursor de leitura no offset absoluto desejado
    image.seekg(offset, std::ios::beg);
    
    if (image.fail()) {
        return false;
    }

    // Lê n bytes para o buffer; cast necessário pois read() exige char*
    image.read(static_cast<char*>(buf), n);

    return image.good();
}

bool write_bytes(std::fstream& image, uint64_t offset, void* buf, size_t n) {
    // Posiciona o cursor de escrita no offset absoluto desejado
    image.seekp(offset, std::ios::beg);
    
    if (image.fail()) {
        return false;
    }

    // Escreve n bytes do buffer na imagem; cast necessário pois write() exige char*
    image.write(static_cast<char*>(buf), n);

    if (!image.good()) {
        return false;
    }

    // Garante que os dados saiam do buffer do runtime e cheguem ao SO
    image.flush();

    return image.good();
}

std::vector<std::string> split_tokens(const std::string& str, char delimiter) {
    std::vector<std::string> tokens; // Vetor para armazenar os tokens extraídos
    std::stringstream ss(str); // Cria um stream a partir da string de entrada
    std::string item; // String temporária para armazenar cada token extraído
    
    // getline com delimitador extrai cada token até encontrar o delimitador (por padrão '/')
    while (std::getline(ss, item, delimiter)) {
        // Ignora tokens vazios (ex: barras duplas "//" ou barra inicial "/")
        if (!item.empty()) {
            tokens.push_back(item);
        }
    }
    
    return tokens;
}
