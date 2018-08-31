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
/**
***********************************************************************************************************************
* @file  ddUriInterface.h
* @brief Declaration for public URI interfaces. Contains the minimal interface to implement a URIService.
***********************************************************************************************************************
*/

#pragma once

#include "gpuopen.h"
#include "ddPlatform.h"
#include "util/sharedptr.h"

namespace DevDriver
{
    namespace TransferProtocol
    {
        class ServerBlock;
    }

    // The maximum allowed name for a service name
    DD_STATIC_CONST size_t kMaxUriServiceNameLength = 128;

    enum struct URIDataFormat : uint32
    {
        Unknown = 0,
        Text,
        Binary,
        Count
    };

#if DD_VERSION_SUPPORTS(GPUOPEN_URIINTERFACE_CLEANUP_VERSION)
    // An interface to write bytes.
    class IByteWriter
    {
    protected:
        virtual ~IByteWriter() {}

    public:
        // Finish all writing and return the last error.
        virtual Result End() = 0;

        // Write exactly `length` bytes.
        virtual void WriteBytes(const void* pBytes, size_t length) = 0;

        // Write a value as a byte array.
        // N.B.: Be mindful of your struct's implicit padding!
        template <typename T>
        void Write(const T& value)
        {
            WriteBytes(&value, sizeof(value));
        }

        // Explicitly prevent copying pointers.
        // If you want to write the address, use an appropriately sized integer type, like std::ptrdiff_t.
        // If you want to write the data at the address, provide a length.
        // Be careful of member function pointers, they're probably not the size you think they are.
        //      https://blogs.msdn.microsoft.com/oldnewthing/20040209-00/?p=40713
        template <typename T>
        void Write(const T* value) = delete;
    };

    // An interface to write and validate text.
    class ITextWriter
    {
    protected:
        virtual ~ITextWriter() {}

    public:
        // Finish all writing and return the last error.
        virtual Result End() = 0;

        // Write formatted text.
        // Try and only pass string literals as `pFmt`. Prefer: Write("%s", myGeneratedBuffer);
        virtual void Write(const char* pFmt, ...) = 0;

        // Write specific types
        virtual void Write(uint64 value) = 0;
        virtual void Write(uint32 value) = 0;
        virtual void Write(uint16 value) = 0;
        virtual void Write(uint8  value) = 0;
        virtual void Write(int64  value) = 0;
        virtual void Write(int32  value) = 0;
        virtual void Write(int16  value) = 0;
        virtual void Write(double value) = 0;
        virtual void Write(float  value) = 0;
        virtual void Write(bool   value) = 0;
        virtual void Write(char   value) = 0;
    };

    // An interface to write and validate structured data - e.g. json or message pack
    class IStructuredWriter
    {
    protected:
        virtual ~IStructuredWriter() {}

    public:
        // Finish all writing and return the last error.
        virtual Result End() = 0;

        // Structured data is often nullable.
        // Write a "null" value.
        virtual void ValueNull() = 0;

        // ===== Collection Writers ====================================================================================

        // Begin writing a new list collection.
        virtual void BeginList() = 0;

        // End the current list collection.
        virtual void EndList() = 0;

        // Begin writing a new map collection.
        virtual void BeginMap() = 0;

        // End the current map collection.
        virtual void EndMap() = 0;

        // Write a key into a map.
        virtual void Key(const char* pKey) = 0;

        // ===== Value Writers =========================================================================================

        virtual void Value(const char* pValue) = 0;

        virtual void Value(uint64 value) = 0;
        virtual void Value(uint32 value) = 0;
        virtual void Value(uint16 value) = 0;
        virtual void Value(uint8  value) = 0;
        virtual void Value(int64  value) = 0;
        virtual void Value(int32  value) = 0;
        virtual void Value(int16  value) = 0;
        virtual void Value(int8   value) = 0;
        virtual void Value(double value) = 0;
        virtual void Value(float  value) = 0;
        virtual void Value(bool   value) = 0;
        virtual void Value(char   value) = 0;

        // ===== Key + Value Writers ===================================================================================

        // Write a key-value pair where the value will be a list.
        void KeyAndBeginList(const char* pKey) { Key(pKey); BeginList(); }

        // Write a key-value pair where the value will be a map.
        void KeyAndBeginMap(const char* pKey)  { Key(pKey); BeginMap(); }

        // Write a key-value pair.
        void KeyAndValue(const char* pKey, const char* pValue) { Key(pKey); Value(pValue); }
        void KeyAndValue(const char* pKey, uint64      value)  { Key(pKey); Value(value); }
        void KeyAndValue(const char* pKey, uint32      value)  { Key(pKey); Value(value); }
        void KeyAndValue(const char* pKey, int64       value)  { Key(pKey); Value(value); }
        void KeyAndValue(const char* pKey, int32       value)  { Key(pKey); Value(value); }
        void KeyAndValue(const char* pKey, double      value)  { Key(pKey); Value(value); }
        void KeyAndValue(const char* pKey, float       value)  { Key(pKey); Value(value); }
        void KeyAndValue(const char* pKey, bool        value)  { Key(pKey); Value(value); }

        // Write a key-value pair where the value will be a "null" value.
        void KeyAndValueNull(const char* pKey) { Key(pKey); ValueNull(); }
    };

    // An aggregate of the POST metadata for a request.
    struct PostDataInfo
    {
        const void*   pData;  // Immutable view of the post data
        uint32        size;   // Size of the post data in bytes
        URIDataFormat format; // Format of the post data - i.e. how to read it

        // Zero initialize the struct.
        PostDataInfo()
        {
            memset(this, 0, sizeof(*this));
        }
    };

    // An interface that represents a unique URI request
    class IURIRequestContext
    {
    protected:
        virtual ~IURIRequestContext() {}

    public:
        // Retrieve the request argument string
        // N.B: This is non-const and designed to be mutated
        virtual char* GetRequestArguments() = 0;

        // Retrieve information about the post data of this request
        virtual const PostDataInfo& GetPostData() const = 0;

        // Creates and returns a Writer to copy bytes into the response block.
        // Only a single writer is allowed per request context.
        // Returns:
        //    - Result::Rejected if any writer of any type has already been returned
        //    - Result::Error if `ppWriter` is `nullptr`
        virtual Result BeginByteResponse(IByteWriter** ppWriter) = 0;

        // Creates and returns a Writer to copy text into the response block.
        // Only a single writer is allowed per request context.
        // Returns:
        //    - Result::Rejected if any writer of any type has already been returned
        //    - Result::Error if `ppWriter` is `nullptr`
        virtual Result BeginTextResponse(ITextWriter** ppWriter) = 0;

        // Creates and returns a Writer to copy json into the response block.
        // Only a single writer is allowed per request context.
        // Returns:
        //    - Result::Rejected if any writer of any type has already been returned
        //    - Result::Error if `ppWriter` is `nullptr`
        virtual Result BeginJsonResponse(IStructuredWriter** ppWriter) = 0;
    };
#else
    // A struct that represents a unique URI request
    // Deprecated
    struct URIRequestContext
    {
        // Mutable arguments passed to the request
        char* pRequestArguments;

        // Data provided by the client along with the request
        const void* pPostData;

        // Size of the post data pointed to by pPostData
        uint32 postDataSize;

        // The format of the data sent along with the request
        URIDataFormat postDataFormat;

        // A server block to write the response data into.
        SharedPointer<TransferProtocol::ServerBlock> pResponseBlock;

        // The format of the data written into the response block.
        URIDataFormat responseDataFormat;
    };
    // Compatibility typedef
    using IURIRequestContext = URIRequestContext;

#endif

    struct URIResponseHeader
    {
        // The size of the response data in bytes
        size_t responseDataSizeInBytes;

        // The format of the response data
        URIDataFormat responseDataFormat;
    };

    // Base class for URI services
    class IService
    {
    public:
        virtual ~IService() {}

        // Returns the name of the service
        virtual const char* GetName() const = 0;

#if DD_VERSION_SUPPORTS(GPUOPEN_URIINTERFACE_CLEANUP_VERSION)
        // Returns the service version
        virtual Version GetVersion() const = 0;
#else
        // For clients that don't yet support the new URI interface define a default implementation
        // that returns an invalid version.
        DD_STATIC_CONST Version kInvalidVersionNumber = 0xFFFE;
        virtual Version GetVersion() const { return kInvalidVersionNumber; };
#endif

#if DD_VERSION_SUPPORTS(GPUOPEN_URIINTERFACE_CLEANUP_VERSION)
        // Attempts to handle a request from a client
        virtual Result HandleRequest(IURIRequestContext* pContext) = 0;
#elif DD_VERSION_SUPPORTS(GPUOPEN_URI_RESPONSE_FORMATS_VERSION)
        // Attempts to handle a request from a client
        // Deprecated
        virtual Result HandleRequest(URIRequestContext* pContext) = 0;
#else
        // Attempts to handle a request from a client
        // Deprecated
        virtual Result HandleRequest(char*                                        pArguments,
                                     SharedPointer<TransferProtocol::ServerBlock> pBlock)
        {
            DD_UNUSED(pArguments);
            DD_UNUSED(pBlock);
            DD_NOT_IMPLEMENTED();
            return Result::Error;
        }

        // Attempts to handle a request from a client
        virtual Result HandleRequest(URIRequestContext* pContext)
        {
            DD_ASSERT(pContext != nullptr);

            const Result result = HandleRequest(pContext->pRequestArguments,
                                                pContext->pResponseBlock);
            if (result == Result::Success)
            {
                pContext->responseDataFormat = URIDataFormat::Text;
            }

            return result;
        }
#endif

        // Determines the size limit for post data requests for the client request.  By default services
        // will not accept any post data.  The pArguments paramter must remain non-const because the
        // service may need to manipulate it for further processing.
        virtual size_t QueryPostSizeLimit(char* pArguments) const
        {
            DD_UNUSED(pArguments);
            return 0;
        }

    protected:
        IService() {};
    };
} // DevDriver
