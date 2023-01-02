#include "filestream.h"
#include <TFE_Archive/archive.h>
#include <TFE_System/system.h>
#include <cassert>
#include <cstring>
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

//Work buffers for handling special cases like std::string without allocating memory (beyond what the strings needs itself).
// TODO: This should be put in a shared place.
extern u32  s_workBufferU32[1024];		//4k buffer.
extern char s_workBufferChar[32768];	//32k buffer.


// Linux filesystems are case-sensitive; try to open the
// base directory and find a file that has the same name
// with different case.
// Caller MUST free the pointer returned by this function!
static char *try_to_find_file(const char *filename)
{
	char *fncopy, *dn, *fn;
	char *result = NULL;
	struct dirent *de;
	DIR *dir;
	int ol, dl, fl, nfl;

	// dirname() and basename() screw with the input buffer,
	// hence we need to make 2 copies.
	ol = strlen(filename);
	fncopy = (char *)malloc(ol * 2 + 2);
	if (!fncopy)
		return NULL;
	memset(fncopy, 0, ol + ol + 2);
	strncpy(fncopy, filename, ol);
	strncpy(fncopy + ol + 1, filename, ol);
	
	dn = dirname(fncopy);
	fn = basename(fncopy + ol +1);
	
	dl = strlen(dn);
	fl = strlen(fn);
	if ((dl < 1) || (fl < 1)) {
		free(fncopy);
		return NULL;
	}

	dir = opendir(dn);
	if (!dir) {
		free(fncopy);
		return NULL;
	}
	
	while (NULL != (de = readdir(dir)))
	{
		if (de->d_type != DT_REG)
			continue;

		nfl = strlen(de->d_name);
		if (nfl != fl)
			continue;
			
		if (0 != strncasecmp(de->d_name, fn, fl))
			continue;
			
		// found a match
		result = (char *)malloc(dl + 1 + nfl + 1);
		if (!result)
			break;
		memset(result, 0, dl + 1 + nfl + 1);
		sprintf(result, "%s/%s", dn, de->d_name);
		break;	// we're done.
	}
	free(fncopy);
	closedir(dir);
	return result;
}

FileStream::FileStream() : Stream()
{
	m_file = nullptr;
	m_archive = nullptr;
	m_mode = MODE_INVALID;
}

FileStream::~FileStream()
{
	close();
}

bool FileStream::exists(const char* filename)
{
	char *fn2 = try_to_find_file(filename);
	bool found = false;
	
	if (fn2 != NULL) {
		found = true;
		free(fn2);
	}

	return found;
}

bool FileStream::open(const char* filename, AccessMode mode)
{
	char *fn2;
	const char* modeStrings[] = { "rb", "wb", "rb+" };

	if (!filename || 1 > strlen(filename))
		return false;

	// try to open the given filename first; if that fails,
	// because the filename cannot be found, do a little
	// digging and find a matching filename with different
	// case.
	m_file = fopen(filename, modeStrings[mode]);
	m_mode = mode;
	if ((m_file == NULL) && (errno == ENOENT)) {
		// ok, try harder to find a filename with different case
		fn2 = try_to_find_file(filename);
		if (fn2 == NULL) {
			return false;
		}
		m_file = fopen(fn2, modeStrings[mode]);
		free(fn2);
	}

	return m_file != nullptr;
}

bool FileStream::open(const FilePath* filePath, AccessMode mode)
{
	if (filePath->archive)
	{
		// Note: this currently only supports reading.
		assert(mode == Stream::MODE_READ);

		if (filePath->index < 0) { return false; }
		m_mode = mode;
		m_file = nullptr;
		m_archive = filePath->archive;

		return filePath->archive->openFile(filePath->index);
	}
	else
	{
		return open(filePath->path, mode);
	}
}

void FileStream::close()
{
	if (m_file)
	{
		if (m_mode == MODE_WRITE || m_mode == MODE_READWRITE) 
		{ 
			fflush(m_file); 
		}

		fclose(m_file);
		m_file = nullptr;
	}
	else if (m_archive)
	{
		m_archive->closeFile();
		m_archive = nullptr;
	}
	m_mode = MODE_INVALID;
}

u32 FileStream::readContents(const char* filePath, void** output)
{
	assert(output);

	u32 size = 0;
	FileStream file;
	if (file.open(filePath, MODE_READ))
	{
		size = (u32)file.getSize();
		*output = realloc(*output, size + 1);
		file.readBuffer(*output, size);
		file.close();
	}
	return size;
}

u32 FileStream::readContents(const char* filePath, void* output, size_t size)
{
	assert(output);

	FileStream file;
	if (file.open(filePath, MODE_READ))
	{
		size_t fileSize = file.getSize();
		size = size <= fileSize ? size : fileSize;
		file.readBuffer(output, (u32)size);
		file.close();

		return u32(size);
	}
	return 0;
}

u32 FileStream::readContents(const FilePath* filePath, void** output)
{
	assert(output);

	u32 size = 0;
	FileStream file;
	if (file.open(filePath, MODE_READ))
	{
		size = (u32)file.getSize();
		*output = realloc(*output, size + 1);
		file.readBuffer(*output, size);
		file.close();
	}
	return size;
}

// It is assumed that output has already been allocated.
u32 FileStream::readContents(const FilePath* filePath, void* output, size_t size)
{
	FileStream file;
	if (file.open(filePath, MODE_READ))
	{
		size_t fileSize = file.getSize();
		size = size <= fileSize ? size : fileSize;
		file.readBuffer(output, (u32)size);
		file.close();

		return u32(size);
	}
	return 0;
}

//derived from Stream
bool FileStream::seek(s32 offset, Origin origin/*=ORIGIN_START*/)
{
	const s32 forigin[] = { SEEK_SET, SEEK_END, SEEK_CUR };
	if (m_file)
	{
		return fseek(m_file, offset, forigin[origin]) == 0;
	}
	else if (m_archive)
	{
		return m_archive->seekFile(offset, forigin[origin]);
	}
	return false;
}

size_t FileStream::getLoc()
{
	if (!m_file && !m_archive)
	{
		return 0;
	}
	if (m_file)
	{
		return ftell(m_file);
	}
	return m_archive->getLocInFile();
}

size_t FileStream::getSize()
{
	if (!m_file && !m_archive)
	{
		return 0;
	}

	size_t filesize = 0;
	if (m_file)
	{
		seek(0, FileStream::ORIGIN_END);
		filesize = getLoc();
		seek(0, FileStream::ORIGIN_START);
	}
	else
	{
		filesize = m_archive->getFileLength();
	}

	return filesize;
}

bool FileStream::isOpen() const
{
	return m_file!=nullptr || m_archive!=nullptr;
}

u32 FileStream::readBuffer(void* ptr, u32 size, u32 count)
{
	assert(m_mode == MODE_READ || m_mode == MODE_READWRITE);
	if (m_file)
	{
		// fread() returns the number of *elements* read, but we want the number of bytes read.
		return (u32)fread(ptr, size, count, m_file) * size;
	}
	else if (m_archive)
	{
		return (u32)m_archive->readFile(ptr, size * count);
	}
	return 0;
}

void FileStream::writeBuffer(const void* ptr, u32 size, u32 count)
{
	assert(m_mode == MODE_WRITE || m_mode == MODE_READWRITE);
	if (m_file)
	{
		fwrite(ptr, size, count, m_file);
	}
}

void FileStream::writeString(const char* fmt, ...)
{
	static char tmpStr[4096];
	assert(m_mode == MODE_WRITE || m_mode == MODE_READWRITE);

	if (m_file)
	{
		va_list arg;
		va_start(arg, fmt);
		vsprintf(tmpStr, fmt, arg);
		va_end(arg);

		const size_t len = strlen(tmpStr);
		fwrite(tmpStr, len, 1, m_file);
	}
}

void FileStream::flush()
{
	if (m_file)
	{
		fflush(m_file);
	}
}

void FileStream::readString(std::string* ptr, u32 count)
{
	assert(m_mode == MODE_READ || m_mode == MODE_READWRITE);
	assert(count <= 256);
	//first read the length.
	readBuffer(s_workBufferU32, sizeof(u32), count);

	//then read the string data.
	for (u32 s=0; s<count; s++)
	{
		assert(s_workBufferU32[s] <= 32768);
		readBuffer(s_workBufferChar, 1, s_workBufferU32[s]);
		s_workBufferChar[ s_workBufferU32[s] ] = 0;

		ptr[s] = s_workBufferChar;
	}
}

void FileStream::writeString(const std::string* ptr, u32 count)
{
	assert(m_mode == MODE_WRITE || m_mode == MODE_READWRITE);
	assert(m_file);	// TODO: Add Archive support.
	assert(count <= 256);
	//first read the length.
	for (u32 s=0; s<count; s++)
	{
		s_workBufferU32[s] = (u32)ptr[s].length();
	}
	fwrite(s_workBufferU32, sizeof(u32), count, m_file);

	//then write the string data.
	for (u32 s=0; s<count; s++)
	{
		fwrite(ptr[s].data(), 1, s_workBufferU32[s], m_file);
	}
}
