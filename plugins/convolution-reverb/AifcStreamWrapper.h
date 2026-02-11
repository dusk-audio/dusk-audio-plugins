/*
  ==============================================================================

    Convolution Reverb - AIFC Stream Wrapper
    Patches AIFC files with 'in24' compression type to 'NONE' for JUCE compatibility
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

/**
 * A wrapper around an InputStream that patches AIFC files with 'in24', 'in32', or 'in16'
 * compression types to use 'NONE' instead, making them compatible with JUCE's
 * AIFF reader.
 *
 * Apple's Space Designer uses .SDIR files which are AIFC format with 'in24'
 * (24-bit integer PCM). While 'in24' is functionally identical to 'NONE',
 * JUCE doesn't recognize it as a valid compression type.
 */
class AifcPatchedInputStream : public juce::InputStream{
public:
    AifcPatchedInputStream(std::unique_ptr<juce::InputStream> source)
        : sourceStream(std::move(source))
    {
        // Read the entire file into memory so we can patch it
        juce::MemoryOutputStream memStream;
        memStream.writeFromInputStream(*sourceStream, -1);

        patchedData.setSize(memStream.getDataSize());
        std::memcpy(patchedData.getData(), memStream.getData(), memStream.getDataSize());

        // Patch the AIFC compression type if needed
        patchAifcCompressionType();
    }

    ~AifcPatchedInputStream() override = default;

    juce::int64 getTotalLength() override
    {
        return patchedData.getSize();
    }

    int read(void* destBuffer, int maxBytesToRead) override
    {
        int bytesToRead = std::min(maxBytesToRead,
                                   static_cast<int>(patchedData.getSize() - position));

        if (bytesToRead <= 0)
            return 0;

        std::memcpy(destBuffer,
                    static_cast<const char*>(patchedData.getData()) + position,
                    static_cast<size_t>(bytesToRead));

        position += bytesToRead;
        return bytesToRead;
    }

    bool isExhausted() override
    {
        return position >= patchedData.getSize();
    }

    juce::int64 getPosition() override
    {
        return position;
    }

    bool setPosition(juce::int64 newPosition) override
    {
        position = juce::jlimit<juce::int64>(0, patchedData.getSize(), newPosition);
        return true;
    }

    bool wasPatched() const { return didPatch; }

private:
    std::unique_ptr<juce::InputStream> sourceStream;
    juce::MemoryBlock patchedData;
    juce::int64 position = 0;
    bool didPatch = false;

    void patchAifcCompressionType()
    {
        auto* data = static_cast<char*>(patchedData.getData());
        size_t size = patchedData.getSize();

        // Check if this is an AIFC file
        if (size < 12)
            return;

        // Check for FORM header
        if (std::memcmp(data, "FORM", 4) != 0)
            return;

        // Check for AIFC type (at offset 8)
        if (std::memcmp(data + 8, "AIFC", 4) != 0)
            return;

        // Search for COMM chunk and patch compression type
        size_t offset = 12;
        while (offset + 8 < size)
        {
            // Bounds check for chunk header read
            if (offset + 8 > size)
                return;

            // Read chunk ID
            char chunkId[5] = {0};
            std::memcpy(chunkId, data + offset, 4);

            // Read chunk size (big-endian)
            uint32_t chunkSize = (static_cast<uint8_t>(data[offset + 4]) << 24) |
                                 (static_cast<uint8_t>(data[offset + 5]) << 16) |
                                 (static_cast<uint8_t>(data[offset + 6]) << 8) |
                                 static_cast<uint8_t>(data[offset + 7]);

            // Sanity check chunk size to prevent overflow
            if (chunkSize > size - offset - 8)
                return;

            if (std::strcmp(chunkId, "COMM") == 0)
            {
                // AIFC COMM chunk structure:
                // 2 bytes: numChannels
                // 4 bytes: numSampleFrames
                // 2 bytes: sampleSize
                // 10 bytes: sampleRate (extended precision)
                // 4 bytes: compressionType (at offset + 8 + 18 = offset + 26)
                // ... compression name follows

                // Ensure chunk is large enough for compression type field
                if (chunkSize < 22)  // 18 bytes before compressionType + 4 bytes for compressionType
                    return;

                size_t compTypeOffset = offset + 8 + 18;

                if (compTypeOffset + 4 <= size)
                {
                    char compType[5] = {0};
                    std::memcpy(compType, data + compTypeOffset, 4);

                    // Patch 'in24', 'in32', 'in16' to 'NONE' (which JUCE supports)
                    if (std::strcmp(compType, "in24") == 0 ||
                        std::strcmp(compType, "in32") == 0 ||
                        std::strcmp(compType, "in16") == 0)
                    {
                        // Patch to 'NONE'
                        std::memcpy(data + compTypeOffset, "NONE", 4);
                        didPatch = true;
                        return;
                    }
                }
            }

            // Move to next chunk (8 bytes for header + chunk data + padding to even)
            size_t nextOffset = offset + 8 + chunkSize;
            if (chunkSize & 1)
                nextOffset++; // Pad to even boundary

            // Check for overflow before advancing
            if (nextOffset <= offset)
                return;

            offset = nextOffset;
        }
    }
};

/**
 * Helper function to create an audio format reader that handles AIFC files
 * with non-standard compression types like 'in24' (used by Space Designer .SDIR files).
 */
inline std::unique_ptr<juce::AudioFormatReader> createReaderForAudioFile(
    juce::AudioFormatManager& formatManager,
    const juce::File& file)
{
    // Check file extension to determine if we need AIFC patching
    auto ext = file.getFileExtension().toLowerCase();

    // Only use the AIFC patcher for AIFF/AIFC/SDIR files that might have 'in24' compression
    if (ext == ".sdir" || ext == ".aiff" || ext == ".aif" || ext == ".aifc")
    {
        auto inputStream = file.createInputStream();
        if (inputStream == nullptr)
            return nullptr;

        // Wrap the stream to patch AIFC compression types if needed
        auto patchedStream = std::make_unique<AifcPatchedInputStream>(std::move(inputStream));

        return std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(std::move(patchedStream)));
    }

    // For WAV, FLAC, OGG, MP3, etc. - use standard file-based reader
    return std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(file));
}
