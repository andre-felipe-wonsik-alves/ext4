#include <iostream>
#include "ext4_utils.h"
#include "io_utils.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Use: " << argv[0] << " <ext4_image.img>\n";
        
        return 1;
    }

    std::string image_path = argv[1];
    std::fstream image;

    if (!open_image(image_path, image)) {
        std::cerr << "error on open_image() in main()";
        
        return 1;
    }

    super_block sb;
    
    if (!read_superblock(image, sb)) {
        std::cerr << "error on read_superblock() in main()";
        
        return 1;
    }

    ext4_sb_info ext4_info = init(image, sb);
    
    inode inode;
    
    if (!read_inode(ext4_info, 2, inode)) {
        std::cerr << "error on read_inode() in main()";
        return 1;
    }

    print_superblock(ext4_info);
    print_gdt(ext4_info);
    print_inode(inode, 2);
}
