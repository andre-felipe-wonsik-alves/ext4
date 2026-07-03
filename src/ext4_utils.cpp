#include <iomanip>
#include <iostream>
#include <string_view>
#include "ext4_utils.h"
#include "io_utils.h"

bool read_superblock(std::fstream& image, super_block& sb) {
    if (!read_bytes(image, 1024, &sb, sizeof(super_block))) {
        return false;
    }

    return true;
}

bool read_gdt(ext4_sb_info& ext4_info) {
    uint16_t desc_size = ext4_info.sb.s_desc_size;
    uint64_t offset = ext4_info.gdt_offset;

    for (uint64_t i = 0; i < ext4_info.num_groups; i++) {
        group_description gd;

        if (!read_bytes(ext4_info.image, offset, &gd, sizeof(group_description))) {
            return false;
        }
        
        ext4_info.gdt.push_back(gd);

        offset += desc_size;
    }

    return true;
}

void print_superblock(const ext4_sb_info& ext4_info) {
    super_block sb = ext4_info.sb;

    const int w = 30;
    std::cout << std::left << std::setfill(' ');

    std::cout << std::setw(w) << "s_inodes_count:" << sb.s_inodes_count << "\n";
    std::cout << std::setw(w) << "s_blocks_count_lo:" << sb.s_blocks_count_lo << "\n";
    std::cout << std::setw(w) << "s_r_blocks_count_lo:" << sb.s_r_blocks_count_lo << "\n";
    std::cout << std::setw(w) << "s_free_blocks_count_lo:" << sb.s_free_blocks_count_lo << "\n";
    std::cout << std::setw(w) << "s_free_inodes_count:" << sb.s_free_inodes_count << "\n";
    std::cout << std::setw(w) << "s_first_data_block:" << sb.s_first_data_block << "\n";
    std::cout << std::setw(w) << "s_log_block_size:" << sb.s_log_block_size << "\n";
    std::cout << std::setw(w) << "s_log_cluster_size:" << sb.s_log_cluster_size << "\n";
    std::cout << std::setw(w) << "s_blocks_per_group:" << sb.s_blocks_per_group << "\n";
    std::cout << std::setw(w) << "s_clusters_per_group:" << sb.s_clusters_per_group << "\n";
    std::cout << std::setw(w) << "s_inodes_per_group:" << sb.s_inodes_per_group << "\n";
    std::cout << std::setw(w) << "s_mtime:" << sb.s_mtime << "\n";
    std::cout << std::setw(w) << "s_wtime:" << sb.s_wtime << "\n";
    std::cout << std::setw(w) << "s_mnt_count:" << sb.s_mnt_count << "\n";
    std::cout << std::setw(w) << "s_max_mnt_count:" << sb.s_max_mnt_count << "\n";
    std::cout << std::setw(w) << "s_magic:" << "0x" << std::hex << sb.s_magic << std::dec << "\n";
    std::cout << std::setw(w) << "s_state:" << "0x" << std::hex << sb.s_state << std::dec << "\n";
    std::cout << std::setw(w) << "s_errors:" << sb.s_errors << "\n";
    std::cout << std::setw(w) << "s_minor_rev_level:" << sb.s_minor_rev_level << "\n";
    std::cout << std::setw(w) << "s_lastcheck:" << sb.s_lastcheck << "\n";
    std::cout << std::setw(w) << "s_checkinterval:" << sb.s_checkinterval << "\n";
    std::cout << std::setw(w) << "s_creator_os:" << sb.s_creator_os << "\n";
    std::cout << std::setw(w) << "s_rev_level:" << sb.s_rev_level << "\n";
    std::cout << std::setw(w) << "s_def_resuid:" << sb.s_def_resuid << "\n";
    std::cout << std::setw(w) << "s_def_resgid:" << sb.s_def_resgid << "\n";

    std::cout << std::setw(w) << "s_first_ino:" << sb.s_first_ino << "\n";
    std::cout << std::setw(w) << "s_inode_size:" << sb.s_inode_size << "\n";
    std::cout << std::setw(w) << "s_block_group_nr:" << sb.s_block_group_nr << "\n";
    std::cout << std::setw(w) << "s_feature_compat:" << "0x" << std::hex << sb.s_feature_compat << std::dec << "\n";
    std::cout << std::setw(w) << "s_feature_incompat:" << "0x" << std::hex << sb.s_feature_incompat << std::dec << "\n";
    std::cout << std::setw(w) << "s_feature_ro_compat:" << "0x" << std::hex << sb.s_feature_ro_compat << std::dec << "\n";
    std::cout << std::setw(w) << "s_uuid:" << std::string_view(reinterpret_cast<const char*>(sb.s_uuid), 16) << "\n";
    std::cout << std::setw(w) << "s_volume_name:" << std::string_view(sb.s_volume_name, 16) << "\n";
    std::cout << std::setw(w) << "s_last_mounted:" << std::string_view(sb.s_last_mounted, 64) << "\n";
    std::cout << std::setw(w) << "s_algorithm_usage_bitmap:" << "0x" << std::hex << sb.s_algorithm_usage_bitmap << std::dec << "\n";

    std::cout << std::setw(w) << "s_prealloc_blocks:" << (int)sb.s_prealloc_blocks << "\n";
    std::cout << std::setw(w) << "s_prealloc_dir_blocks:" << (int)sb.s_prealloc_dir_blocks << "\n";
    std::cout << std::setw(w) << "s_reserved_gdt_blocks:" << sb.s_reserved_gdt_blocks << "\n";

    std::cout << std::setw(w) << "s_journal_uuid:" << std::string_view(reinterpret_cast<const char*>(sb.s_journal_uuid), 16) << "\n";
    std::cout << std::setw(w) << "s_journal_inum:" << sb.s_journal_inum << "\n";
    std::cout << std::setw(w) << "s_journal_dev:" << sb.s_journal_dev << "\n";
    std::cout << std::setw(w) << "s_last_orphan:" << sb.s_last_orphan << "\n";
    std::cout << std::setw(w) << "s_hash_seed:" << "0x" << std::hex << sb.s_hash_seed[0] << " 0x" << sb.s_hash_seed[1] << " 0x" << sb.s_hash_seed[2] << " 0x" << sb.s_hash_seed[3] << std::dec << "\n";
    std::cout << std::setw(w) << "s_def_hash_version:" << (int)sb.s_def_hash_version << "\n";
    std::cout << std::setw(w) << "s_jnl_backup_type:" << (int)sb.s_jnl_backup_type << "\n";
    std::cout << std::setw(w) << "s_desc_size:" << sb.s_desc_size << "\n";
    std::cout << std::setw(w) << "s_default_mount_opts:" << "0x" << std::hex << sb.s_default_mount_opts << std::dec << "\n";
    std::cout << std::setw(w) << "s_first_meta_bg:" << sb.s_first_meta_bg << "\n";
    std::cout << std::setw(w) << "s_mkfs_time:" << sb.s_mkfs_time << "\n";
    std::cout << std::setw(w) << "s_jnl_blocks[0]:" << sb.s_jnl_blocks[0] << "\n";

    std::cout << std::setw(w) << "s_blocks_count_hi:" << sb.s_blocks_count_hi << "\n";
    std::cout << std::setw(w) << "s_r_blocks_count_hi:" << sb.s_r_blocks_count_hi << "\n";
    std::cout << std::setw(w) << "s_free_blocks_count_hi:" << sb.s_free_blocks_count_hi << "\n";
    std::cout << std::setw(w) << "s_min_extra_isize:" << sb.s_min_extra_isize << "\n";
    std::cout << std::setw(w) << "s_want_extra_isize:" << sb.s_want_extra_isize << "\n";
    std::cout << std::setw(w) << "s_flags:" << "0x" << std::hex << sb.s_flags << std::dec << "\n";
    std::cout << std::setw(w) << "s_raid_stride:" << sb.s_raid_stride << "\n";
    std::cout << std::setw(w) << "s_mmp_update_interval:" << sb.s_mmp_update_interval << "\n";
    std::cout << std::setw(w) << "s_mmp_block:" << sb.s_mmp_block << "\n";
    std::cout << std::setw(w) << "s_raid_stripe_width:" << sb.s_raid_stripe_width << "\n";
    std::cout << std::setw(w) << "s_log_groups_per_flex:" << (int)sb.s_log_groups_per_flex << "\n";
    std::cout << std::setw(w) << "s_checksum_type:" << (int)sb.s_checksum_type << "\n";
    std::cout << std::setw(w) << "s_encryption_level:" << (int)sb.s_encryption_level << "\n";
    std::cout << std::setw(w) << "s_reserved_pad:" << (int)sb.s_reserved_pad << "\n";
    std::cout << std::setw(w) << "s_kbytes_written:" << sb.s_kbytes_written << "\n";
    std::cout << std::setw(w) << "s_snapshot_inum:" << sb.s_snapshot_inum << "\n";
    std::cout << std::setw(w) << "s_snapshot_id:" << sb.s_snapshot_id << "\n";
    std::cout << std::setw(w) << "s_snapshot_r_blocks_count:" << sb.s_snapshot_r_blocks_count << "\n";
    std::cout << std::setw(w) << "s_snapshot_list:" << sb.s_snapshot_list << "\n";
    std::cout << std::setw(w) << "s_error_count:" << sb.s_error_count << "\n";
    std::cout << std::setw(w) << "s_first_error_time:" << sb.s_first_error_time << "\n";
    std::cout << std::setw(w) << "s_first_error_ino:" << sb.s_first_error_ino << "\n";
    std::cout << std::setw(w) << "s_first_error_block:" << sb.s_first_error_block << "\n";
    std::cout << std::setw(w) << "s_first_error_func:" << std::string_view(reinterpret_cast<const char*>(sb.s_first_error_func), 32) << "\n";
    std::cout << std::setw(w) << "s_first_error_line:" << sb.s_first_error_line << "\n";
    std::cout << std::setw(w) << "s_last_error_time:" << sb.s_last_error_time << "\n";
    std::cout << std::setw(w) << "s_last_error_ino:" << sb.s_last_error_ino << "\n";
    std::cout << std::setw(w) << "s_last_error_line:" << sb.s_last_error_line << "\n";
    std::cout << std::setw(w) << "s_last_error_block:" << sb.s_last_error_block << "\n";
    std::cout << std::setw(w) << "s_last_error_func:" << std::string_view(reinterpret_cast<const char*>(sb.s_last_error_func), 32) << "\n";
    std::cout << std::setw(w) << "s_mount_opts:" << std::string_view(reinterpret_cast<const char*>(sb.s_mount_opts), 64) << "\n";
    std::cout << std::setw(w) << "s_usr_quota_inum:" << sb.s_usr_quota_inum << "\n";
    std::cout << std::setw(w) << "s_grp_quota_inum:" << sb.s_grp_quota_inum << "\n";
    std::cout << std::setw(w) << "s_overhead_clusters:" << sb.s_overhead_clusters << "\n";
    std::cout << std::setw(w) << "s_backup_bgs:" << "0x" << std::hex << sb.s_backup_bgs[0] << " 0x" << sb.s_backup_bgs[1] << std::dec << "\n";
    std::cout << std::setw(w) << "s_encrypt_algos:" << (int)sb.s_encrypt_algos[0] << " " << (int)sb.s_encrypt_algos[1] << "\n";
    std::cout << std::setw(w) << "s_encrypt_pw_salt:" << std::string_view(reinterpret_cast<const char*>(sb.s_encrypt_pw_salt), 16) << "\n";
    std::cout << std::setw(w) << "s_lpf_ino:" << sb.s_lpf_ino << "\n";
    std::cout << std::setw(w) << "s_prj_quota_inum:" << sb.s_prj_quota_inum << "\n";
    std::cout << std::setw(w) << "s_checksum_seed:" << "0x" << std::hex << sb.s_checksum_seed << std::dec << "\n";
    std::cout << std::setw(w) << "s_wtime_hi:" << (int)sb.s_wtime_hi << "\n";
    std::cout << std::setw(w) << "s_mtime_hi:" << (int)sb.s_mtime_hi << "\n";
    std::cout << std::setw(w) << "s_mkfs_time_hi:" << (int)sb.s_mkfs_time_hi << "\n";
    std::cout << std::setw(w) << "s_lastcheck_hi:" << (int)sb.s_lastcheck_hi << "\n";
    std::cout << std::setw(w) << "s_first_error_time_hi:" << (int)sb.s_first_error_time_hi << "\n";
    std::cout << std::setw(w) << "s_last_error_time_hi:" << (int)sb.s_last_error_time_hi << "\n";
    std::cout << std::setw(w) << "s_first_error_errcode:" << (int)sb.s_first_error_errcode << "\n";
    std::cout << std::setw(w) << "s_last_error_errcode:" << (int)sb.s_last_error_errcode << "\n";
    std::cout << std::setw(w) << "s_encoding:" << sb.s_encoding << "\n";
    std::cout << std::setw(w) << "s_encoding_flags:" << sb.s_encoding_flags << "\n";
    std::cout << std::setw(w) << "s_orphan_file_inum:" << sb.s_orphan_file_inum << "\n";
    std::cout << std::setw(w) << "s_def_resuid_hi:" << sb.s_def_resuid_hi << "\n";
    std::cout << std::setw(w) << "s_def_resgid_hi:" << sb.s_def_resgid_hi << "\n";
    std::cout << std::setw(w) << "s_reserved[0]:" << sb.s_reserved[0] << "\n";
    std::cout << std::setw(w) << "s_checksum:" << "0x" << std::hex << sb.s_checksum << std::dec << "\n";
}

void print_gdt(const ext4_sb_info& ext4_info) {
    const int w = 30;
    std::cout << std::left << std::setfill(' ');

    for (size_t i = 0; i < ext4_info.gdt.size(); ++i) {
        const auto& gd = ext4_info.gdt[i]; 

        std::cout << std::setw(w) << "bg_block_bitmap_lo:" << gd.bg_block_bitmap_lo << "\n";
        std::cout << std::setw(w) << "bg_inode_bitmap_lo:" << gd.bg_inode_bitmap_lo << "\n";
        std::cout << std::setw(w) << "bg_inode_table_lo:" << gd.bg_inode_table_lo << "\n";
        std::cout << std::setw(w) << "bg_free_blocks_count_lo:" << gd.bg_free_blocks_count_lo << "\n";
        std::cout << std::setw(w) << "bg_free_inodes_count_lo:" << gd.bg_free_inodes_count_lo << "\n";
        std::cout << std::setw(w) << "bg_used_dirs_count_lo:" << gd.bg_used_dirs_count_lo << "\n";
        std::cout << "\n";
    }
}

ext4_sb_info init(std::fstream& image, const super_block& sb) {
    uint64_t block_size = get_block_size(sb);
    uint64_t blocks_count = get_blocks_count(sb);
    uint64_t num_groups = get_num_groups(sb);
    uint16_t desc_size = sb.s_desc_size;
    uint64_t gdt_offset = ((block_size == 1024) ? 2 : 1) * block_size;

    ext4_sb_info ext4_info {
        image,
        sb,
        {},
        block_size,
        blocks_count,
        num_groups,
        desc_size,
        gdt_offset
    };
    
    if (!read_gdt(ext4_info)) {
        std::cerr << "error on read_gdt() in init() \n";
    }

    return ext4_info;
}
