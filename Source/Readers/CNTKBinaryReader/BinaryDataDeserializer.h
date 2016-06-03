//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//

#pragma once

#include "DataDeserializerBase.h"
#include "BinaryConfigHelper.h"
#include "CorpusDescriptor.h"
#include "BinaryDataChunk.h"

namespace Microsoft { namespace MSR { namespace CNTK {


class BinaryDataDeserialzer {
public:
    virtual size_t GetSequencesForChunk(size_t numSequences, size_t startIndex, void* data, std::vector<SequenceDataPtr>& result) = 0;

    StorageType GetStorageType() { return m_matType; }
    ElementType GetElementType() { return m_elemType; }
    TensorShapePtr GetSampleLayout() { return make_shared<TensorShape>(m_numCols); }

    size_t GetElemSizeBytes()
    {
        if (m_elemType == ElementType::tfloat)
            return sizeof(float);
        else if (m_elemType == ElementType::tdouble)
            return sizeof(double);
        else
            LogicError("Error, elemtype is not defined for BinaryDataDeserializer.");
    }
    
protected:
    StorageType m_matType;
    ElementType m_elemType;
    size_t m_numCols;

};

typedef shared_ptr<BinaryDataDeserialzer> BinaryDataDeserializerPtr;
    
class DenseBinaryDataDeserializer : public BinaryDataDeserialzer
{
public:
    DenseBinaryDataDeserializer(FILE* infile)
    {
        // We don't have to read the storage type. We know we're dense
        m_matType = StorageType::dense;

        // Read the element type, note it's stored as an int32
        int32_t elemType;
        fread(&elemType, sizeof(elemType), 1, infile);
        if (elemType == 0)
            m_elemType = ElementType::tfloat;
        else if (elemType == 1)
            m_elemType = ElementType::tdouble;
        else
            RuntimeError("Error, the reader read element type %d, but only 0 (float) and 1 (double) are valid.", elemType);

        // Read the number of columns
        int32_t numCols;
        fread(&numCols, sizeof(numCols), 1, infile);
        m_numCols = numCols;
    }

    size_t GetSequencesForChunk(size_t numSequences, size_t startIndex, void* data, std::vector<SequenceDataPtr>& result)
    {
        size_t elemSize = GetElemSizeBytes();
        result.resize(numSequences);
        for (size_t c = 0; c < numSequences; c++)
        {
            DenseSequenceDataPtr sequence = make_shared<DenseSequenceData>();
            // We can't popuplate sequence->m_chunk here, so delay that for later
            sequence->m_chunk           = nullptr;
            sequence->m_data            = (char*)data + c*m_numCols*elemSize;
            sequence->m_id              = startIndex + c;
            sequence->m_numberOfSamples = 1;
            sequence->m_sampleLayout    = std::make_shared<TensorShape>(m_numCols);
            result[c] = sequence;
        }

        // For dense, the number of bytes processed is just numRows * numCols * elemSize;
        return numSequences * m_numCols * elemSize;
    }

};

class SparseBinaryDataDeserializer : public BinaryDataDeserialzer
{
public:
    SparseBinaryDataDeserializer(FILE* infile)
    {
        // Read the storage type. Currently we only support sparse_csc, 
        // but for future compatability allow it to be a parameter.
        int32_t storageType;
        fread(&storageType, sizeof(storageType), 1, infile);
        if (storageType == 0)
            m_matType = StorageType::sparse_csc;
        else
            RuntimeError("Error, the reader read matrix type %d, but only 0 (sparse_csc) is valid.", storageType);

        // Read the element type, note it's stored as an int32
        int32_t elemType;
        fread(&elemType, sizeof(elemType), 1, infile);
        if (elemType== 0)
            m_elemType = ElementType::tfloat;
        else if (elemType == 1)
            m_elemType = ElementType::tdouble;
        else
            RuntimeError("Error, the reader read element type %d, but only 0 (float) and 1 (double) are valid.", elemType);

        // Read the number of columns
        int32_t numCols;
        fread(&numCols, sizeof(numCols), 1, infile);
        m_numCols = numCols;
    }
    
    // The format of data is: 
    // int32_t: nnz for the entire chunk
    // ElemType[nnz]: the values for the sparse sequences
    // int32_t[nnz]: the row offsets for the sparse sequences
    // int32_t[numSequences]: the column offsets for the sparse sequences
    size_t GetSequencesForChunk(size_t numSequences, size_t startIndex, void* data, std::vector<SequenceDataPtr>& result)
    {
        size_t elemSize = GetElemSizeBytes();
        result.resize(numSequences);

        // For sparse, the first int32_t is the number of nnz values in the entire set of sequences
        int32_t totalNNz = *(int32_t*)data;

        // the rest of this chunk
        // Since we're not templating on ElemType, we use void for the values. Note that this is the only place
        // this deserializer uses ElemType, the rest are int32_t for this deserializer.
        void* values = (byte*)data + sizeof(int32_t);

        // Now the row offsets
        int32_t* rowOffsets = (int32_t*)((byte*)values + elemSize * totalNNz);

        // Now the col offsets
        int32_t* colOffsets = rowOffsets + totalNNz;

        // Now we setup some helper members to process the chunk
        for (size_t colIndex = 0; colIndex < numSequences; colIndex++)
        {
            SparseSequenceDataPtr sequence = make_shared<SparseSequenceData>();
            // We can't popuplate sequence->m_chunk here, so delay that for later
            sequence->m_chunk           = nullptr;
            sequence->m_id              = startIndex + colIndex;

            // We know the number of elements in all of the samples, it's just this:
            sequence->m_totalNnzCount = colOffsets[colIndex + 1] - colOffsets[colIndex];

            // The values array is already properly packed, so just use it.
            sequence->m_data = values;
            
            // The indices are correct (note they MUST BE IN INCREASING ORDER), but we will have to fix them up a 
            // little bit, for now just use them
            sequence->m_indices = rowOffsets;
            for (int32_t curRow = 0; curRow < sequence->m_totalNnzCount; curRow++)
            {
                // Get the sample for the current index
                size_t sampleNum = rowOffsets[curRow] / m_numCols;
                // The current sample might be OOB, if so, fill in the the missing ones.
                while(sequence->m_nnzCounts.size() < sampleNum+1)
                    sequence->m_nnzCounts.push_back(0);
                // Now that we have enough samples, increment the nnz for the sample
                sequence->m_nnzCounts[sampleNum] += 1;
                // Now that we've found it's sample, fix up the index.
                rowOffsets[curRow] %= m_numCols;
            }
            sequence->m_numberOfSamples = sequence->m_nnzCounts.size();
            // update values, rowOffsets pointers
            values = (byte*)values + sequence->m_totalNnzCount * elemSize;
            rowOffsets += sequence->m_totalNnzCount;

            result[colIndex] = sequence;
        }

        // For spares, we compute how many bytes we processed
        // From the header to this function, we see that is:
        // sizeof(int32_t) + totalNNz * sizeof(ElemType) + totalNNz * sizeof(int32_t) + numSequences * sizeof(int32_t)
        return sizeof(int32_t) + totalNNz * (elemSize + sizeof(int32_t)) + (numSequences + 1) * sizeof(int32_t);
    }

};

    
}}}
