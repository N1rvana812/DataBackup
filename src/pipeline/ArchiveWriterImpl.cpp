#include "pipeline/ArchiveWriterImpl.h"
#include "pipeline/KeyDerivation.h"

#include <cstring>
#include <stdexcept>

namespace backup {

ArchiveWriterImpl::ArchiveWriterImpl() = default;

ArchiveWriterImpl::~ArchiveWriterImpl() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

bool ArchiveWriterImpl::init(const std::string& archivePath, const BackupConfig& config) {
    // Close any previously opened file
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }

    config_ = config;
    fileCount_ = 0;

    // Open the archive file for binary writing
    file_ = std::fopen(archivePath.c_str(), "wb");
    if (file_ == nullptr) {
        return false;
    }

    try {
        // Generate random salt and IV
        salt_ = KeyDerivation::generateRandomBytes(SALT_SIZE);
        iv_ = KeyDerivation::generateRandomBytes(IV_SIZE);

        // Derive encryption key if encryption is enabled
        if (config_.enableEncryption) {
            derivedKey_ = KeyDerivation::deriveKey(
                config_.password, salt_.data(), salt_.size(), AES_KEY_SIZE);

            encryptor_ = std::make_unique<StreamEncryptor>();
            if (!encryptor_->init(derivedKey_.data(), derivedKey_.size(),
                                   iv_.data(), iv_.size())) {
                cleanup();
                return false;
            }
        }

        // Create compressor if compression is enabled
        if (config_.enableCompression) {
            compressor_ = std::make_unique<StreamCompressor>(config_.compressionLevel);
        }

        // Write the global header
        ArchiveGlobalHeader header {};
        ArchiveFormat::initGlobalHeader(header, config_, salt_, iv_);

        if (!writeRaw(&header, sizeof(header))) {
            cleanup();
            return false;
        }

        // Record the index offset (right after the global header)
        indexOffset_ = sizeof(ArchiveGlobalHeader);

        return true;
    } catch (const std::exception&) {
        cleanup();
        return false;
    }
}

bool ArchiveWriterImpl::addFile(std::shared_ptr<IFileReader> reader) {
    if (file_ == nullptr || reader == nullptr) {
        return false;
    }

    const FileMetaData meta = reader->getMetaData();

    if (!writeFileHeader(meta)) {
        return false;
    }

    if (meta.isDirectory) {
        ++fileCount_;
        return true;
    }

    // Regular file: write chunked data
    if (!writeRegularFile(std::move(reader))) {
        return false;
    }

    ++fileCount_;
    return true;
}

bool ArchiveWriterImpl::finalize() {
    if (file_ == nullptr) {
        return false;
    }

    if (!writeFooter()) {
        return false;
    }

    // Securely clear sensitive data from memory
    KeyDerivation::secureClear(derivedKey_);
    encryptor_.reset();
    compressor_.reset();

    std::fclose(file_);
    file_ = nullptr;

    return true;
}

bool ArchiveWriterImpl::writeRaw(const void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return true;
    }
    const size_t written = std::fwrite(data, 1, size, file_);
    return written == size;
}

bool ArchiveWriterImpl::writeFileHeader(const FileMetaData& meta) {
    FileEntryHeader header {};
    ArchiveFormat::metaDataToFileEntryHeader(meta, header);

    // Write the fixed-size header
    if (!writeRaw(&header, sizeof(header))) {
        return false;
    }

    // Write the variable-length path
    if (header.pathLength > 0) {
        if (!writeRaw(meta.relativePath.data(), header.pathLength)) {
            return false;
        }
    }

    return true;
}

bool ArchiveWriterImpl::writeDirectoryEntry(const FileMetaData& meta) {
    // Directory entries are just the file header, no data chunks
    return writeFileHeader(meta);
}

bool ArchiveWriterImpl::writeRegularFile(std::shared_ptr<IFileReader> reader) {
    std::vector<uint8_t> inputBuffer(CHUNK_SIZE);

    while (true) {
        // 1. Read 4KB data chunk
        const ssize_t bytesRead = reader->readChunk(inputBuffer.data(), inputBuffer.size());
        if (bytesRead < 0) {
            return false;
        }
        if (bytesRead == 0) {
            break; // EOF
        }

        const size_t inputSize = static_cast<size_t>(bytesRead);
        const uint8_t* dataPtr = inputBuffer.data();
        size_t dataSize = inputSize;

        // 2. Compress the chunk (if compression is enabled)
        std::vector<uint8_t> compressedData;
        if (compressor_) {
            try {
                compressedData = compressor_->compress(dataPtr, dataSize);
                dataPtr = compressedData.data();
                dataSize = compressedData.size();
            } catch (const std::exception&) {
                return false;
            }
        }

        // 3. Encrypt the chunk (if encryption is enabled)
        std::vector<uint8_t> encryptedData;
        if (encryptor_) {
            try {
                encryptedData = encryptor_->encrypt(dataPtr, dataSize);
                dataPtr = encryptedData.data();
                dataSize = encryptedData.size();
            } catch (const std::exception&) {
                return false;
            }
        }

        // 4. Write chunk length + chunk data
        const uint32_t chunkLen = static_cast<uint32_t>(dataSize);
        if (!writeRaw(&chunkLen, sizeof(chunkLen))) {
            return false;
        }
        if (dataSize > 0) {
            if (!writeRaw(dataPtr, dataSize)) {
                return false;
            }
        }
    }

    // Write EOF marker (chunk length = 0)
    const uint32_t zeroChunkLen = 0;
    return writeRaw(&zeroChunkLen, sizeof(zeroChunkLen));
}

bool ArchiveWriterImpl::writeFooter() {
    ArchiveFooter footer {};
    ArchiveFormat::initFooter(footer, indexOffset_, fileCount_);

    // Record the footer position before writing
    // (The footer starts at currentPos)
    if (!writeRaw(&footer, sizeof(footer))) {
        return false;
    }

    return true;
}

void ArchiveWriterImpl::cleanup() {
    KeyDerivation::secureClear(derivedKey_);
    encryptor_.reset();
    compressor_.reset();

    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

} // namespace backup
