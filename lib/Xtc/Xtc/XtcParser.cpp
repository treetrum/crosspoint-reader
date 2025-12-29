/**
 * XtcParser.cpp
 *
 * XTC file parsing implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "XtcParser.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>

#include <cstring>

namespace xtc {

XtcParser::XtcParser()
    : m_isOpen(false),
      m_defaultWidth(DISPLAY_WIDTH),
      m_defaultHeight(DISPLAY_HEIGHT),
      m_bitDepth(1),
      m_lastError(XtcError::OK),
      m_hasMetadata(false),
      m_hasChapters(false),
      m_coverPage(0xFFFF),
      m_readDirection(0) {
  memset(&m_header, 0, sizeof(m_header));
}

XtcParser::~XtcParser() { close(); }

XtcError XtcParser::open(const char* filepath) {
  // Close if already open
  if (m_isOpen) {
    close();
  }

  // Open file
  if (!FsHelpers::openFileForRead("XTC", filepath, m_file)) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return m_lastError;
  }

  // Read header
  m_lastError = readHeader();
  if (m_lastError != XtcError::OK) {
    Serial.printf("[%lu] [XTC] Failed to read header: %s\n", millis(), errorToString(m_lastError));
    m_file.close();
    return m_lastError;
  }

  // Read title if available
  readTitle();

  // Read page table
  m_lastError = readPageTable();
  if (m_lastError != XtcError::OK) {
    Serial.printf("[%lu] [XTC] Failed to read page table: %s\n", millis(), errorToString(m_lastError));
    m_file.close();
    return m_lastError;
  }

  m_isOpen = true;
  Serial.printf("[%lu] [XTC] Opened file: %s (%u pages, %dx%d)\n", millis(), filepath, m_header.pageCount,
                m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

void XtcParser::close() {
  if (m_isOpen) {
    m_file.close();
    m_isOpen = false;
  }
  m_pageTable.clear();
  m_chapters.clear();
  m_title.clear();
  m_author.clear();
  m_hasMetadata = false;
  m_hasChapters = false;
  m_coverPage = 0xFFFF;
  m_readDirection = 0;
  memset(&m_header, 0, sizeof(m_header));
}

XtcError XtcParser::readHeader() {
  // First, read as V2 header to detect format
  XtcHeaderV2 headerV2;
  size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&headerV2), sizeof(XtcHeaderV2));
  if (bytesRead != sizeof(XtcHeaderV2)) {
    return XtcError::READ_ERROR;
  }

  // Verify magic number (accept both XTC and XTCH)
  if (headerV2.magic != XTC_MAGIC && headerV2.magic != XTCH_MAGIC) {
    Serial.printf("[%lu] [XTC] Invalid magic: 0x%08X (expected 0x%08X or 0x%08X)\n", millis(), headerV2.magic,
                  XTC_MAGIC, XTCH_MAGIC);
    return XtcError::INVALID_MAGIC;
  }

  // Determine bit depth from file magic
  m_bitDepth = (headerV2.magic == XTCH_MAGIC) ? 2 : 1;

  // Basic validation
  if (headerV2.pageCount == 0) {
    return XtcError::CORRUPTED_HEADER;
  }

  // Detect V2 format: hasMetadata or hasChapters should be 0 or 1,
  // and metadataOffset should be reasonable if hasMetadata is set
  bool isV2Format = false;
  if ((headerV2.hasMetadata == 0 || headerV2.hasMetadata == 1) &&
      (headerV2.hasChapters == 0 || headerV2.hasChapters == 1) &&
      (headerV2.hasThumbnails == 0 || headerV2.hasThumbnails == 1) && headerV2.readDirection <= 2) {
    // Check if offsets are reasonable (not zero for V2 with metadata/chapters)
    if ((headerV2.hasMetadata && headerV2.metadataOffset > 0) || (headerV2.hasChapters && headerV2.chapterOffset > 0) ||
        (!headerV2.hasMetadata && !headerV2.hasChapters)) {
      isV2Format = true;
    }
  }

  if (isV2Format) {
    // Process as V2 format
    m_header.magic = headerV2.magic;
    m_header.version = headerV2.version;
    m_header.pageCount = headerV2.pageCount;
    m_header.pageTableOffset = headerV2.indexOffset;
    m_header.dataOffset = headerV2.dataOffset;

    m_hasMetadata = headerV2.hasMetadata != 0;
    m_hasChapters = headerV2.hasChapters != 0;
    m_readDirection = headerV2.readDirection;

    Serial.printf("[%lu] [XTC] V2 Header: magic=0x%08X (%s), ver=0x%04X, pages=%u, bitDepth=%u\n", millis(),
                  headerV2.magic, (headerV2.magic == XTCH_MAGIC) ? "XTCH" : "XTC", headerV2.version, headerV2.pageCount,
                  m_bitDepth);
    Serial.printf("[%lu] [XTC] V2 Metadata: hasMetadata=%d, hasChapters=%d, readDir=%d\n", millis(), m_hasMetadata,
                  m_hasChapters, m_readDirection);

    // Read metadata if available
    if (m_hasMetadata && headerV2.metadataOffset > 0) {
      XtcError err = readMetadataV2(headerV2.metadataOffset);
      if (err != XtcError::OK) {
        Serial.printf("[%lu] [XTC] Warning: Failed to read metadata\n", millis());
      }
    }

    // Read chapters if available
    if (m_hasChapters && headerV2.chapterOffset > 0) {
      // Chapter count is in metadata, use it if available
      uint16_t chapterCount = 0;
      if (m_hasMetadata) {
        // Already read from metadata in readMetadataV2
        chapterCount = static_cast<uint16_t>(m_chapters.capacity());  // Will be set properly in readChaptersV2
      }
      // Read chapters - chapterCount will be determined from file or metadata
      XtcError err = readChaptersV2(headerV2.chapterOffset, chapterCount);
      if (err != XtcError::OK) {
        Serial.printf("[%lu] [XTC] Warning: Failed to read chapters\n", millis());
      }
    }
  } else {
    // Process as V1 (legacy) format - copy to m_header
    memcpy(&m_header, &headerV2, sizeof(XtcHeader));

    // Check version
    if (m_header.version > 1) {
      Serial.printf("[%lu] [XTC] Unsupported version: %d\n", millis(), m_header.version);
      return XtcError::INVALID_VERSION;
    }

    Serial.printf("[%lu] [XTC] V1 Header: magic=0x%08X (%s), ver=%u, pages=%u, bitDepth=%u\n", millis(), m_header.magic,
                  (m_header.magic == XTCH_MAGIC) ? "XTCH" : "XTC", m_header.version, m_header.pageCount, m_bitDepth);
  }

  return XtcError::OK;
}

XtcError XtcParser::readTitle() {
  // Title is usually at offset 0x38 (56) for 88-byte headers
  // Read title as null-terminated UTF-8 string
  if (m_header.titleOffset == 0) {
    m_header.titleOffset = 0x38;  // Default offset
  }

  if (!m_file.seek(m_header.titleOffset)) {
    return XtcError::READ_ERROR;
  }

  char titleBuf[128] = {0};
  m_file.read(reinterpret_cast<uint8_t*>(titleBuf), sizeof(titleBuf) - 1);
  m_title = titleBuf;

  Serial.printf("[%lu] [XTC] Title: %s\n", millis(), m_title.c_str());
  return XtcError::OK;
}

XtcError XtcParser::readPageTable() {
  if (m_header.pageTableOffset == 0) {
    Serial.printf("[%lu] [XTC] Page table offset is 0, cannot read\n", millis());
    return XtcError::CORRUPTED_HEADER;
  }

  // Seek to page table
  if (!m_file.seek(m_header.pageTableOffset)) {
    Serial.printf("[%lu] [XTC] Failed to seek to page table at %llu\n", millis(), m_header.pageTableOffset);
    return XtcError::READ_ERROR;
  }

  m_pageTable.resize(m_header.pageCount);

  // Read page table entries
  for (uint16_t i = 0; i < m_header.pageCount; i++) {
    PageTableEntry entry;
    size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(PageTableEntry));
    if (bytesRead != sizeof(PageTableEntry)) {
      Serial.printf("[%lu] [XTC] Failed to read page table entry %u\n", millis(), i);
      return XtcError::READ_ERROR;
    }

    m_pageTable[i].offset = static_cast<uint32_t>(entry.dataOffset);
    m_pageTable[i].size = entry.dataSize;
    m_pageTable[i].width = entry.width;
    m_pageTable[i].height = entry.height;
    m_pageTable[i].bitDepth = m_bitDepth;

    // Update default dimensions from first page
    if (i == 0) {
      m_defaultWidth = entry.width;
      m_defaultHeight = entry.height;
    }
  }

  Serial.printf("[%lu] [XTC] Read %u page table entries\n", millis(), m_header.pageCount);
  return XtcError::OK;
}

bool XtcParser::getPageInfo(uint32_t pageIndex, PageInfo& info) const {
  if (pageIndex >= m_pageTable.size()) {
    return false;
  }
  info = m_pageTable[pageIndex];
  return true;
}

size_t XtcParser::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) {
  if (!m_isOpen) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return 0;
  }

  if (pageIndex >= m_header.pageCount) {
    m_lastError = XtcError::PAGE_OUT_OF_RANGE;
    return 0;
  }

  const PageInfo& page = m_pageTable[pageIndex];

  // Seek to page data
  if (!m_file.seek(page.offset)) {
    Serial.printf("[%lu] [XTC] Failed to seek to page %u at offset %lu\n", millis(), pageIndex, page.offset);
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  // Read page header (XTG for 1-bit, XTH for 2-bit - same structure)
  XtgPageHeader pageHeader;
  size_t headerRead = m_file.read(reinterpret_cast<uint8_t*>(&pageHeader), sizeof(XtgPageHeader));
  if (headerRead != sizeof(XtgPageHeader)) {
    Serial.printf("[%lu] [XTC] Failed to read page header for page %u\n", millis(), pageIndex);
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  // Verify page magic (XTG for 1-bit, XTH for 2-bit)
  const uint32_t expectedMagic = (m_bitDepth == 2) ? XTH_MAGIC : XTG_MAGIC;
  if (pageHeader.magic != expectedMagic) {
    Serial.printf("[%lu] [XTC] Invalid page magic for page %u: 0x%08X (expected 0x%08X)\n", millis(), pageIndex,
                  pageHeader.magic, expectedMagic);
    m_lastError = XtcError::INVALID_MAGIC;
    return 0;
  }

  // Calculate bitmap size based on bit depth
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t bitmapSize;
  if (m_bitDepth == 2) {
    // XTH: two bit planes, each containing (width * height) bits rounded up to bytes
    bitmapSize = ((static_cast<size_t>(pageHeader.width) * pageHeader.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageHeader.width + 7) / 8) * pageHeader.height;
  }

  // Check buffer size
  if (bufferSize < bitmapSize) {
    Serial.printf("[%lu] [XTC] Buffer too small: need %u, have %u\n", millis(), bitmapSize, bufferSize);
    m_lastError = XtcError::MEMORY_ERROR;
    return 0;
  }

  // Read bitmap data
  size_t bytesRead = m_file.read(buffer, bitmapSize);
  if (bytesRead != bitmapSize) {
    Serial.printf("[%lu] [XTC] Page read error: expected %u, got %u\n", millis(), bitmapSize, bytesRead);
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  m_lastError = XtcError::OK;
  return bytesRead;
}

XtcError XtcParser::loadPageStreaming(uint32_t pageIndex,
                                      std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                      size_t chunkSize) {
  if (!m_isOpen) {
    return XtcError::FILE_NOT_FOUND;
  }

  if (pageIndex >= m_header.pageCount) {
    return XtcError::PAGE_OUT_OF_RANGE;
  }

  const PageInfo& page = m_pageTable[pageIndex];

  // Seek to page data
  if (!m_file.seek(page.offset)) {
    return XtcError::READ_ERROR;
  }

  // Read and skip page header (XTG for 1-bit, XTH for 2-bit)
  XtgPageHeader pageHeader;
  size_t headerRead = m_file.read(reinterpret_cast<uint8_t*>(&pageHeader), sizeof(XtgPageHeader));
  const uint32_t expectedMagic = (m_bitDepth == 2) ? XTH_MAGIC : XTG_MAGIC;
  if (headerRead != sizeof(XtgPageHeader) || pageHeader.magic != expectedMagic) {
    return XtcError::READ_ERROR;
  }

  // Calculate bitmap size based on bit depth
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, ((width * height + 7) / 8) * 2 bytes
  size_t bitmapSize;
  if (m_bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageHeader.width) * pageHeader.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageHeader.width + 7) / 8) * pageHeader.height;
  }

  // Read in chunks
  std::vector<uint8_t> chunk(chunkSize);
  size_t totalRead = 0;

  while (totalRead < bitmapSize) {
    size_t toRead = std::min(chunkSize, bitmapSize - totalRead);
    size_t bytesRead = m_file.read(chunk.data(), toRead);

    if (bytesRead == 0) {
      return XtcError::READ_ERROR;
    }

    callback(chunk.data(), bytesRead, totalRead);
    totalRead += bytesRead;
  }

  return XtcError::OK;
}

bool XtcParser::isValidXtcFile(const char* filepath) {
  File file = SD.open(filepath, FILE_READ);
  if (!file) {
    return false;
  }

  uint32_t magic = 0;
  size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic));
  file.close();

  if (bytesRead != sizeof(magic)) {
    return false;
  }

  return (magic == XTC_MAGIC || magic == XTCH_MAGIC);
}

XtcError XtcParser::readMetadataV2(uint64_t metadataOffset) {
  if (!m_file.seek(metadataOffset)) {
    return XtcError::READ_ERROR;
  }

  XtcMetadata metadata;
  size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&metadata), sizeof(XtcMetadata));
  if (bytesRead != sizeof(XtcMetadata)) {
    return XtcError::READ_ERROR;
  }

  // Extract title (ensure null-termination)
  metadata.title[127] = '\0';
  m_title = metadata.title;

  // Extract author
  metadata.author[63] = '\0';
  m_author = metadata.author;

  // Cover page
  m_coverPage = metadata.coverPage;

  // Reserve space for chapters if count is available
  if (metadata.chapterCount > 0) {
    m_chapters.reserve(metadata.chapterCount);
  }

  Serial.printf("[%lu] [XTC] Metadata: title=\"%s\", author=\"%s\", coverPage=%u, chapterCount=%u\n", millis(),
                m_title.c_str(), m_author.c_str(), m_coverPage, metadata.chapterCount);

  return XtcError::OK;
}

XtcError XtcParser::readChaptersV2(uint64_t chapterOffset, uint16_t chapterCount) {
  if (!m_file.seek(chapterOffset)) {
    return XtcError::READ_ERROR;
  }

  // If chapterCount is 0, try to determine from reserved capacity or read until invalid
  uint16_t maxChapters = chapterCount > 0 ? chapterCount : 100;  // Reasonable limit

  m_chapters.clear();

  for (uint16_t i = 0; i < maxChapters; i++) {
    XtcChapter chapter;
    size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&chapter), sizeof(XtcChapter));
    if (bytesRead != sizeof(XtcChapter)) {
      break;
    }

    // Check if this is a valid chapter entry (non-empty name or valid page range)
    chapter.chapterName[79] = '\0';
    if (chapter.chapterName[0] == '\0' && chapter.startPage == 0 && chapter.endPage == 0 && chapterCount == 0) {
      // Empty entry and no explicit count - assume end of chapters
      break;
    }

    ChapterInfo info;
    info.name = chapter.chapterName;
    info.startPage = chapter.startPage;
    info.endPage = chapter.endPage;
    m_chapters.push_back(info);

    Serial.printf("[%lu] [XTC] Chapter %u: \"%s\" (pages %u-%u)\n", millis(), i, info.name.c_str(), info.startPage,
                  info.endPage);

    // Stop if we've read the expected count
    if (chapterCount > 0 && m_chapters.size() >= chapterCount) {
      break;
    }
  }

  Serial.printf("[%lu] [XTC] Loaded %u chapters\n", millis(), static_cast<unsigned>(m_chapters.size()));

  return XtcError::OK;
}

int XtcParser::getChapterIndexForPage(uint32_t pageIndex) const {
  for (size_t i = 0; i < m_chapters.size(); i++) {
    // Adjust for 1-based page numbers (same logic as chapter selection navigation)
    // XTC_FORMAT.md specifies 0-based, but some converters use 1-based
    uint32_t startPage = (m_chapters[i].startPage > 0) ? m_chapters[i].startPage - 1 : 0;
    uint32_t endPage = (m_chapters[i].endPage > 0) ? m_chapters[i].endPage - 1 : 0;

    if (pageIndex >= startPage && pageIndex <= endPage) {
      return static_cast<int>(i);
    }
  }
  return -1;  // No chapter found for this page
}

}  // namespace xtc
