#include "../include/mecocoa.hpp"

#if _MCCA == 0x8632 && _GUI_ENABLE && _GUI_FREETYPE

#include "freetype/x86/include/ft2build.h"
#include "freetype/x86/include/freetype/freetype.h"
#include "../include/filesys.hpp"

FT_Library ft_library;
uint8_t* font_buf = nullptr;
Mutex ft_lock;

// 512-byte block cache for FT_Stream (32KB total = 64 blocks).
// Each miss reads exactly one 512-byte sector (one storage->Read = one IPC to
// Task_Hdd_Serv). No bulk prefetch — avoids long readfl that can deadlock the
// message layer when it tries to issue 64 back-to-back sector reads.
#define FT_BLOCK_SIZE    512
#define FT_BLOCK_COUNT   64
#define FT_BLOCK_INVALID 0xFFFFFFFFu
static uint8_t*  ft_cache_buf = nullptr;        // malloc'd 32KB (64 blocks)
static uint32_t  ft_cache_tag[FT_BLOCK_COUNT];   // block_id per slot
static void*     ft_cache_file = nullptr;        // vfs_file* bound to cache
static uint64_t  ft_cache_file_size = 0;         // i_size for staleness check
static uint32_t  ft_cache_next = 0;              // round-robin replacement slot

// TTF table prefetch buffer.
// FT_Open_Face reads many small tables (head, maxp, hhea, cmap, loca, OS/2...)
// scattered across the file. Each table's first access triggers a FAT-chain
// walk in readfl; if the table sits at a large offset (e.g. post/name near EOF
// of a 10MB font), that walk is thousands of clusters and can deadlock the
// IPC layer. We prefetch the file head once (where most small tables live) so
// FT_Open_Face / FT_Set_Pixel_Sizes hit memory instead of disk.
#define FT_TABLE_PREFETCH_BYTES (128 * 1024)     // 128KB covers most small tables
static uint8_t*  ft_table_buf = nullptr;          // malloc'd prefetch buffer
static uint32_t  ft_table_valid = 0;              // bytes actually prefetched

__attribute__((section(".ext.freetype")))
static uint8 ascii_glyph_cache[128][16];

static bool ascii_glyph_cache_valid[128] = { false };
static bool loading_ascii = false;
static uint32 ascii_bytes_read = 0;

// CJK mono rendering glyph cache (16x16 pixels single-bit bitmap)
struct CJKCacheEntry {
	uint32 unicode_char;
	uint8 bits[16][2]; // 16 rows, 2 bytes per row (16 bits wide)
	bool valid;
};

#define CJK_CACHE_SIZE 512
__attribute__((section(".ext.freetype")))
static CJKCacheEntry cjk_cache[CJK_CACHE_SIZE];

class FreeTypeFontEngine : public uni::FontEngine {
private:
	FT_Face m_face;
	uni::Size2 m_cell_size;
public:
	FreeTypeFontEngine(FT_Face face, uni::Size2 size) : m_face(face), m_cell_size(size) {}
	virtual ~FreeTypeFontEngine() {
		FT_Done_Face(m_face);
	}

	virtual uni::Size2 GetCellSize() const override {
		return m_cell_size;
	}

	__attribute__((section(".ext.freetype.vcode")))
	virtual void DrawChar(
		Color* pixel_buffer,
		stduint pitch_pixels,
		stduint px_x,
		stduint px_y,
		uint32 unicode_char,
		Color fg,
		Color bg
	) const override {
		// Bypass dummy placeholder cell completely to prevent overwriting the CJK right-half glyph
		if (unicode_char == 0xFFFFFFFF) return;

		// Fill background grid (Double width for CJK characters)
		stduint bg_w = (unicode_char >= 0x100) ? m_cell_size.x * 2 : m_cell_size.x;
		for (stduint dy = 0; dy < m_cell_size.y; ++dy) {
			Color* row = pixel_buffer + (px_y + dy) * pitch_pixels + px_x;
			for (stduint dx = 0; dx < bg_w; ++dx) {
				row[dx] = bg;
			}
		}

		// Fast path: Render from in-memory ASCII glyph cache if available
		if (unicode_char < 128 && ascii_glyph_cache_valid[unicode_char]) {
			for (stduint dy = 0; dy < m_cell_size.y; ++dy) {
				Color* row = pixel_buffer + (px_y + dy) * pitch_pixels + px_x;
				uint8 bits = ascii_glyph_cache[unicode_char][dy];
				for (stduint dx = 0; dx < m_cell_size.x; ++dx) {
					if (bits & (1 << dx)) {
						row[dx] = fg;
					}
				}
			}
			return;
		}

		// Fast path: Render from CJK glyph cache if hit in memory
		if (unicode_char >= 128) {
			stduint hash = unicode_char % CJK_CACHE_SIZE;
			if (cjk_cache[hash].valid && cjk_cache[hash].unicode_char == unicode_char) {
				for (stduint dy = 0; dy < m_cell_size.y; ++dy) {
					int target_y = (int)px_y + dy;
					Color* row = pixel_buffer + target_y * pitch_pixels;
					uint16 row_bits = ((uint16)cjk_cache[hash].bits[dy][0] << 8) | cjk_cache[hash].bits[dy][1];
					for (stduint dx = 0; dx < bg_w; ++dx) {
						if (row_bits & (0x8000 >> dx)) {
							row[px_x + dx] = fg;
						}
					}
				}
				return;
			}
		}

		// Spaces and control characters have no glyph to render
		if (unicode_char <= 32) return;

		MutexLocal guard(&ft_lock);

		// Load character glyph
		const auto glyph_index = FT_Get_Char_Index(m_face, unicode_char);
		if (glyph_index == 0) return;

		int err = FT_Load_Glyph(m_face, glyph_index, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO);
		if (err) return;

		FT_Bitmap& bitmap = m_face->glyph->bitmap;
		int baseline = m_cell_size.y * 3 / 4;
		int glyph_left = m_face->glyph->bitmap_left;
		int glyph_top = baseline - m_face->glyph->bitmap_top;

		// Temporary buffer to hold the rasterized 16x16 CJK glyph
		uint8 temp_bits[16][2] = { 0 };

		stduint max_w = (unicode_char >= 0x100) ? m_cell_size.x * 2 : m_cell_size.x;
		for (int dy = 0; dy < (int)bitmap.rows; ++dy) {
			int target_y = glyph_top + dy;
			if (target_y < 0 || target_y >= 16) continue;

			unsigned char* q = &bitmap.buffer[bitmap.pitch * dy];

			for (int dx = 0; dx < (int)bitmap.width; ++dx) {
				int target_x = glyph_left + dx;
				if (target_x < 0 || target_x >= (int)max_w) continue;

				const bool b = q[dx >> 3] & (0x80 >> (dx & 0x7));
				if (b) {
					temp_bits[target_y][target_x >> 3] |= (0x80 >> (target_x & 0x7));
				}
			}
		}

		// Populate cache for CJK characters
		if (unicode_char >= 128) {
			stduint hash = unicode_char % CJK_CACHE_SIZE;
			cjk_cache[hash].valid = false; // Invalidate first to ensure safe concurrent reading
			for (int y = 0; y < 16; ++y) {
				cjk_cache[hash].bits[y][0] = temp_bits[y][0];
				cjk_cache[hash].bits[y][1] = temp_bits[y][1];
			}
			cjk_cache[hash].valid = true;
			cjk_cache[hash].unicode_char = unicode_char; // Write this last as a publication barrier
		}

		// Render from temp_bits to frame buffer
		for (stduint dy = 0; dy < m_cell_size.y; ++dy) {
			int target_y = (int)px_y + dy;
			Color* row = pixel_buffer + target_y * pitch_pixels;
			uint16 row_bits = ((uint16)temp_bits[dy][0] << 8) | temp_bits[dy][1];
			for (stduint dx = 0; dx < max_w; ++dx) {
				if (row_bits & (0x8000 >> dx)) {
					row[px_x + dx] = fg;
				}
			}
		}
	}

	__attribute__((section(".ext.freetype.vcode")))
	virtual Color GetPixel(
		uint32 unicode_char,
		stduint gx,
		stduint gy,
		Color fg,
		Color bg
	) const override {
		// Bypass dummy placeholder cell completely to prevent querying
		if (unicode_char == 0xFFFFFFFF) return bg;

		// Fast path: Query from in-memory ASCII glyph cache if available
		if (unicode_char < 128 && ascii_glyph_cache_valid[unicode_char]) {
			if (gx >= m_cell_size.x || gy >= m_cell_size.y) return bg;
			uint8 bits = ascii_glyph_cache[unicode_char][gy];
			return (bits & (1 << gx)) ? fg : bg;
		}

		// Fast path: Query from in-memory CJK glyph cache if available
		if (unicode_char >= 128) {
			stduint hash = unicode_char % CJK_CACHE_SIZE;
			if (cjk_cache[hash].valid && cjk_cache[hash].unicode_char == unicode_char) {
				stduint max_w = (unicode_char >= 0x100) ? m_cell_size.x * 2 : m_cell_size.x;
				if (gx >= max_w || gy >= m_cell_size.y) return bg;
				uint8 byte_val = cjk_cache[hash].bits[gy][gx >> 3];
				return (byte_val & (0x80 >> (gx & 0x7))) ? fg : bg;
			}
		}

		// Spaces and control characters have no glyph to query
		if (unicode_char <= 32) return bg;

		MutexLocal guard(&ft_lock);

		const auto glyph_index = FT_Get_Char_Index(m_face, unicode_char);
		if (glyph_index == 0) return bg;

		int err = FT_Load_Glyph(m_face, glyph_index, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO);
		if (err) return bg;

		FT_Bitmap& bitmap = m_face->glyph->bitmap;
		int baseline = m_cell_size.y * 3 / 4;
		int glyph_left = m_face->glyph->bitmap_left;
		int glyph_top = baseline - m_face->glyph->bitmap_top;

		int rel_x = (int)gx - glyph_left;
		int rel_y = (int)gy - glyph_top;

		if (rel_x < 0 || rel_x >= (int)bitmap.width || rel_y < 0 || rel_y >= (int)bitmap.rows) {
			return bg;
		}

		unsigned char* q = &bitmap.buffer[bitmap.pitch * rel_y];
		const bool b = q[rel_x >> 3] & (0x80 >> (rel_x & 0x7));
		return b ? fg : bg;
	}
};

uni::FontEngine* global_ft_engine = nullptr;

extern "C" stdsint sysc_OPEN(stduint usr_filepath, stduint flag);
extern "C" stdsint sysc_CLOS(stduint fd);

static unsigned long My_FT_Stream_Read(
	FT_Stream stream,
	unsigned long offset,
	unsigned char* buffer,
	unsigned long count
) {
	vfs_file* file = (vfs_file*)stream->descriptor.pointer;
	if (count == 0) return 0;
	if (!file || !file->f_inode || !file->f_inode->i_sb || !file->f_inode->i_sb->fs) {
		return 0;
	}

	// Invalidate cache if file identity or size changed (e.g. reopened, or file mutated)
	if (ft_cache_file != (void*)file || ft_cache_file_size != file->f_inode->i_size) {
		ft_cache_file = (void*)file;
		ft_cache_file_size = file->f_inode->i_size;
		for (int i = 0; i < FT_BLOCK_COUNT; i++) ft_cache_tag[i] = FT_BLOCK_INVALID;
	}

	// Fast path: TTF table prefetch buffer (covers FT_Open_Face / FT_Set_Pixel_Sizes reads)
	if (ft_table_valid > 0 &&
		offset + count <= ft_table_valid) {
		MemCopyN(buffer, ft_table_buf + offset, count);
		return count;
	}

	// Call readfl directly on the filesystem trait, bypassing Filesys::Read.
	// Filesys::Read holds vfs_lock (a spinlock that disables interrupts).
	// Disabling interrupts prevents the scheduler from switching to Task_Hdd_Serv,
	// which FAT::readfl depends on for disk I/O — causing a permanent hang.
	FilesysTrait* fs = file->f_inode->i_sb->fs;

	unsigned long total_read = 0;
	unsigned long pos = offset;
	unsigned long remaining = count;

	// Serve request block-by-block (512 bytes each). Each miss triggers exactly
	// one readfl of 512 bytes (= one sector I/O), avoiding the deadlock risk of
	// issuing 64 back-to-back sector reads in a single 32KB readfl call.
	while (remaining > 0) {
		uint32_t block_id = pos / FT_BLOCK_SIZE;
		uint32_t block_off = pos % FT_BLOCK_SIZE;
		uint32_t can_read = FT_BLOCK_SIZE - block_off;
		if (can_read > remaining) can_read = (uint32_t)remaining;

		// Linear search for block in cache
		int slot = -1;
		for (int i = 0; i < FT_BLOCK_COUNT; i++) {
			if (ft_cache_tag[i] == block_id) { slot = i; break; }
		}

		if (slot < 0) {
			// Miss: round-robin replacement
			slot = (int)ft_cache_next;
			ft_cache_next = (ft_cache_next + 1) % FT_BLOCK_COUNT;

			// Read one aligned 512-byte sector (single storage->Read IPC)
			stduint bytes = fs->readfl(file->f_inode->internal_handler,
				Slice{ (stduint)(block_id * FT_BLOCK_SIZE), (stduint)FT_BLOCK_SIZE },
				ft_cache_buf + slot * FT_BLOCK_SIZE);
			if (bytes == 0) return total_read;
			ft_cache_tag[slot] = block_id;
		}

		MemCopyN(buffer + total_read,
			ft_cache_buf + slot * FT_BLOCK_SIZE + block_off, can_read);

		total_read += can_read;
		remaining -= can_read;
		pos += can_read;
	}

	if (loading_ascii) {
		static uint32 last_logged_kb = 0;
		ascii_bytes_read += total_read;
		if (ascii_bytes_read / 4096 > last_logged_kb) {
			last_logged_kb = ascii_bytes_read / 4096;
			ploginfo("InitializeFont: Loaded %u KB of ASCII font data...", last_logged_kb * 4);
		}
	}
	return total_read;
}

static void My_FT_Stream_Close(FT_Stream stream) {
	vfs_file* file = (vfs_file*)stream->descriptor.pointer;
	if (file) {
		if (Taskman::CurrentTID() == Task_FileSys) {
			Filesys::Close(file);
		} else {
			ProcessBlock* pb = ProcessBlock::Acquire(Taskman::CurrentTID());
			int fd = -1;
			if (pb) {
				auto files = pb->fileman.Lock();
				for (stduint i = 0; i < files->pfiles.Count(); ++i) {
					if (files->pfiles[i] && files->pfiles[i]->vfile == file) {
						fd = i;
						break;
					}
				}
				ProcessBlock::Release(pb);
			}
			if (fd != -1) {
				sysc_CLOS(fd);
			} else {
				Filesys::Close(file);
			}
		}
		stream->descriptor.pointer = nullptr; // Prevent double close
	}
	// Release prefetch buffer when the FT stream is closed (FT_Done_Face).
	// After this point FreeType no longer reads via My_FT_Stream_Read.
	if (ft_table_buf) {
		free(ft_table_buf);
		ft_table_buf = nullptr;
		ft_table_valid = 0;
	}
}

__attribute__((section(".ext.freetype.code")))
bool InitializeFont() {
	ploginfo("InitializeFont: [Start] (Stream-based Lazy Loading Mode)");

	// Allocate the 32KB block cache once (64 x 512-byte blocks).
	if (!ft_cache_buf) {
		ft_cache_buf = (uint8_t*)malloc(FT_BLOCK_SIZE * FT_BLOCK_COUNT);
		if (!ft_cache_buf) {
			plogerro("InitializeFont: failed to allocate %u-byte block cache",
				FT_BLOCK_SIZE * FT_BLOCK_COUNT);
			return false;
		}
		for (int i = 0; i < FT_BLOCK_COUNT; i++) ft_cache_tag[i] = FT_BLOCK_INVALID;
		ploginfo("InitializeFont: block cache %u bytes allocated", FT_BLOCK_SIZE * FT_BLOCK_COUNT);
	}

	if (int err = FT_Init_FreeType(&ft_library)) {
		plogerro("InitializeFont: FT_Init_FreeType failed: %d", err);
		return false;
	}
	ploginfo("InitializeFont: FT_Init_FreeType success, ft_library = %p", ft_library);

	vfs_file* file = nullptr;
	int open_err = 0;
	ploginfo("InitializeFont: Opening simsun.ttf...");
	if (Taskman::CurrentTID() == Task_FileSys) {
		open_err = Filesys::Open("/mnt/ide0.4/simsun.ttf", 0, &file);
	} else {
		int fd = sysc_OPEN((stduint)"/mnt/ide0.4/simsun.ttf", 0);
		if (fd >= 0) {
			ProcessBlock* pb = ProcessBlock::Acquire(Taskman::CurrentTID());
			if (pb) {
				auto files = pb->fileman.Lock();
				if (fd < (stdsint)files->pfiles.Count() && files->pfiles[fd]) {
					file = files->pfiles[fd]->vfile;
				}
			}
			ProcessBlock::Release(pb);
			open_err = (file != nullptr) ? 0 : -1;
		} else {
			open_err = fd;
		}
	}
	ploginfo("InitializeFont: Filesys::Open returned %d, file = %p", open_err, file);
	if (open_err != 0 || !file) {
		plogerro("InitializeFont: Failed to open simsun.ttf");
		FT_Done_FreeType(ft_library);
		return false;
	}

	if (!file->f_inode) {
		plogerro("InitializeFont: file->f_inode is NULL!");
		if (Taskman::CurrentTID() == Task_FileSys) {
			Filesys::Close(file);
		} else {
			ProcessBlock* pb = ProcessBlock::Acquire(Taskman::CurrentTID());
			int fd = -1;
			if (pb) {
				auto files = pb->fileman.Lock();
				for (stduint i = 0; i < files->pfiles.Count(); ++i) {
					if (files->pfiles[i] && files->pfiles[i]->vfile == file) {
						fd = i;
						break;
					}
				}
				ProcessBlock::Release(pb);
			}
			if (fd != -1) {
				sysc_CLOS(fd);
			} else {
				Filesys::Close(file);
			}
		}
		FT_Done_FreeType(ft_library);
		return false;
	}
	stduint font_size = file->f_inode->i_size;
	ploginfo("InitializeFont: simsun.ttf size = %u bytes", font_size);
	if (font_size == 0) {
		plogerro("InitializeFont: Invalid simsun.ttf size");
		if (Taskman::CurrentTID() == Task_FileSys) {
			Filesys::Close(file);
		} else {
			ProcessBlock* pb = ProcessBlock::Acquire(Taskman::CurrentTID());
			int fd = -1;
			if (pb) {
				auto files = pb->fileman.Lock();
				for (stduint i = 0; i < files->pfiles.Count(); ++i) {
					if (files->pfiles[i] && files->pfiles[i]->vfile == file) {
						fd = i;
						break;
					}
				}
				ProcessBlock::Release(pb);
			}
			if (fd != -1) {
				sysc_CLOS(fd);
			} else {
				Filesys::Close(file);
			}
		}
		FT_Done_FreeType(ft_library);
		return false;
	}

	// Initialize the custom FT_StreamRec for true on-demand lazy loading
	static FT_StreamRec stream;
	MemSet(&stream, 0, sizeof(stream));
	stream.size = font_size;
	stream.descriptor.pointer = file;
	stream.read = My_FT_Stream_Read;
	stream.close = My_FT_Stream_Close;

	// Prefetch the file head into a flat buffer. FT_Open_Face / FT_Set_Pixel_Sizes
	// read many small tables (head, maxp, hhea, cmap, loca, OS/2...) that usually
	// live in the first 128KB. Without prefetch, each table's first seek walks the
	// FAT chain from start_cluster; tables near EOF of a 10MB font cause thousands
	// of cluster hops and can deadlock the IPC layer. Prefetch uses 512-byte sector
	// reads (single IPC each) so it cannot trigger the bulk-read deadlock.
	if (!ft_table_buf) {
		ft_table_buf = (uint8_t*)malloc(FT_TABLE_PREFETCH_BYTES);
		if (!ft_table_buf) {
			plogerro("InitializeFont: failed to allocate %u-byte prefetch buffer", FT_TABLE_PREFETCH_BYTES);
		} else {
			FilesysTrait* fs = file->f_inode->i_sb->fs;
			uint32_t to_prefetch = (font_size < FT_TABLE_PREFETCH_BYTES) ? (uint32_t)font_size : FT_TABLE_PREFETCH_BYTES;
			uint32_t off = 0;
			while (off < to_prefetch) {
				uint32_t chunk = (to_prefetch - off < FT_BLOCK_SIZE) ? (to_prefetch - off) : FT_BLOCK_SIZE;
				stduint bytes = fs->readfl(file->f_inode->internal_handler,
					Slice{ (stduint)off, (stduint)chunk }, ft_table_buf + off);
				if (bytes == 0) break;
				off += (uint32_t)bytes;
			}
			ft_table_valid = off;
			ploginfo("InitializeFont: prefetched %u bytes of TTF head (table area)", ft_table_valid);
		}
	}

	FT_Open_Args args = {};
	args.flags = FT_OPEN_STREAM;
	args.stream = &stream;

	FT_Face face;
	ploginfo("InitializeFont: Creating FT_Open_Face (Lazy Stream)...");
	int face_err = FT_Open_Face(ft_library, &args, 0, &face);
	ploginfo("InitializeFont: FT_Open_Face returned %d, face = %p", face_err, face);
	if (face_err == 0) {
		ploginfo("InitializeFont: Setting pixel sizes...");
		FT_Set_Pixel_Sizes(face, 16, 16);

		// Pre-populate ASCII glyph cache (32 to 127) while in safe Task_FileSys context
		loading_ascii = true;
		ascii_bytes_read = 0;
		for (uint32 c = 32; c < 128; ++c) {
			const auto glyph_index = FT_Get_Char_Index(face, c);
			if (glyph_index == 0) continue;
			if (FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO)) continue;

			FT_Bitmap& bitmap = face->glyph->bitmap;
			int baseline = 16 * 3 / 4;
			int glyph_left = face->glyph->bitmap_left;
			int glyph_top = baseline - face->glyph->bitmap_top;

			for (int dy = 0; dy < (int)bitmap.rows; ++dy) {
				int target_y = glyph_top + dy;
				if (target_y < 0 || target_y >= 16) continue;

				unsigned char* q = &bitmap.buffer[bitmap.pitch * dy];
				for (int dx = 0; dx < (int)bitmap.width; ++dx) {
					int target_x = glyph_left + dx;
					if (target_x < 0 || target_x >= 8) continue;

					const bool b = q[dx >> 3] & (0x80 >> (dx & 0x7));
					if (b) {
						ascii_glyph_cache[c][target_y] |= (1 << target_x);
					}
				}
			}
			ascii_glyph_cache_valid[c] = true;
		}
		loading_ascii = false;

		ploginfo("InitializeFont: Instantiating FreeTypeFontEngine...");
		global_ft_engine = new FreeTypeFontEngine(face, uni::Size2(8, 16));
		ploginfo("InitializeFont: FreeTypeFontEngine created at %p", global_ft_engine);
	} else {
		plogerro("InitializeFont: Failed to load font face via stream");
		if (ft_table_buf) { free(ft_table_buf); ft_table_buf = nullptr; ft_table_valid = 0; }
		if (Taskman::CurrentTID() == Task_FileSys) {
			Filesys::Close(file);
		} else {
			ProcessBlock* pb = ProcessBlock::Acquire(Taskman::CurrentTID());
			int fd = -1;
			if (pb) {
				auto files = pb->fileman.Lock();
				for (stduint i = 0; i < files->pfiles.Count(); ++i) {
					if (files->pfiles[i] && files->pfiles[i]->vfile == file) {
						fd = i;
						break;
					}
				}
				ProcessBlock::Release(pb);
			}
			if (fd != -1) {
				sysc_CLOS(fd);
			} else {
				Filesys::Close(file);
			}
		}
		FT_Done_FreeType(ft_library);
		return false;
	}

	ploginfo("InitializeFont: [Successful End]");
	return true;
}

#endif

#if !(_MCCA == 0x8632 && _GUI_ENABLE && _GUI_FREETYPE)
// Stubs to avoid link-time errors when FreeType is disabled, falling back to default 16x8 font
#include "../include/mecocoa.hpp"
#include <cpp/Device/_Video.hpp>

uni::FontEngine* global_ft_engine = nullptr;

bool InitializeFont() {
	return false;
}
#endif
