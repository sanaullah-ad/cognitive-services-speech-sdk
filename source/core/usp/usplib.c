//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//
// usplib.c: the library provides the client implementation of the speech USP protocol.
//

#include "usp.h"
#include "uspinternal.h"

static int UspEventLoop(UspHandle handle)
{
    UspContext* uspContext = (UspContext *)handle;

    while (1)
    {
        // Todo: deal with concurrency? 
        if (uspContext->flags & USP_FLAG_SHUTDOWN)
        {
            break;
        }

        if (uspContext->flags & USP_FLAG_INITIALIZED)
        {
            UspRun(handle);
        }

        ThreadAPI_Sleep(200);
    }

    return 0;
}

// Todo: seperate UspIntialize into UspOpen, UspSetCallbacks, UspSetOptions...
UspResult UspInitialize(UspHandle* handle, UspCallbacks *callbacks, void* callbackContext)
{
    UspContext* uspContext = (UspContext *)malloc(sizeof(UspContext));
    SPEECH_HANDLE speechHandle;
    // Todo: use configuration data 
    const char endpoint[] = "wss://speech.platform.bing.com/speech/recognition/interactive/cognitiveservices/v1?language=en-us";

    if (handle == NULL)
    {
        return USP_INVALID_PARAMETER;
    }

    if ((callbacks == NULL) || (callbacks->version != USP_VERSION) || (callbacks->size != sizeof(UspCallbacks)))
    {
        return USP_INVALID_PARAMETER;
    }

    telemetry_initialize();
    propertybag_initialize();

    if (speech_open(&speechHandle, 0, NULL))
    {
        LogError("speech_open failed.");
        return USP_INITIALIZATION_FAILURE;
    }

    uspContext->speechContext = (SPEECH_CONTEXT *)speechHandle;
    if (Speech_Initialize(uspContext->speechContext, endpoint))
    {
        LogError("Speech_Initialize failed.");
        speech_close(speechHandle);
        return USP_INITIALIZATION_FAILURE;
    }

    uspContext->callbacks = callbacks;
    uspContext->callbackContext = callbackContext;
    // Todo: remove SPEECH_CONTEXT 
    uspContext->speechContext->uspHandle = (UspHandle)uspContext;

    // Create work thread for processing USP messages.
    if (ThreadAPI_Create(&uspContext->workThreadHandle, UspEventLoop, uspContext) != THREADAPI_OK)
    {
        LogError("Create work thread in USP failed.");
        speech_close(speechHandle);
        return USP_INITIALIZATION_FAILURE;
    }

    uspContext->flags = USP_FLAG_INITIALIZED;

    *handle = (UspHandle)uspContext;
    return USP_SUCCESS;
}

// Pass another parameter to return the bytes have been written.
UspResult UspWrite(UspHandle handle, const uint8_t* buffer, size_t byteToWrite)
{
    UspContext* context = (UspContext *)handle;

    if (context->speechContext == NULL)
    {
        return USP_UNINTIALIZED;
    }

    if (byteToWrite == 0)
    {
        if (audiostream_flush(context->speechContext) != 0)
        {
            return USP_WRITE_ERROR;
        }
    }
    else
    {
        // Todo: mismatch between size_t ad byteToWrite...
        if (audiostream_write(context->speechContext, buffer, (uint32_t)byteToWrite, NULL))
        {
            return USP_WRITE_ERROR;
        }
    }

    return USP_SUCCESS;
}

// Todo: UspClose is better
UspResult UspShutdown(UspHandle handle)
{
    UspContext* context = (UspContext *)handle;
    if (handle == NULL)
    {
        return USP_INVALID_HANDLE;
    }

    context->flags |= USP_FLAG_SHUTDOWN;

    if (context->workThreadHandle != NULL)
    {
        LogInfo("Wait for work thread to complete");
        ThreadAPI_Join(context->workThreadHandle, NULL);
    }

    if (context->speechContext)
    {
        speech_close((SPEECH_HANDLE)context->speechContext);
    }

    free(context);

    propertybag_shutdown();
    telemetry_uninitialize();

    return USP_SUCCESS;
}

// Todo: Hide it into a work thread.
void UspRun(UspHandle handle)
{
    UspContext* uspContext = (UspContext *)handle;

    Lock(uspContext->speechContext->mSpeechRequestLock);

    transport_dowork(uspContext->speechContext->mTransportRequest[0]);

    Unlock(uspContext->speechContext->mSpeechRequestLock);

}

