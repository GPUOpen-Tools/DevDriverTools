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

#include "protocols/ddURIClient.h"
#include "protocols/ddURIProtocol.h"
#include "protocols/ddTransferProtocol.h"
#include "msgChannel.h"
#include "ddTransferManager.h"

#define URI_CLIENT_MIN_MAJOR_VERSION URI_INITIAL_VERSION
#define URI_CLIENT_MAX_MAJOR_VERSION URI_POST_PROTOCOL_VERSION

namespace DevDriver
{
    namespace URIProtocol
    {
        static constexpr URIDataFormat ResponseFormatToUriFormat(ResponseDataFormat format)
        {
            static_assert(static_cast<uint32>(ResponseDataFormat::Unknown) == static_cast<uint32>(URIDataFormat::Unknown),
                          "ResponseDataFormat and URIDataFormat no longer match");
            static_assert(static_cast<uint32>(ResponseDataFormat::Text) == static_cast<uint32>(URIDataFormat::Text),
                          "ResponseDataFormat and URIDataFormat no longer match");
            static_assert(static_cast<uint32>(ResponseDataFormat::Binary) == static_cast<uint32>(URIDataFormat::Binary),
                          "ResponseDataFormat and URIDataFormat no longer match");
            static_assert(static_cast<uint32>(ResponseDataFormat::Count) == static_cast<uint32>(URIDataFormat::Count),
                          "ResponseDataFormat and URIDataFormat no longer match");
            return static_cast<URIDataFormat>(format);
        }

        // =====================================================================================================================
        URIClient::URIClient(IMsgChannel* pMsgChannel)
            : BaseProtocolClient(pMsgChannel, Protocol::URI, URI_CLIENT_MIN_MAJOR_VERSION, URI_CLIENT_MAX_MAJOR_VERSION)
        {
            memset(&m_context, 0, sizeof(m_context));
        }

        // =====================================================================================================================
        URIClient::~URIClient()
        {
        }

        // =====================================================================================================================
        Result URIClient::RequestURI(
            const char*      pRequestString,
            ResponseHeader*  pResponseHeader,
            const void*      pPostData,
            size_t           postDataSize)
        {
            Result result = Result::UriInvalidParameters;

            if ((m_context.state == State::Idle) &&
                (pRequestString != nullptr))
            {
                // Setup some sensible defaults in the response header
                if (pResponseHeader != nullptr)
                {
                    pResponseHeader->responseDataSizeInBytes = 0;
                    pResponseHeader->responseDataFormat = URIDataFormat::Unknown;
                }

                // Set up the request payload.
                SizedPayloadContainer container = {};
                // If there's no post data just create the container with the request string directly
                if ((pPostData == nullptr) || (postDataSize == 0))
                {
                    container.CreatePayload<URIRequestPayload>(pRequestString);
                    result = Result::Success;
                }
                else if (pPostData != nullptr)
                {
                    // Try to fit the post data into a single message packet
                    if (postDataSize <= kMaxInlineDataSize)
                    {
                        // If the data fits into a single packet, setup the URI payload struct first
                        container.CreatePayload<URIRequestPayload>(pRequestString,
                                                                   TransferProtocol::kInvalidBlockId,
                                                                   TransferDataFormat::Binary,
                                                                   static_cast<uint32>(postDataSize));
                        // Then copy the data into the payload right after the struct
                        void* pPostDataLocation = GetInlineDataPtr(&container);
                        memcpy(pPostDataLocation, pPostData, postDataSize);
                        // And then update the payload size so the post data doesn't get trimmed off
                        container.payloadSize = static_cast<uint32>(sizeof(URIRequestPayload) + postDataSize);
                        result = Result::Success;
                    }
                    else
                    {
                        // If the data won't fit in a single packet we need to request a block from the server.
                        // First send the post request, the response will tell us the block ID we should open to
                        // push our data into.
                        SizedPayloadContainer blockRequest = {};
                        blockRequest.CreatePayload<URIPostRequestPayload>(pRequestString, static_cast<uint32>(postDataSize));
                        result = TransactURIPayload(&blockRequest);
                        if (result == Result::Success)
                        {
                            // Read the response and get the block ID to use for our post data
                            const URIPostResponsePayload& response = blockRequest.GetPayload<URIPostResponsePayload>();
                            const uint32 pushBlockId = response.blockId;
                            result = response.result;
                            if (result == Result::Success)
                            {
                                // Open the indicated block and send our data
                                TransferProtocol::PushBlock* pPostBlock = m_pMsgChannel->GetTransferManager().OpenPushBlock(
                                    m_pSession->GetDestinationClientId(),
                                    pushBlockId,
                                    postDataSize);

                                if (pPostBlock != nullptr)
                                {
                                    result = pPostBlock->Write(static_cast<const uint8*>(pPostData), postDataSize);
                                    if (result == Result::Success)
                                    {
                                        result = pPostBlock->Finalize();
                                    }

                                    m_pMsgChannel->GetTransferManager().ClosePushBlock(&pPostBlock);
                                }
                                else
                                {
                                    result = Result::UriFailedToAcquirePostBlock;
                                }
                            }

                            // Finally setup the container to send the URI request, this time with the block ID that contains
                            // our post data
                            if (result == Result::Success)
                            {
                                container.CreatePayload<URIRequestPayload>(pRequestString,
                                                                           pushBlockId,
                                                                           TransferDataFormat::Binary,
                                                                           static_cast<uint32>(postDataSize));
                            }
                        }
                    }
                }

                // Issue a transaction.
                if (result == Result::Success)
                {
                    result = TransactURIPayload(&container);
                }

                if (result == Result::Success)
                {
                    const URIResponsePayload& responsePayload = container.GetPayload<URIResponsePayload>();

                    if (responsePayload.header.command == URIMessage::URIResponse)
                    {
                        result = responsePayload.result;
                        if (result == Result::Success)
                        {
                            // We've successfully received the response. Extract the relevant fields from the response.
                            if (responsePayload.blockId != TransferProtocol::kInvalidBlockId)
                            {
                                const TransferProtocol::BlockId& remoteBlockId = responsePayload.blockId;

                                // Attempt to open the pull block containing the response data.
                                // @Todo: Detect if the service returns the invalid block ID and treat that as a success.
                                //        It will require a new protocol version because existing clients will fail if
                                //        the invalid block ID is returned in lieu of a block of size 0.
                                TransferProtocol::PullBlock* pPullBlock =
                                    m_pMsgChannel->GetTransferManager().OpenPullBlock(GetRemoteClientId(), remoteBlockId);

                                if (pPullBlock != nullptr)
                                {
                                    m_context.pBlock = pPullBlock;
                                    const size_t blockSize = m_context.pBlock->GetBlockDataSize();

                                    // We successfully opened the block. Return the block data size and format via the header.
                                    // The header is optional so check for nullptr first.
                                    if (pResponseHeader != nullptr)
                                    {
                                        // Set up some defaults for the response fields.
                                        URIDataFormat responseDataFormat = URIDataFormat::Text;
                                        if (m_pSession->GetVersion() >= URI_RESPONSE_FORMATS_VERSION)
                                        {
                                            responseDataFormat = ResponseFormatToUriFormat(responsePayload.format);
                                        }

                                        pResponseHeader->responseDataSizeInBytes = blockSize;
                                        pResponseHeader->responseDataFormat = responseDataFormat;
                                    }

                                    // If the block size is non-zero we move to the read state
                                    if (blockSize > 0)
                                    {
                                        // Set up internal state.
                                        m_context.state = State::ReadResponse;
                                    }
                                    else // If the block size is zero we automatically close it and move back to idle
                                    {
                                        m_pMsgChannel->GetTransferManager().ClosePullBlock(&m_context.pBlock);
                                    }
                                }
                                else
                                {
                                    // Failed to open the response block.
                                    result = Result::UriFailedToOpenResponseBlock;
                                }
                            }
                        }
                    }
                }
            }
            return result;
        }

#if !DD_VERSION_SUPPORTS(GPUOPEN_URI_RESPONSE_FORMATS_VERSION)
        // =====================================================================================================================
        Result URIClient::RequestURI(
            const char*         pRequestString,
            size_t*             pResponseSizeInBytes)
        {
            Result result = Result::UriInvalidParamters;

            if ((pRequestString != nullptr) &&
                (pResponseSizeInBytes != nullptr))
            {
                // Pass a header into the request function so we can get the response size.
                ResponseHeader header = {};
                result = RequestURI(pRequestString, &header);

                // If the request was successful, extract the response size and return it.
                if (result == Result::Success)
                {
                    *pResponseSizeInBytes = header.responseDataSizeInBytes;
                }
            }

            return result;
        }
#endif

        // =====================================================================================================================
        Result URIClient::ReadResponse(uint8* pDstBuffer, size_t bufferSize, size_t* pBytesRead)
        {
            Result result = Result::UriInvalidParameters;

            if (m_context.state == State::ReadResponse)
            {
                result = m_context.pBlock->Read(pDstBuffer, bufferSize, pBytesRead);

                // If we reach the end of the stream or we encounter an error, we should transition back to the idle state.
                if ((result == Result::EndOfStream) ||
                    (result == Result::Error))
                {
                    m_context.state = State::Idle;
                    m_pMsgChannel->GetTransferManager().ClosePullBlock(&m_context.pBlock);
                }
            }

            return result;
        }

        // =====================================================================================================================
        Result URIClient::AbortRequest()
        {
            Result result = Result::UriInvalidParameters;

            if (m_context.state == State::ReadResponse)
            {
                m_context.state = State::Idle;
                m_pMsgChannel->GetTransferManager().ClosePullBlock(&m_context.pBlock);

                result = Result::Success;
            }

            return result;
        }

        // =====================================================================================================================
        void URIClient::ResetState()
        {
            // Close the pull block if it's still valid.
            if (m_context.pBlock != nullptr)
            {
                m_pMsgChannel->GetTransferManager().ClosePullBlock(&m_context.pBlock);
            }

            memset(&m_context, 0, sizeof(m_context));
        }

        // ============================================================================================================
        // Helper method to send a payload, handling backwards compatibility and retrying.
        Result URIClient::SendURIPayload(
            const SizedPayloadContainer& container,
            uint32 timeoutInMs,
            uint32 retryInMs)
        {
            // Use the legacy size for the container if we're connected to an older client, otherwise use the real size.
            const Version sessionVersion = (m_pSession.IsNull() == false) ? m_pSession->GetVersion() : 0;
            const uint32 payloadSize = (sessionVersion >= URI_POST_PROTOCOL_VERSION) ? container.payloadSize
                                                                                     : kLegacyMaxSize;

            return SendSizedPayload(container.payload,
                                    payloadSize,
                                    timeoutInMs,
                                    retryInMs);
        }

        // ============================================================================================================
        // Helper method to handle receiving a payload from a SizedPayloadContainer, including retrying if busy.
        Result URIClient::ReceiveURIPayload(
            SizedPayloadContainer* pContainer,
            uint32 timeoutInMs,
            uint32 retryInMs)
        {
            return ReceiveSizedPayload(pContainer->payload,
                                       sizeof(pContainer->payload),
                                       &pContainer->payloadSize,
                                       timeoutInMs,
                                       retryInMs);
        }

        // ============================================================================================================
        // Helper method to send and then receive using a SizedPayloadContainer object.
        Result URIClient::TransactURIPayload(
            SizedPayloadContainer* pContainer,
            uint32 timeoutInMs,
            uint32 retryInMs)
        {
            Result result = SendURIPayload(*pContainer, timeoutInMs, retryInMs);
            if (result == Result::Success)
            {
                result = ReceiveURIPayload(pContainer, timeoutInMs, retryInMs);
            }
            return result;
        }
    }

} // DevDriver
