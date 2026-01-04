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
			/* INODE::i_mode (octal, lower 12 bits reserved) */
			#define I_TYPE_MASK     0170000
			#define I_REGULAR       0100000
			#define I_BLOCK_SPECIAL 0060000
			#define I_DIRECTORY     0040000
			#define I_CHAR_SPECIAL  0020000
			#define I_NAMED_PIPE	0010000

		u32	i_size; // File size. If size < (nr_sects * SECTOR_SIZE) : the rest bytes are wasted and reserved for later writing

		//{} a slice
		u32	i_start_sect; /**< The first sector of the data (in the device) */
		u32	i_nr_sects; /**< How many sectors the file occupies (in the device) */

		u8	_unused[16]; /**< Stuff for alignment */
	} entity;
	// the following items are only present in memory
	int	i_dev;      // 
	int	i_cnt;		/**< How many procs share this inode  */
	int	i_num;		/**< inode nr. (S.N.) */
};

#define	INODE_SIZE sizeof(inode::inode_entity) // for storage
#define	MAX_FILENAME_LEN 12

struct dir_entry {
	int  inode_nr;		/**< inode nr. */
	char name[MAX_FILENAME_LEN];	/**< Filename */
};
#define	DIR_ENTRY_SIZE sizeof(dir_entry) // for storage


class OrangesFs : public FilesysTrait
{
public:
	byte* buffer_sector;
	unsigned partid;
	//
	OrangesFs(StorageTrait& s, byte* buffer, unsigned dev);
	virtual bool makefs(rostr vol_label, void* moreinfo = 0) override;
	virtual bool loadfs(void* moreinfo = 0) override;
	// `exinfo` is the return-inode
	// using: create_file("/hello", 0);
	virtual bool create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest = 0) override;
	// clear the i-node in inode_array[] although it is not really needed.
	// don't clear the data bytes so the file is recoverable.
	virtual bool remove(rostr pathname) override;// remove file/folder
	virtual void* search(rostr fullpath, void* moreinfo) override;// retback is the fd
	virtual bool proper(void* handler, stduint cmd, const void* moreinfo = 0) override;// set proper
	virtual bool enumer(void* dir_handler, _tocall_ft _fn) override;
	virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override;// read file
	virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override;// write file

	// // //
	// Read superblock from the given device then write it into a free superblock[] slot.
	void read_superblock();
	//
	super_block* get_superblock();
	// Get the inode ptr of given inode nr. A cache -- inode_table[] -- is maintained to make things faster. If the inode requested is already there, just return it. Otherwise the inode will be read from the disk.

	inode* get_inode(stduint inod_idx);
	// Decrease the reference nr of a slot in inode_table[]. When the nr reaches zero, it means the inode is not used any more and can be overwritten by a new inode.
	static void put_inode(inode* pinode);
	// Write the inode back to the disk. Commonly invoked as soon as the inode is changed.
	void sync_inode(inode* p);
	// // //
protected:
	//
	inline byte* read_sector(stduint sect_nr) {
		bool state = storage->Read(sect_nr, buffer_sector);
		return state ? buffer_sector : 0;
	}
	inline byte* write_sector(stduint sect_nr) {
		bool state = storage->Write(sect_nr, buffer_sector);
		return state ? buffer_sector : 0;
	}

	// Generate a new i-node and write it to disk.
	// [para] inode_nr    I-node nr.
	// [para] start_sect  Start sector of the file pointed by the new i-node.
	inode* new_inode(stduint inode_nr, stduint start_sect);
	// Write a new entry into the directory.
	// [para] dir_inode  I-node of the directory. 
	// [para] inode_nr   I-node nr of the new file.
	// [para] filename   Filename of the new file. 
	void new_direntry(inode* dir_inode, int inode_nr, char* filename);

	//// ---- ---- SEC-MAP ---- ---- ////

	// Allocate a bit in inode-map
	// return inode_nr;
	stduint alloc_imap_bit();
	// Allocate a bit in sector-map.
	// @return  The 1st sector nr allocated.
	int alloc_smap_bit(int nr_sects_to_alloc);


public:

	byte _buf_part[sizeof(DiscPartition)];

public: //{BELOW} need(?) consider ring-calling

	// Return NULL if device is invalid or not-OFs
	static OrangesFs* (*IndexOFs)(unsigned device);
	// Return NULL if device is invalid
	static FilesysTrait* (*IndexFs)(unsigned device);

	
};
typedef OrangesFs FilesysMinix;

//// ---- ---- . ---- ---- ////

#define	NR_FILES	64
#define	NR_FILE_DESC	64	/* FIXME */
#define	NR_INODE	64	/* FIXME */
#define	NR_SUPER_BLOCK	8


#define	is_special(m)	((((m) & I_TYPE_MASK) == I_BLOCK_SPECIAL) ||	\
			 (((m) & I_TYPE_MASK) == I_CHAR_SPECIAL))

#define	NR_DEFAULT_FILE_SECTS	2048 /* 2048 * 512 = 1MB */


#endif
