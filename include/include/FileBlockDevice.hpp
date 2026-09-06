#ifndef FILE_BLOCK_DEVICE_HPP_
#define FILE_BLOCK_DEVICE_HPP_

#include <stdio.h>
#include <cpp/trait/StorageTrait.hpp>

// Lightweight file-based StorageTrait for reading image files with zero full-buffer memory allocation
class FileBlockDevice : public StorageTrait {
private:
	FILE*   m_fp;
	stduint m_size;
	stduint m_cur_offset;
	stduint m_cache_block;
	byte*   m_cache_buf;

public:
	FileBlockDevice(FILE* fp, stduint size, stduint blockSize = 4096)
		: m_fp(fp), m_size(size), m_cur_offset(0),
		  m_cache_block((stduint)~0), m_cache_buf(nullptr) {
		Block_Size = blockSize ? blockSize : 4096;
		readable = true;
		writable = false;
		m_cache_buf = (byte*)malloc(Block_Size);
	}

	virtual ~FileBlockDevice() {
		if (m_cache_buf) {
			free(m_cache_buf);
			m_cache_buf = nullptr;
		}
	}

	virtual bool Read(stduint BlockIden, void* Dest, stduint Times = 1) override {
		if (BlockIden + Times > getUnits()) return false;
		stduint target_offset = BlockIden * Block_Size;
		if (target_offset != m_cur_offset) {
			if (fseek(m_fp, (long)target_offset, SEEK_SET) != 0) return false;
			m_cur_offset = target_offset;
		}
		stduint total_bytes = Times * Block_Size;
		size_t rd = fread(Dest, 1, total_bytes, m_fp);
		m_cur_offset += rd;
		return rd == total_bytes || (rd > 0 && BlockIden + Times == getUnits());
	}

	virtual bool Write(stduint BlockIden, const void* Sors, stduint Times = 1) override {
		return false;
	}

	virtual stduint getUnits() override {
		return (m_size + Block_Size - 1) / Block_Size;
	}

	virtual int operator[](uint64 bytid) override {
		if (bytid >= m_size || !m_cache_buf) return -1;
		stduint b_size = Block_Size ? Block_Size : 4096;
		stduint blk = (stduint)(bytid / b_size);
		stduint off = (stduint)(bytid % b_size);
		if (blk != m_cache_block) {
			if (!Read(blk, m_cache_buf, 1)) return -1;
			m_cache_block = blk;
		}
		return m_cache_buf[off];
	}
};

#endif
