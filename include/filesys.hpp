// minix-like
#ifndef FILESYS_HPP_
#define FILESYS_HPP_
#define _STYLE_RUST
#include "cpp/trait/StorageTrait.hpp"
#include "cpp/trait/FilesysTrait.hpp"

#include "fileman.hpp"

using namespace uni;

#define	MAGIC_V1	0x111// Oranges 1.0

// The 2nd sector of the FS
struct super_block {
	struct super_block_entity {
		u32 magic;
		u32 nr_inodes;  /**< How many inodes */
		u32 nr_sects;  /**< How many sectors */
		u32 nr_imap_sects;  /**< How many inode-map sectors */
		u32 nr_smap_sects;  /**< How many sector-map sectors */
		u32 n_1st_sect;  /**< Number of the 1st data sector */
		u32 nr_inode_sects;   /**< How many inode sectors */
		u32 root_inode;       /**< Inode nr of root directory */
		u32 inode_size;       /**< INODE_SIZE */
		u32 inode_isize_off;  /**< Offset of `struct inode::i_size' */
		u32 inode_start_off;  /**< Offset of `struct inode::i_start_sect' */
		u32 dir_ent_size;     /**< DIR_ENTRY_SIZE */
		u32 dir_ent_inode_off;/**< Offset of `struct dir_entry::inode_nr' */
		u32 dir_ent_fname_off;/**< Offset of `struct dir_entry::name' */
	} entity;
	// the following item(s) are only present in memory
	int	sb_dev; 	/**< the super block's home device: where it's from */
};

#define	SUPER_BLOCK_SIZE sizeof(super_block::super_block_entity) // for storage

struct inode {
	struct inode_entity {
		u32	i_mode; /**< Accsess mode. type of file */
		u32	i_size; // File size. If size < (nr_sects * SECTOR_SIZE) : the rest bytes are wasted and reserved for later writing

		//{} a slice
		u32	i_start_sect; /**< The first sector of the data (in the device) */
		u32	i_nr_sects; /**< How many sectors the file occupies (in the device) */

		u8	_unused[16]; /**< Stuff for alignment */
	} entity;
	// the following items are only present in memory
	int	i_dev;      // 0xAABB, e.g. 0x0100 for IDE1:0(maybe CD-ROM)// h[0] h[1~4][a~...], h[5], h[6~9][a~...]
	int	i_cnt;		/**< How many procs share this inode  */
	int	i_num;		/**< inode nr.  */
};

#define	INODE_SIZE sizeof(inode::inode_entity) // for storage
#define	MAX_FILENAME_LEN 12

struct dir_entry {
	int  inode_nr;		/**< inode nr. */
	char name[MAX_FILENAME_LEN];	/**< Filename */
};

#define	DIR_ENTRY_SIZE sizeof(dir_entry) // for storage

struct file_desc {
	int fd_mode; /**< R or W */
	int fd_pos; /**< Current position for R/W. */
	struct inode* fd_inode; /**< Ptr to the i-node */
};


class FilesysMinix : public FilesysTrait
{
	

public:
	FilesysMinix(StorageTrait& s) {
		storage = &s;
	}

	
};

struct FileDescriptor {
	i32 fd_mode;// RW
	u64 fd_pos;
	inode* fd_inode;
};


#define	INVALID_INODE		0
#define	ROOT_INODE		1

/* INODE::i_mode (octal, lower 12 bits reserved) */
#define I_TYPE_MASK     0170000
#define I_REGULAR       0100000
#define I_BLOCK_SPECIAL 0060000
#define I_DIRECTORY     0040000
#define I_CHAR_SPECIAL  0020000
#define I_NAMED_PIPE	0010000

#define	MAKE_DEV(a,b)		((a << 4) | b)

void make_filesys(Harddisk_PATA_Paged& ide, byte* buffer);

bool strip_path(char* filename, const char* pathname, struct inode** ppinode);

struct FilesysOrange {
	StorageTrait* storage;
	FilesysOrange(StorageTrait& s) {
		storage = &s;
	}
};

#endif
