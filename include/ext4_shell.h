#ifndef SHELL_H
#define SHELL_H

#include <string>
#include <vector>
#include "ext4_utils.h" 

class Ext4Shell {
private:
    Ext4FS fs;
    uint32_t curr_inode;
    std::string curr_path;

    void info();
    void cat(const std::string& path);
    void attr(const std::string& path);
    void cd(const std::string& path);
    void ls();
    void testi(uint32_t inode_num);
    void testb(uint32_t block_num);
    void ext4_export(const std::string& source, const std::string& target); // export é palavra reservada
    void pwd();

public:
    Ext4Shell();
    void run();
};

#endif
