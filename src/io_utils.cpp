#include "io_utils.h"

bool open_image(const std::string& path, std::fstream& image) {
    image.open(path, std::fstream::in | std::fstream::out | std::fstream::binary);
   
    return image.is_open();
}

bool read_bytes(std::fstream& image, uint64_t offset, void* dest, size_t n) {
    image.seekg(offset, std::ios::beg);
    image.read(static_cast<char*>(dest), n);

    if (image.fail()) {
        image.clear();

        return false;
    }

    return true;
}
