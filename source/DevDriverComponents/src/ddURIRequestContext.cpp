/*
 *******************************************************************************
 *
 * Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "gpuopen.h"
#if DD_VERSION_SUPPORTS(GPUOPEN_URIINTERFACE_CLEANUP_VERSION)
#include "ddURIRequestContext.h"
#include "ddTransferManager.h"

using namespace DevDriver;

// ========================================================================================================
// {Byte,Text,Json}Writer Callback
Result URIRequestContext::WriteBytes(void* pUserData, const void* pBytes, size_t numBytes)
{
    DD_ASSERT(pUserData != nullptr);
    URIRequestContext& context = *static_cast<URIRequestContext*>(pUserData);
    Result result = Result::Error;

    if (pBytes != nullptr)
    {
        context.GetBlock()->Write(pBytes, numBytes);
        result = Result::Success;
    }
    else if ((pBytes == nullptr) && (numBytes == 0))
    {
        // Special "End-of-Writer" call.
        switch (context.m_contextState)
        {
        case URIRequestContext::ContextState::ByteWriterSelected:
        case URIRequestContext::ContextState::TextWriterSelected:
        case URIRequestContext::ContextState::JsonWriterSelected:
            context.m_contextState = URIRequestContext::ContextState::WritingCompleted;
            result = Result::Success;
            break;
        default:
            // Invalid usage.
            DD_ASSERT(context.m_contextState != URIRequestContext::ContextState::WriterSelection);
            DD_ASSERT(context.m_contextState != URIRequestContext::ContextState::WritingCompleted);
            result = Result::Error;
        }
    }
    return result;
}

// ========================================================================================================
URIRequestContext::URIRequestContext()
    : m_postInfo(),
      m_contextState(ContextState::WriterSelection),
      m_byteWriter(this, URIRequestContext::WriteBytes),
      m_textWriter(this, URIRequestContext::WriteBytes),
      m_jsonWriter(this, URIRequestContext::WriteBytes)
{
}

// ========================================================================================================
void URIRequestContext::Begin(char*                                        pArguments,
                              URIDataFormat                                format,
                              SharedPointer<TransferProtocol::ServerBlock> pResponseBlock,
                              const PostDataInfo&                          postDataInfo)
{
    DD_ASSERT(m_contextState == ContextState::WriterSelection && "You missed a call to URIRequestContext::End()");
    m_postInfo           = postDataInfo;
    m_pRequestArguments  = pArguments;
    m_responseDataFormat = format;
    m_pResponseBlock     = pResponseBlock;
    m_contextState       = ContextState::WriterSelection;
}

// ========================================================================================================
void URIRequestContext::End()
{
    DD_ASSERT((m_contextState == ContextState::WriterSelection) ||
              (m_contextState == ContextState::WritingCompleted));
    m_contextState = ContextState::WriterSelection;
}

// ========================================================================================================
char* URIRequestContext::GetRequestArguments()
{
    return m_pRequestArguments;
}

// ========================================================================================================
const PostDataInfo& URIRequestContext::GetPostData() const
{
    return m_postInfo;
}

// ========================================================================================================
URIDataFormat URIRequestContext::GetUriDataFormat() const
{
    return m_responseDataFormat;
}

// ========================================================================================================
SharedPointer<TransferProtocol::ServerBlock> URIRequestContext::GetBlock() const
{
    return m_pResponseBlock;
}

// ========================================================================================================
Result URIRequestContext::BeginByteResponse(IByteWriter** ppWriter)
{
    Result result = Result::UriInvalidParameters;
    if (ppWriter != nullptr)
    {
        if (m_contextState == ContextState::WriterSelection)
        {
            m_contextState = ContextState::ByteWriterSelected;
            *ppWriter = &m_byteWriter;
            m_responseDataFormat = URIDataFormat::Binary;
            result = Result::Success;
        }
        else
        {
            result = Result::Rejected;
        }
    }
    return result;
}

// ========================================================================================================
Result URIRequestContext::BeginTextResponse(ITextWriter** ppWriter)
{
    Result result = Result::UriInvalidParameters;
    if (ppWriter != nullptr)
    {
        if (m_contextState == ContextState::WriterSelection)
        {
            m_contextState = ContextState::TextWriterSelected;
            *ppWriter = &m_textWriter;
            m_responseDataFormat = URIDataFormat::Text;
            result = Result::Success;
        }
        else
        {
            result = Result::Rejected;
        }
    }
    return result;
}

// ========================================================================================================
Result URIRequestContext::BeginJsonResponse(IStructuredWriter** ppWriter)
{
    Result result = Result::UriInvalidParameters;
    if (ppWriter != nullptr)
    {
        if (m_contextState == ContextState::WriterSelection)
        {
            m_contextState = ContextState::JsonWriterSelected;
            m_responseDataFormat = URIDataFormat::Text;
            *ppWriter = &m_jsonWriter;
            result = Result::Success;
        }
        else
        {
            result = Result::Rejected;
        }
    }
    return result;
}
#endif
