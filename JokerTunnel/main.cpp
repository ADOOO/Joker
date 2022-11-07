#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment(lib, "Ws2_32.lib")
#include "precomp.h"
#include <winsock2.h>
#include <cstring>
#include "urlparse.h"
#include <stdlib.h>
#include <map>
#include <random>
#include "common.h"
#include "thread.h"
#include <future>
#include <functional>
#include <thread>
#include <chrono>

using namespace std;
// Macros.
#define INITIALIZE_HTTP_RESPONSE( resp, status, reason )    \
    do                                                      \
    {                                                       \
        RtlZeroMemory( (resp), sizeof(*(resp)) );           \
        (resp)->StatusCode = (status);                      \
        (resp)->pReason = (reason);                         \
        (resp)->ReasonLength = (USHORT) strlen(reason);     \
    } while (FALSE)

#define ADD_KNOWN_HEADER(Response, HeaderId, RawValue)               \
    do                                                               \
    {                                                                \
        (Response).Headers.KnownHeaders[(HeaderId)].pRawValue =      \
                                                          (RawValue);\
        (Response).Headers.KnownHeaders[(HeaderId)].RawValueLength = \
            (USHORT) strlen(RawValue);                               \
    } while(FALSE)

#define ALLOC_MEM(cb) HeapAlloc(GetProcessHeap(), 0, (cb))

#define FREE_MEM(ptr) HeapFree(GetProcessHeap(), 0, (ptr))

// sessions:
map<string, SOCKET> SESSIONS;
map<string, int> readTimes;
ThreadPool pool; // code from https://github.com/xf-8087/ThreadPool
DWORD SendHttpResponse(
    IN HANDLE        hReqQueue,
    IN PHTTP_REQUEST pRequest,
    IN USHORT        StatusCode,
    IN PSTR          pReason,
    IN PSTR          pEntity
);

DWORD DoReceiveRequests(HANDLE hReqQueue);
DWORD SendHttpPostResponse(IN HANDLE hReqQueue,IN PHTTP_REQUEST pRequest);
SOCKET GetConnection(const wchar_t* host, int port);
DWORD handleConnect(IN HANDLE hReqQueue, IN PHTTP_REQUEST pRequest);
DWORD handleRead(IN HANDLE hReqQueue,IN PHTTP_REQUEST pRequest);
DWORD handleForward(IN HANDLE hReqQueue, IN PHTTP_REQUEST pRequest);
DWORD handleDisConnect(IN HANDLE hReqQueue,IN PHTTP_REQUEST pRequest);
PHTTP_UNKNOWN_HEADER BuildUnknowHeaders(PCSTR status, PCSTR error, HTTP_UNKNOWN_HEADER unknowheaders[2]);


int __cdecl wmain(int argc, wchar_t* argv[]) {
    pool.start(30);

    ULONG           retCode;
    HANDLE          hReqQueue = NULL;
    HTTPAPI_VERSION HttpApiVersion = HTTPAPI_VERSION_1;
    int             UrlAdded = 0;
    WSADATA wsaData;

    if (argc < 2) {
        wprintf(L"%ws: <Url1> [Url2] ... \n", argv[0]);
        return -1;
    }

    int ok = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (ok != 0) {
        printf("WSAStartup error\n");
        return -1;
    }

    // Initialize HTTP Server APIs
    retCode = HttpInitialize(
        HttpApiVersion,
        HTTP_INITIALIZE_SERVER,    // Flags
        NULL                       // Reserved
    );

    if (retCode != NO_ERROR) {
        wprintf(L"HttpInitialize failed with %lu \n", retCode);
        return retCode;
    }

    // Create a Request Queue Handle
    retCode = HttpCreateHttpHandle(
        &hReqQueue,        // Req Queue
        0                  // Reserved
    );

    if (retCode != NO_ERROR) {
        wprintf(L"HttpCreateHttpHandle failed with %lu \n", retCode);
        goto CleanUp;
    }

    // bind add url
    for (int i = 1; i < argc; i++) {
        wprintf(L"listening for requests on the following url: %s\n", argv[i]);
        retCode = HttpAddUrl(
            hReqQueue,    // Req Queue
            argv[i],      // Fully qualified URL
            NULL          // Reserved
        );
        if (retCode != NO_ERROR) {
            wprintf(L"HttpAddUrl failed with %lu \n", retCode);
            goto CleanUp;
        } else {
            // Track the currently added URLs.
            UrlAdded++;
        }
    }
    
    // main function:
    DoReceiveRequests(hReqQueue);

CleanUp:

    // Call HttpRemoveUrl for all added URLs.
    for (int i = 1; i <= UrlAdded; i++) {
        HttpRemoveUrl(
            hReqQueue,     // Req Queue
            argv[i]        // Fully qualified URL
        );
    }

    // Close the Request Queue handle.
    if (hReqQueue) CloseHandle(hReqQueue);
    // Call HttpTerminate.
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    return retCode;
}

DWORD HandleError(
    IN HANDLE        hReqQueue,
    IN PHTTP_REQUEST pRequest,
    IN USHORT        StatusCode,
    IN PSTR          pReason,
    IN PSTR          errorCause
) {
    HTTP_RESPONSE   response;
    HTTP_DATA_CHUNK dataChunk;
    DWORD           result;
    DWORD           bytesSent;


    // Initialize the HTTP response structure.
    INITIALIZE_HTTP_RESPONSE(&response, StatusCode, pReason);

    // Add a known header.
    ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");
    HTTP_UNKNOWN_HEADER headers[2];
    response.Headers.pUnknownHeaders = BuildUnknowHeaders("notOK", errorCause, headers);
    response.Headers.UnknownHeaderCount = 2;

    // Add an entity chunk.
    dataChunk.DataChunkType = HttpDataChunkFromMemory;
    dataChunk.FromMemory.pBuffer = errorCause;
    dataChunk.FromMemory.BufferLength = strlen(errorCause);
    response.EntityChunkCount = 1;
    response.pEntityChunks = &dataChunk;

    // Because the entity body is sent in one call, it is not
    // required to specify the Content-Length.
    result = HttpSendHttpResponse(
        hReqQueue,           // ReqQueueHandle
        pRequest->RequestId, // Request ID
        0,                   // Flags
        &response,           // HTTP response
        NULL,                // pReserved1
        &bytesSent,          // bytes sent  (OPTIONAL)
        NULL,                // pReserved2  (must be NULL)
        0,                   // Reserved3   (must be 0)
        NULL,                // LPOVERLAPPED(OPTIONAL)
        NULL                 // pReserved4  (must be NULL)
    );

    if (result != NO_ERROR)
        wprintf(L"HttpSendHttpResponse failed with %lu \n", result);
    return result;
}


DWORD DoReceiveRequests(IN HANDLE hReqQueue) {
    ULONG              result;
    HTTP_REQUEST_ID    requestId;
    DWORD              bytesRead;
    PHTTP_REQUEST      pRequest;
    PCHAR              pRequestBuffer;
    ULONG              RequestBufferLength;

    //
    // Allocate a 2 KB buffer. This size should work for most 
    // requests. The buffer size can be increased if required. Space
    // is also required for an HTTP_REQUEST structure.
    //
    RequestBufferLength = sizeof(HTTP_REQUEST) + 2048;
    pRequestBuffer = (PCHAR)ALLOC_MEM(RequestBufferLength);

    if (pRequestBuffer == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    pRequest = (PHTTP_REQUEST)pRequestBuffer;

    // Wait for a new request. This is indicated by a NULL 
    // request ID.
    HTTP_SET_NULL_ID(&requestId);

    for (;;) {
        RtlZeroMemory(pRequest, RequestBufferLength);
        result = HttpReceiveHttpRequest(
            hReqQueue,          // Req Queue
            requestId,          // Req ID
            0,                  // Flags
            pRequest,           // HTTP request buffer
            RequestBufferLength,// req buffer length
            &bytesRead,         // bytes received
            NULL                // LPOVERLAPPED
        );
        std::future<DWORD> res;
        if (NO_ERROR == result) {
            // Worked! 
            switch (pRequest->Verb)
            {
            case HttpVerbGET:
                //wprintf(L"Got a GET request for %ws \n", pRequest->CookedUrl.pFullUrl);
                result = SendHttpResponse(
                    hReqQueue,
                    pRequest,
                    200,
                    "OK",
                    "Georg says, 'All seems fine'\r\n"
                );
                break;
            case HttpVerbPOST:
                //wprintf(L"Got a POST request for %ws \n", pRequest->CookedUrl.pFullUrl);
                switch (parseCmd(pRequest->CookedUrl)) {
                case 1:
                    res = pool.submit(handleConnect, hReqQueue, pRequest);
                    result = res.get();
                    //result = handleConnect(hReqQueue, pRequest);
                    break;
                case 2:
                    res = pool.submit(handleDisConnect, hReqQueue, pRequest);
                    result = res.get();
                    //result = handleDisConnect(hReqQueue, pRequest);
                    break;
                case 3:
                    res = pool.submit(handleForward, hReqQueue, pRequest);
                    result = res.get();
                    //result = handleForward(hReqQueue, pRequest);
                    break;
                case 4:
                    res = pool.submit(handleRead, hReqQueue, pRequest);
                    result = res.get();
                    //result = handleRead(hReqQueue, pRequest);
                    break;
                default:
                    result = HandleError(hReqQueue, pRequest, 200, "cmdNotMatch", "cmd not match");
                    break;
                }
                break;
            default:
                result = HandleError(hReqQueue, pRequest, 503, "Not Implemented", "Method Not Implemented");
                wprintf(L"Got a unknown request for %ws \n", pRequest->CookedUrl.pFullUrl);
                break;
            }

            // TODO: 测试暂时注释
            //if (result != NO_ERROR) {
            //    break;
            //}

            // Reset the Request ID to handle the next request.
            HTTP_SET_NULL_ID(&requestId);
        }
        else if (result == ERROR_MORE_DATA)
        {
            //
            // The input buffer was too small to hold the request
            // headers. Increase the buffer size and call the 
            // API again. 
            //
            // When calling the API again, handle the request
            // that failed by passing a RequestID.
            //
            // This RequestID is read from the old buffer.
            //
            requestId = pRequest->RequestId;

            // Free the old buffer and allocate a new buffer.
            RequestBufferLength = bytesRead;
            FREE_MEM(pRequestBuffer);
            pRequestBuffer = (PCHAR)ALLOC_MEM(RequestBufferLength);

            if (pRequestBuffer == NULL)
            {
                result = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }

            pRequest = (PHTTP_REQUEST)pRequestBuffer;

        }
        else if (ERROR_CONNECTION_INVALID == result &&
            !HTTP_IS_NULL_ID(&requestId))
        {
            // The TCP connection was corrupted by the peer when
            // attempting to handle a request with more buffer. 
            // Continue to the next request.
            HTTP_SET_NULL_ID(&requestId);
        } else {
            break;
        }
    }

    if (pRequestBuffer)
        FREE_MEM(pRequestBuffer);

    return result;
}


DWORD SendHttpResponse(
    IN HANDLE        hReqQueue,
    IN PHTTP_REQUEST pRequest,
    IN USHORT        StatusCode,
    IN PSTR          pReason,
    IN PSTR          pEntityString
)
{
    HTTP_RESPONSE   response;
    HTTP_DATA_CHUNK dataChunk;
    DWORD           result;
    DWORD           bytesSent;

    // Initialize the HTTP response structure.
    INITIALIZE_HTTP_RESPONSE(&response, StatusCode, pReason);

    // Add a known header.
    ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");

    if (pEntityString)
    {
        // Add an entity chunk.
        dataChunk.DataChunkType = HttpDataChunkFromMemory;
        dataChunk.FromMemory.pBuffer = pEntityString;
        dataChunk.FromMemory.BufferLength =
            (ULONG)strlen(pEntityString);

        response.EntityChunkCount = 1;
        response.pEntityChunks = &dataChunk;
    }

    HTTP_UNKNOWN_HEADER headers[2];
    response.Headers.pUnknownHeaders = BuildUnknowHeaders("OK", "NOERROR", headers);
    response.Headers.UnknownHeaderCount = 2;

    // 
    // Because the entity body is sent in one call, it is not
    // required to specify the Content-Length.
    //

    result = HttpSendHttpResponse(
        hReqQueue,           // ReqQueueHandle
        pRequest->RequestId, // Request ID
        0,                   // Flags
        &response,           // HTTP response
        NULL,                // pReserved1
        &bytesSent,          // bytes sent  (OPTIONAL)
        NULL,                // pReserved2  (must be NULL)
        0,                   // Reserved3   (must be 0)
        NULL,                // LPOVERLAPPED(OPTIONAL)
        NULL                 // pReserved4  (must be NULL)
    );

    if (result != NO_ERROR)
        wprintf(L"HttpSendHttpResponse failed with %lu \n", result);

    return result;
}

#define MAX_ULONG_STR ((ULONG) sizeof("4294967295"))


// CONNECT
DWORD handleConnect(
    IN HANDLE        hReqQueue,
    IN PHTTP_REQUEST pRequest
) {
    wstring host = parseTarget(pRequest->CookedUrl);
    wstring port = parsePort(pRequest->CookedUrl);
    if (wcslen(host.c_str()) == 0 || wcslen(port.c_str()) == 0)
        return HandleError(hReqQueue, pRequest, 200, "OK", "invalid host:port");

    SOCKET target = GetConnection(host.c_str(), stoi(port.c_str()));
    if (target == NULL)
        return HandleError(hReqQueue, pRequest, 200, "OK", "connect to host error");
    
    // 设置非阻塞模式
    BOOL nouse = true;
    ioctlsocket(target, FIONBIO, (unsigned long*)(&nouse));
    
    // 生成一个sessionID,默认随机32位
    string sessionID = generateRandomId();
    SESSIONS[sessionID] = target;
    
    // build response.
    HTTP_RESPONSE   response;
    DWORD           result;
    DWORD           bytesSent;
    // Initialize the HTTP response structure.
    INITIALIZE_HTTP_RESPONSE(&response, 200, "OK");
    // Add a known header.
    ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");
    // Cookie
    ADD_KNOWN_HEADER(response, HttpHeaderSetCookie, sessionID.c_str());
    //response.Headers.pUnknownHeaders = unknowheaders;
    HTTP_UNKNOWN_HEADER unknowheaders[2];
    response.Headers.pUnknownHeaders = BuildUnknowHeaders("OK", "NOERROR", unknowheaders);
    response.Headers.UnknownHeaderCount = 2;
    result = HttpSendHttpResponse(
        hReqQueue,           // ReqQueueHandle
        pRequest->RequestId, // Request ID
        0,                   // Flags
        &response,           // HTTP response
        NULL,                // pReserved1
        &bytesSent,          // bytes sent  (OPTIONAL)
        NULL,                // pReserved2  (must be NULL)
        0,                   // Reserved3   (must be 0)
        NULL,                // LPOVERLAPPED(OPTIONAL)
        NULL                 // pReserved4  (must be NULL)
    );

    if (result != NO_ERROR) 
        wprintf(L"HttpSendHttpResponse failed with %lu \n", result);
    return result;
}
// DISCONNECT
DWORD handleDisConnect(
    IN HANDLE        hReqQueue,
    IN PHTTP_REQUEST pRequest
) {
    HTTP_KNOWN_HEADER sessionHeader = pRequest->Headers.KnownHeaders[HttpHeaderCookie];
    // Cookie不存在
    if (sessionHeader.RawValueLength == 0)
        return HandleError(hReqQueue, pRequest, 200, "OK", "no session found");
    string sessionID = sessionHeader.pRawValue;
    // session 无效
    if (SESSIONS.count(sessionID) == 0)
        return HandleError(hReqQueue, pRequest, 200, "OK", "invalid session");
    
    SOCKET sessionSocket = SESSIONS[sessionID];
    // 删除元素
    SESSIONS.erase(sessionID); 
    closesocket(sessionSocket);
    // ok
    HTTP_RESPONSE   response;
    DWORD           result;
    DWORD           bytesSent;
    // Initialize the HTTP response structure.
    INITIALIZE_HTTP_RESPONSE(&response, 200, "OK");
    ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");
    HTTP_UNKNOWN_HEADER headers[2];
    response.Headers.pUnknownHeaders = BuildUnknowHeaders("OK", "NOERROR", headers);
    response.Headers.UnknownHeaderCount = 2;

    result = HttpSendHttpResponse(
        hReqQueue,           // ReqQueueHandle
        pRequest->RequestId, // Request ID
        0,                   // Flags
        &response,           // HTTP response
        NULL,                // pReserved1
        &bytesSent,          // bytes sent  (OPTIONAL)
        NULL,                // pReserved2  (must be NULL)
        0,                   // Reserved3   (must be 0)
        NULL,                // LPOVERLAPPED(OPTIONAL)
        NULL                 // pReserved4  (must be NULL)
    );

    if (result != NO_ERROR)
        wprintf(L"HttpSendHttpResponse failed with %lu \n", result);
    return result;
}

DWORD handleForward(
    IN HANDLE        hReqQueue,
    IN PHTTP_REQUEST pRequest
) {
    HTTP_RESPONSE   response;
    char*           pEntityBuffer;
    ULONG           EntityBufferLength;
    ULONG           BytesRead;
    DWORD           result;
    DWORD           bytesSent;
    ULONG           TotalBytesRead = 0;
    CHAR            szContentLength[MAX_ULONG_STR];

    HTTP_KNOWN_HEADER sessionHeader = pRequest->Headers.KnownHeaders[HttpHeaderCookie];
    // Cookie不存在
    if (sessionHeader.RawValueLength == 0)
        return HandleError(hReqQueue, pRequest, 200, "OK", "no session found");
    string sessionID = sessionHeader.pRawValue;
    // session 无效
    if (SESSIONS.count(sessionID) == 0)
        return HandleError(hReqQueue, pRequest, 200, "OK", "invalid session");

    SOCKET target = SESSIONS[sessionID];

    // 获取请求body
    EntityBufferLength = 65535;
    pEntityBuffer = (char*)ALLOC_MEM(EntityBufferLength);
    // 下面一部分代码都是参考的demo code.
    if (pEntityBuffer == NULL)
    {
        result = ERROR_NOT_ENOUGH_MEMORY;
        wprintf(L"Insufficient resources \n");
        goto Done;
    }

    // Initialize the HTTP response structure.
    char* tmpBuffer = new char[2048]{ 0 };
    if (tmpBuffer == NULL) {
        if (pEntityBuffer)
            FREE_MEM(pEntityBuffer);
        return  HandleError(hReqQueue, pRequest, 200, "OK", "malloc tmpBuffer error!");
    }
        
    ULONG tmpLength = 2048;
    char* newBuffer = NULL;

    INITIALIZE_HTTP_RESPONSE(&response, 200, "OK");
    if (pRequest->Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS) {
        do
        {
            BytesRead = 0;
            result = HttpReceiveRequestEntityBody(
                hReqQueue,
                pRequest->RequestId,
                0,
                pEntityBuffer,
                EntityBufferLength,
                &BytesRead,
                NULL
            );

            switch (result)
            {
            case NO_ERROR:
                if (BytesRead != 0)
                {
                    TotalBytesRead += BytesRead;
                    if (tmpLength < TotalBytesRead) {
                        newBuffer = (char*)malloc(TotalBytesRead * 2);
                        if (newBuffer == NULL) {
                            if (pEntityBuffer)
                                FREE_MEM(pEntityBuffer);
                            if (tmpBuffer) {
                                delete tmpBuffer;
                                tmpBuffer = NULL;
                            }
                            return  HandleError(hReqQueue, pRequest, 200, "OK", "malloc error!");
                        }
                        ZeroMemory(newBuffer, TotalBytesRead * 2);
                        CopyMemory(newBuffer, tmpBuffer, tmpLength);
                        CopyMemory(newBuffer + strlen(tmpBuffer), pEntityBuffer, BytesRead);
                        delete(tmpBuffer);
                        tmpBuffer = newBuffer;
                        tmpLength = TotalBytesRead * 2;
                    }
                    else {
                        CopyMemory(tmpBuffer + strlen(tmpBuffer), pEntityBuffer, BytesRead);
                    }
                }
                break;
            case ERROR_HANDLE_EOF:
                if (BytesRead != 0)
                {
                    if (tmpLength < TotalBytesRead) {
                        char* newBuffer = (char*)malloc(TotalBytesRead * 2);
                        CopyMemory(newBuffer, tmpBuffer, tmpLength);
                        CopyMemory(newBuffer + strlen(tmpBuffer), pEntityBuffer, BytesRead);
                        delete tmpBuffer;
                        tmpBuffer = newBuffer;
                        tmpLength = TotalBytesRead * 2;
                    }
                    else {
                        CopyMemory(tmpBuffer + strlen(tmpBuffer), pEntityBuffer, BytesRead);
                    }
                }
                sprintf_s(szContentLength, MAX_ULONG_STR, "%lu", TotalBytesRead);
                // 接收完毕,先处理数据再返回success.
                // 处理数据,发送给target
                // data: hTempFile, length: TotalBytesRead
                char* ResultData;
                ResultData = NULL;
                ResultData = new char[TotalBytesRead + 1];
                // 一次性全部发送,不知道会不会有坑
                BOOL ok;
                ok = send(target, tmpBuffer, TotalBytesRead, 0);
                printf("send to remote:\n%s", tmpBuffer);
                // 返回success
                ADD_KNOWN_HEADER(
                    response,
                    HttpHeaderContentLength,
                    szContentLength
                );
                HTTP_UNKNOWN_HEADER headers[2];
                response.Headers.pUnknownHeaders = BuildUnknowHeaders("OK", "NOERROR", headers);
                result =
                    HttpSendHttpResponse(
                        hReqQueue,           // ReqQueueHandle
                        pRequest->RequestId, // Request ID
                        HTTP_SEND_RESPONSE_FLAG_MORE_DATA,
                        &response,       // HTTP response
                        NULL,            // pReserved1
                        &bytesSent,      // bytes sent-optional
                        NULL,            // pReserved2
                        0,               // Reserved3
                        NULL,            // LPOVERLAPPED
                        NULL             // pReserved4
                    );
                
                if (result != NO_ERROR)
                {
                    wprintf(
                        L"[forward]HttpSendHttpResponse failed with %lu \n",
                        result
                    );
                }
                return result;
                break;
            default:
                wprintf(
                    L"HttpReceiveRequestEntityBody failed with %lu \n",
                    result);
                goto Done;
            }

        } while (TRUE);
    }
    else
    {
        // This request does not have an entity body.
        result = HttpSendHttpResponse(
            hReqQueue,           // ReqQueueHandle
            pRequest->RequestId, // Request ID
            0,
            &response,           // HTTP response
            NULL,                // pReserved1
            &bytesSent,          // bytes sent (optional)
            NULL,                // pReserved2
            0,                   // Reserved3
            NULL,                // LPOVERLAPPED
            NULL                 // pReserved4
        );
        if (result != NO_ERROR)
        {
            wprintf(L"[forward]HttpSendHttpResponse failed with %lu \n",
                result);
        }
    }

Done:

    if (pEntityBuffer)
        FREE_MEM(pEntityBuffer);

    // 加下面四行编译不通过.
    //if (newBuffer)
    //    free(newBuffer);
    //if (tmpBuffer) 
    //    delete tmpBuffer;

    return HandleError(hReqQueue, pRequest, 200, "OK", "error done");
}

DWORD handleRead(
    IN HANDLE        hReqQueue,
    IN PHTTP_REQUEST pRequest
) {
    HTTP_KNOWN_HEADER sessionHeader = pRequest->Headers.KnownHeaders[HttpHeaderCookie];
    // Cookie不存在
    if (sessionHeader.RawValueLength == 0)
        return HandleError(hReqQueue, pRequest, 200, "OK", "no session found");
    string sessionID = sessionHeader.pRawValue;
    // session 无效
    if (SESSIONS.count(sessionID) == 0)
        return HandleError(hReqQueue, pRequest, 200, "OK", "invalid session");

    SOCKET target = SESSIONS[sessionID];
    HTTP_DATA_CHUNK dataChunks;
    int count = 0;
    ULONG64 bytesRead = 0;
    char buf[1024] = { 0 };
    char* outbuf = NULL;

    dataChunks.DataChunkType = HttpDataChunkFromMemory;
    dataChunks.FromMemory.BufferLength = 0;
    dataChunks.FromMemory.pBuffer = NULL;
    
    if (readTimes.count(sessionID) == 0)
        readTimes[sessionID] = 0;
    while (1) {
        int len = recv(target, buf, 1024, 0);
        if (len == 0) {
            if (outbuf)
                delete outbuf;
            return HandleError(hReqQueue, pRequest, 200, "NOTOK", "connection has been closed");;
            break;
        }
        else if (len == SOCKET_ERROR) {
            int errorCode = WSAGetLastError();
            printf("errorcode::%d, unreadTime::%d", errorCode, readTimes[sessionID]);
            if (errorCode != 10035) {
                if (outbuf)
                    delete outbuf;
                return HandleError(hReqQueue, pRequest, 200, "NOTOK", "connection has been closed");
            }
               
            if (readTimes[sessionID] > 15) {
                if (outbuf)
                    delete outbuf;
                return HandleError(hReqQueue, pRequest, 200, "NOTOK", "connection has been closed");
            }
            readTimes[sessionID]++;
            break;
        }
        else {
            readTimes[sessionID] = 0;
            bytesRead += len;
            outbuf = new char[dataChunks.FromMemory.BufferLength+len];
            ZeroMemory(outbuf, dataChunks.FromMemory.BufferLength + len);
            memcpy(outbuf, dataChunks.FromMemory.pBuffer, dataChunks.FromMemory.BufferLength);
            memcpy(outbuf + dataChunks.FromMemory.BufferLength, buf, len);
            delete dataChunks.FromMemory.pBuffer;
            dataChunks.FromMemory.pBuffer = outbuf;
            dataChunks.FromMemory.BufferLength += len;
        }
    }

    if (bytesRead == 0) {
        int result = SendHttpResponse(
            hReqQueue,
            pRequest,
            200,
            "OK",
            NULL
        );
        return result;
    }

    HTTP_RESPONSE   response;
    DWORD           result;
    DWORD           bytesSent;

    // Initialize the HTTP response structure.
    INITIALIZE_HTTP_RESPONSE(&response, 200, "OK");

    // Add a known header.
    //ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");
    HTTP_UNKNOWN_HEADER headers[2];
    response.Headers.pUnknownHeaders = BuildUnknowHeaders("OK", "NOERROR", headers);
    response.Headers.UnknownHeaderCount = 2;

    // 不要忘记下面的两行,会变得不幸 :(
    dataChunks.DataChunkType = HttpDataChunkFromMemory;
    response.EntityChunkCount = 0;
    response.pEntityChunks = &dataChunks;
    string len = to_string(bytesRead);
    ADD_KNOWN_HEADER(response, HttpHeaderContentLength, len.c_str());
    result =
        HttpSendHttpResponse(
            hReqQueue,           // ReqQueueHandle
            pRequest->RequestId, // Request ID
            HTTP_SEND_RESPONSE_FLAG_MORE_DATA,
            &response,       // HTTP response
            NULL,            // pReserved1
            &bytesSent,      // bytes sent-optional
            NULL,            // pReserved2
            0,               // Reserved3
            NULL,            // LPOVERLAPPED
            NULL             // pReserved4
        );

    // send header
    if (result != NO_ERROR)
    {
        if (outbuf) delete outbuf;
        wprintf(L"[read]HttpSendHttpResponse send header failed with %lu \n", result);
        return result;
    }
    //printf("\n%s", dataChunks.FromMemory.pBuffer);
    //dataChunks.FromMemory.BufferLength = (ULONG)strlen((char*)dataChunks.FromMemory.pBuffer);
    dataChunks.FromMemory.BufferLength = bytesRead;
    // send chunk
    result = HttpSendResponseEntityBody(
        hReqQueue,
        pRequest->RequestId,
        0,           // This is the last send.
        1,           // Entity Chunk Count.
        &dataChunks,
        NULL,
        NULL,
        0,
        NULL,
        NULL
    );
    if (result != NO_ERROR)
    {
        if (outbuf) delete outbuf;
        wprintf(L"[read]HttpSendResponseEntityBody failed %lu\n",result);
    }
    return result;
}


SOCKET GetConnection(const wchar_t* host, int port) {
    // TODO: 判断是否是domain, 解析ip.
    // 发现直接传ip进去也没事
    // TODO: bug: 类似baidu这种gethostbyname返回的还是域名,暂未处理.
    struct hostent* host_info;
    struct in_addr addr;

    // wchar_t* -> string : https://stackoverflow.com/questions/27720553/conversion-of-wchar-t-to-string
    wstring ws = host;
    string hostname(ws.begin(), ws.end());
    host_info = gethostbyname(hostname.c_str());
    if (host_info == NULL) {
        return NULL;
    }
    addr.s_addr = *(u_long*) host_info->h_addr_list[0]; // 取第一个地址
    SOCKET sclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sclient == INVALID_SOCKET)
        return NULL;
    sockaddr_in serAddr;
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(port);

    serAddr.sin_addr.S_un.S_addr = addr.s_addr;
    if (connect(sclient, (sockaddr*)&serAddr, sizeof(serAddr)) == SOCKET_ERROR)
    {
        closesocket(sclient);
        return NULL;
    }
    return sclient;
}

PHTTP_UNKNOWN_HEADER BuildUnknowHeaders(PCSTR status, PCSTR error, HTTP_UNKNOWN_HEADER unknowheaders[2]) {
    unknowheaders[0].NameLength = strlen("X-STATUS");
    unknowheaders[0].pName = "X-STATUS";
    unknowheaders[0].pRawValue = status;
    unknowheaders[0].RawValueLength = strlen(status);
    unknowheaders[1].NameLength = strlen("X-ERROR");
    unknowheaders[1].pName = "X-ERROR";
    unknowheaders[1].pRawValue = error;
    unknowheaders[1].RawValueLength = strlen(error);
    return unknowheaders;
}