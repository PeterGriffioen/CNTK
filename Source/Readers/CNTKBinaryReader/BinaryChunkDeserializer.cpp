//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//

#include "stdafx.h"
#include "BinaryChunkDeserializer.h"
#include "BinaryDataChunk.h"

namespace Microsoft { namespace MSR { namespace CNTK {

void BinaryChunkDeserializer::ReadOffsetsTable(FILE* infile)
{
    ReadOffsetsTable(infile, 0, m_numBatches);
}

void BinaryChunkDeserializer::ReadOffsetsTable(FILE* infile, size_t startOffset, size_t numBatches)
{
    assert((int64_t)(startOffset + numBatches) <= m_numBatches);
    size_t startPos = startOffset * sizeof(DiskOffsetsTable) + m_offsetStart;

    // Seek to the offsets table start
    _fseeki64(infile, startPos, SEEK_SET);

    // Note we create numBatches + 1 since we want to be consistent with determining the size of each chunk.
    DiskOffsetsTable* offsetsTable = new DiskOffsetsTable[numBatches + 1];

    fread(offsetsTable, sizeof(DiskOffsetsTable), numBatches, infile);

    // Now read the final entry. It is either the next offset entry (if we're reading a subset and the
    // entry exists), or we just fill it with the correct information based on file size
    if ((int64_t)(startOffset + numBatches) == m_numBatches)
    {
        _fseeki64(infile, 0, SEEK_END);
        offsetsTable[numBatches].offset = _ftelli64(infile);
        offsetsTable[numBatches].numSamples = 0;
        offsetsTable[numBatches].numSequences = 0;
    }
    else
        fread(offsetsTable + numBatches, sizeof(DiskOffsetsTable), 1, infile);

    m_offsetsTable = make_unique<OffsetsTable>(numBatches, offsetsTable);
}

BinaryChunkDeserializer::BinaryChunkDeserializer(const BinaryConfigHelper& helper) :
    BinaryChunkDeserializer(helper.GetFilePath())
{
    SetTraceLevel(helper.GetTraceLevel());

    // Rename/alias not yet implemented
    Initialize(helper.GetRename());
}


BinaryChunkDeserializer::BinaryChunkDeserializer(const std::wstring& filename) : 
    m_filename(filename),
    m_file(nullptr),
    m_offsetStart(0),
    m_dataStart(0),
    m_traceLevel(0)
{
    // streams will be used for rename when it's implemented.

}

BinaryChunkDeserializer::~BinaryChunkDeserializer()
{
    if (m_file)
        fclose(m_file);
}

void BinaryChunkDeserializer::Initialize(const std::map<std::wstring, std::wstring>& rename)
{
    if (m_file)
        fclose(m_file);

    m_file = fopenOrDie(m_filename, L"rb");

    // Parse the header
    _fseeki64(m_file, 0, SEEK_SET);

    // First read the version number of the data file, and make sure the reader version is the same.
    int64_t versionNumber;
    fread(&versionNumber, sizeof(versionNumber), 1, m_file);
    if (versionNumber != m_versionNumber)
        LogicError("The reader version is %d, but the data file was created for version %d.", m_versionNumber, versionNumber);

    // Next is the number of batches in the input file.
    fread(&m_numBatches, sizeof(m_numBatches), 1, m_file);

    // Next is the number of inputs
    fread(&m_numInputs, sizeof(m_numInputs), 1, m_file);

    // Reserve space for all of the inputs, and then read them in.
    m_streams.resize(m_numInputs);
    m_deserializers.resize(m_numInputs);

    int32_t len;
    int32_t maxLen = 100;
    char* tempName = new char[maxLen];
    for (int32_t c = 0; c < m_numInputs; c++)
    {
        // Create our streamDescription for this input
        auto streamDescription = std::make_shared<StreamDescription>();

        // read the name
        fread(&len, sizeof(len), 1, m_file);
        if (len + 1 > maxLen)
        {
            maxLen = len + 1;
            delete[] tempName;
            tempName = new char[maxLen];
        }
        fread(tempName, sizeof(char), len, m_file);
        tempName[len] = '\0';
        wstring wname = msra::strfun::utf16(tempName);
        if (rename.find(wname) == rename.end())
            streamDescription->m_name = wname;
        else
            streamDescription->m_name = rename.at(wname);

        // Read the matrix type. Then instantiate the appropriate BinaryDataDeserializer, and have it read in its parameters
        // Note: Is there a better way to do this?
        int32_t matType;
        fread(&matType, sizeof(matType), 1, m_file);
        if (matType == 0)
            m_deserializers[c] = make_shared<DenseBinaryDataDeserializer>(m_file);
        else if (matType == 1)
            m_deserializers[c] = make_shared<SparseBinaryDataDeserializer>(m_file);
        else
            RuntimeError("Unknown matrix type %d requested.", matType);

        streamDescription->m_id           = c;
        streamDescription->m_elementType  = m_deserializers[c]->GetElementType();
        streamDescription->m_storageType  = m_deserializers[c]->GetStorageType();
        streamDescription->m_sampleLayout = m_deserializers[c]->GetSampleLayout();
        m_streams[c] = streamDescription;
    }
    delete[] tempName;

    // We just finished the header. So we're now at the offsets table.
    m_offsetStart = _ftelli64(m_file);

    // After the header is the data start. Compute that now.
    m_dataStart = m_offsetStart + m_numBatches * sizeof(DiskOffsetsTable);

    // We only have to read in the offsets table once, so do that now.
    // Note it's possible in distributed reading mode to only want to read
    // a subset of the offsets table.
    ReadOffsetsTable(m_file);
}

ChunkDescriptions BinaryChunkDeserializer::GetChunkDescriptions()
{
    assert(m_offsetsTable);
    ChunkDescriptions result;
    result.reserve(m_numBatches);

    for (int64_t c = 0; c < m_numBatches; c++ ) 
    {
        result.push_back(shared_ptr<ChunkDescription>(
            new ChunkDescription {
                c,
                m_offsetsTable->GetNumSamples(c),
                m_offsetsTable->GetNumSequences(c)
        }));
    }

    return result;
}

void BinaryChunkDeserializer::GetSequencesForChunk(size_t chunkId, std::vector<SequenceDescription>& result)
{
    // Reserve space for each sequence
    result.reserve(m_offsetsTable->GetNumSequences(chunkId));

    // We don't store every piece of sequence information, so we have to read the chunk in, parse it, and then
    // find the information.
    ChunkPtr chunk = GetChunk(chunkId);

    size_t startId = m_offsetsTable->GetStartIndex(chunkId);
    std::vector<SequenceDataPtr> temp;
    for (size_t c = 0; c < m_offsetsTable->GetNumSequences(chunkId); c++)
    {
        // BUGBUG: This is inefficient, but we don't have a choice. Why do we need this at all? Why can't
        // this information just be gotten from the chunks? It's not clear.
        size_t numSamples = 0;
        temp.clear();
        chunk->GetSequence(m_offsetsTable->GetStartIndex(chunkId) + c, temp);
        for (size_t i = 0; i < temp.size(); i++) 
            numSamples = max(numSamples, temp[i]->m_numberOfSamples);

        result.push_back(
        {
            startId + c,
            numSamples,
            chunkId,
            true,
            { startId + c, 0 }
        });
    }
}

unique_ptr<byte[]> BinaryChunkDeserializer::ReadChunk(size_t chunkId)
{
    // Seek to the start of the chunk
    _fseeki64(m_file, m_dataStart + m_offsetsTable->GetOffset(chunkId), SEEK_SET);

    // Determine how big the chunk is.
    size_t chunkSize = m_offsetsTable->GetChunkSize(chunkId);
    
    // Create buffer
    unique_ptr<byte[]> buffer = make_unique<byte[]>(chunkSize);

    // Read the chunk from disk
    fread(buffer.get(), sizeof(byte), chunkSize, m_file);

    return buffer;
}


ChunkPtr BinaryChunkDeserializer::GetChunk(size_t chunkId)
{
    // Read the chunk into memory
    unique_ptr<byte[]> chunkBuffer = ReadChunk(chunkId);

    return make_shared<BinaryDataChunk>(chunkId, m_offsetsTable->GetStartIndex(chunkId), m_offsetsTable->GetNumSequences(chunkId), std::move(chunkBuffer), m_deserializers);
}

void BinaryChunkDeserializer::SetTraceLevel(unsigned int traceLevel)
{
    m_traceLevel = traceLevel;
}

}}}
