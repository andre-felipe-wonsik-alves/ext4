#include <sstream>
#include "io_utils.h"

bool open_image(const std::string& path, std::fstream& image) {
    image.open(path, std::fstream::in | std::fstream::out | std::fstream::binary);
   
    return image.is_open();
}

bool read_bytes(std::fstream& image, uint64_t offset, void* buf, size_t n) {
    image.seekg(offset, std::ios::beg);
    
    if (image.fail()) {
        return false;
    }

    image.read(static_cast<char*>(buf), n);

    return image.good();
}

bool write_bytes(std::fstream& image, uint64_t offset, void* buf, size_t n) {
    image.seekg(offset, std::ios::beg);
    
    if (image.fail()) {
        return false;
    }

    image.write(static_cast<char*>(buf), n);

    return true;
}

std::vector<std::string> split_tokens(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string item;
    
    while (std::getline(ss, item, delimiter)) {
        if (!item.empty()) {
            tokens.push_back(item);
        }
    }
    
    return tokens;
}
