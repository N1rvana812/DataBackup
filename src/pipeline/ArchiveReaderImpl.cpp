#include "pipeline/ArchiveReaderImpl.h"
#include "pipeline/KeyDerivation.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace backup {

ArchiveReaderImpl::ArchiveReaderImpl() = default;

ArchiveReaderImpl::~ArchiveReaderImpl() {
    close();
}

bool ArchiveReaderImpl::open(const std::string& archivePath) {
    return open(archivePath, BackupConfig{});
}

bool ArchiveReaderImpl::open(const std::string& archivePath, const BackupConfig& config) {
    if (file_ != nullptr) {
        close();
    }

    file_ = std::fopen(archivePath.c_str(), "rb");
    if (file_ == nullptr) {
        return false;
    }

    // Read the global header
    ArchiveGlobalHeader header {};
    if (!readRaw(&header, sizeof(header))) {
        close();
        return false;
    }

    // Validate magic
    if (!ArchiveFormat::validateMagic(header.magic)) {
        close();
        return false;
    }

    // Parse flags
    compressionEnabled_ = (header.flags & FLAG_COMPRESSION) != 0;
    encryptionEnabled_ = (header.flags & FLAG_ENCRYPTION) != 0;

    totalFileCount_ = 0;

    // Store salt and IV from header
    salt_.assign(header.salt, header.salt + SALT_SIZE);
    iv_.assign(header.iv, header.iv + IV_SIZE);

    // Derive encryption key if encryption is enabled
    if (encryptionEnabled_) {
        if (config.password.empty()) {
            // Encrypted archive but no password provided — will fail on read
            close();
            return false;
        }

        try {
            derivedKey_ = KeyDerivation::deriveKey(
                config.password, salt_.data(), salt_.size(), AES_KEY_SIZE);

            decryptor_ = std::make_unique<StreamEncryptor>();
            if (!decryptor_->init(derivedKey_.data(), derivedKey_.size(),
                                   iv_.data(), iv_.size())) {
                close();
                return false;
            }
        } catch (const std::exception&) {
            close();
            return false;
        }
    }

    if (compressionEnabled_) {
        decompressor_ = std::make_unique<StreamCompressor>(
            static_cast<int>(header.compressionLevel));
    }

    // Reset file reading state
    filesRead_ = 0;
    hasCurrentFile_ = false;

    return true;
}

bool ArchiveReaderImpl::getNextFileMeta(FileMetaData& meta) {
    if (file_ == nullptr) {
        return false;
    }

    // If we've read all files, return false
    if (totalFileCount_ > 0 && filesRead_ >= totalFileCount_) {
        return false;
    }

    // Read the file entry header
    FileEntryHeader entryHeader {};
    if (!readRaw(&entryHeader, sizeof(entryHeader))) {
        return false;
    }

    // Validate: if pathLength is 0 or unreasonably large, try reading as footer
    if (entryHeader.pathLength == 0 || entryHeader.pathLength > 4096) {
        std::fseek(file_, -static_cast<long>(sizeof(entryHeader)), SEEK_CUR);

        ArchiveFooter footer {};
        if (readRaw(&footer, sizeof(footer))) {
            if (ArchiveFormat::validateFooterMagic(footer.footerMagic)) {
                totalFileCount_ = footer.fileCount;
                return false;
            }
        }

        std::fseek(file_, -static_cast<long>(sizeof(footer)), SEEK_CUR);
        return false;
    }

    // Read the path
    std::string path(entryHeader.pathLength, '\0');
    if (entryHeader.pathLength > 0) {
        if (!readRaw(&path[0], entryHeader.pathLength)) {
            return false;
        }
    }

    // Convert to FileMetaData
    ArchiveFormat::fileEntryHeaderToMetaData(entryHeader, path, currentMeta_);
    meta = currentMeta_;

    hasCurrentFile_ = true;
    currentFileRemaining_ = entryHeader.isDirectory ? 0 : entryHeader.originalSize;

    // Reset internal buffer for the new file
    internalBuffer_.clear();
    internalBufferOffset_ = 0;

    ++filesRead_;
    return true;
}

ssize_t ArchiveReaderImpl::readChunk(uint8_t* buffer, size_t size) {
    if (file_ == nullptr || !hasCurrentFile_) {
        return -1;
    }
    if (buffer == nullptr && size > 0) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    // If the current file is a directory or we've exhausted it, return EOF
    if (currentMeta_.isDirectory || currentFileRemaining_ == 0) {
        internalBuffer_.clear();
        internalBufferOffset_ = 0;
        return 0;
    }

    size_t totalCopied = 0;

    while (totalCopied < size) {
        // If internal buffer is exhausted, try to fill it
        if (internalBufferOffset_ >= internalBuffer_.size()) {
            if (!fillInternalBuffer()) {
                break;
            }
        }

        // Copy from internal buffer to user buffer
        const size_t available = internalBuffer_.size() - internalBufferOffset_;
        const size_t toCopy = std::min(size - totalCopied, available);

        std::memcpy(buffer + totalCopied,
                     internalBuffer_.data() + internalBufferOffset_,
                     toCopy);

        internalBufferOffset_ += toCopy;
        totalCopied += toCopy;
        currentFileRemaining_ -= toCopy;
    }

    return static_cast<ssize_t>(totalCopied);
}

void ArchiveReaderImpl::close() {
    cleanup();
}

bool ArchiveReaderImpl::readRaw(void* buffer, size_t size) {
    if (buffer == nullptr || size == 0) {
        return true;
    }
    const size_t bytesRead = std::fread(buffer, 1, size, file_);
    return bytesRead == size;
}

bool ArchiveReaderImpl::fillInternalBuffer() {
    if (file_ == nullptr) {
        return false;
    }

    // Read the chunk length
    uint32_t chunkLen = 0;
    if (!readRaw(&chunkLen, sizeof(chunkLen))) {
        return false;
    }

    // Zero chunk length = EOF marker for this file
    if (chunkLen == 0) {
        currentFileRemaining_ = 0;
        return false;
    }

    // Read the encrypted/compressed chunk data
    std::vector<uint8_t> chunkData(chunkLen);
    if (chunkLen > 0) {
        if (!readRaw(chunkData.data(), chunkLen)) {
            return false;
        }
    }

    const uint8_t* dataPtr = chunkData.data();
    size_t dataSize = chunkData.size();

    // 1. Decrypt (if encryption is enabled)
    std::vector<uint8_t> decryptedData;
    if (encryptionEnabled_ && decryptor_) {
        try {
            decryptedData = decryptor_->decrypt(dataPtr, dataSize);
            dataPtr = decryptedData.data();
            dataSize = decryptedData.size();
        } catch (const std::exception&) {
            return false;
        }
    }

    // 2. Decompress (if compression is enabled)
    if (compressionEnabled_ && decompressor_) {
        try {
            internalBuffer_ = decompressor_->decompress(
                dataPtr, dataSize, CHUNK_SIZE);
        } catch (const std::exception&) {
            return false;
        }
    } else {
        // No compression: raw data goes directly into the buffer
        internalBuffer_.assign(dataPtr, dataPtr + dataSize);
    }

    internalBufferOffset_ = 0;
    return !internalBuffer_.empty();
}

void ArchiveReaderImpl::cleanup() {
    KeyDerivation::secureClear(derivedKey_);

    decryptor_.reset();
    decompressor_.reset();

    internalBuffer_.clear();
    internalBufferOffset_ = 0;

    hasCurrentFile_ = false;
    currentFileRemaining_ = 0;
    filesRead_ = 0;
    totalFileCount_ = 0;

    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

} // namespace backup
