
#include "external/rapidjson/rapidjson.h"
#include "external/rapidjson/document.h"
#include "external/rapidjson/error/en.h"
#include "external/rapidjson/stringbuffer.h"
#include "external/rapidjson/writer.h"
#include "external/rapidjson/prettywriter.h"

#include "ViewerClient.h"

#include <iostream>
#include <sstream>
#include <string>

#define WAIT_FOR_DEBUGGER() false

// MCP spec: https://modelcontextprotocol.io/specification/2025-11-25
// json rpc 2.0 is used for MCP servers.
// spec: https://www.jsonrpc.org/specification

std::string g_serverIP = "127.0.0.1";
int g_serverPort = 6161;

CViewerClient g_viewerClient;
int g_id = -1;
bool g_hasId = false;
bool g_wantsContentLength = true;

enum class ErrorCode : int
{
    JSONParseError = 0,
    RootNotObject,
    BadJSONRPC,
    NoMethod,
    UnknownMethod,
    NoTool,
    UnknownTool,
    BadParams,
    Timeout,
    NoServer,
    SendError,
};

void ErrorResponse(ErrorCode errorCode, const char* message)
{
    if (!g_hasId)
        return;

    // Make the response object
    rapidjson::Document d;
    d.SetObject();

    d.AddMember("jsonrpc", "2.0", d.GetAllocator());
    d.AddMember("id", g_id, d.GetAllocator());

    // make the error object and add it
    rapidjson::Value errorObject(rapidjson::kObjectType);
    errorObject.AddMember("code", (int)errorCode, d.GetAllocator());
    errorObject.AddMember("message", rapidjson::StringRef(message), d.GetAllocator());

    d.AddMember("error", errorObject, d.GetAllocator());

    // Make the string
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    d.Accept(writer);
    std::string json_string = buffer.GetString();

    // Print the string
    if (g_wantsContentLength)
        std::cout << "Content-Length: " << json_string.length() << "\n\n" << json_string << std::flush;
    else
        std::cout << json_string << "\n" << std::flush;
}

template <typename DOCUMENT>
void ResultResponse(DOCUMENT& resultObject)
{
    if (!g_hasId)
        return;

    // Make the response object
    rapidjson::Document d;
    d.SetObject();

    d.AddMember("jsonrpc", "2.0", d.GetAllocator());
    d.AddMember("id", g_id, d.GetAllocator());

    // Add the result 
    d.AddMember("result", resultObject, d.GetAllocator());

    // Make the string
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    d.Accept(writer);
    std::string json_string = buffer.GetString();

    // Print the string
    if (g_wantsContentLength)
        std::cout << "Content-Length: " << json_string.length() << "\n\n" << json_string << std::flush;
    else
        std::cout << json_string << "\n" << std::flush;
}

void SuccessResponse()
{
    rapidjson::Document resultObject;
    resultObject.SetObject();
    ResultResponse(resultObject);
}

bool ConnectToServer()
{
    if (g_viewerClient.IsConnected())
        return true;

    // Try to connect
    {
        char portStr[64];
        sprintf_s(portStr, "%i", g_serverPort);
        g_viewerClient.Start(g_serverIP.c_str(), portStr);
    }

    if (g_viewerClient.IsConnected())
        return true;

    {
        char buffer[1024];
        sprintf_s(buffer, "Could not connect to server %s : %i", g_serverIP.c_str(), g_serverPort);
        ErrorResponse(ErrorCode::NoServer, buffer);
        return false;
    }
}

template <typename DOCUMENT>
bool Read(DOCUMENT& document, float& value)
{
    if (document.IsDouble())
        value = (float)document.GetDouble();
    else if (document.IsInt())
        value = (float)document.GetInt();
    else
        return false;

    return true;
}

template <typename DOCUMENT>
bool Read(DOCUMENT& document, bool& value)
{
    if (!document.IsBool())
        return false;

    value = document.GetBool();

    return true;
}

template <typename DOCUMENT>
bool Read(DOCUMENT& document, std::string& value)
{
    if (!document.IsString())
        return false;

    value = document.GetString();

    return true;
}

template <typename DOCUMENT>
bool Read(DOCUMENT& document, int& value)
{
    if (!document.IsInt())
        return false;

    value = document.GetInt();

    return true;
}

template <typename DOCUMENT, typename TYPE>
bool Read(DOCUMENT& document, const char* name, TYPE& value)
{
    if (!document.HasMember(name))
        return false;

    return Read(document[name], value);
}

// define the structs that contain the parameters for each MCP tools/call function
#define FUNCTION_BEGIN(NAME, DESCRIPTION) struct Params_##NAME {
#define FUNCTION_BEGIN_MCP(NAME, DESCRIPTION) struct Params_##NAME {
#define PARAMETER(TYPE, NAME, DESCRIPTION) TYPE NAME = {};
#define PARAMETER_MCP(TYPE, NAME, DESCRIPTION) TYPE NAME = {};
#define FUNCTION_END() };
#define FUNCTION_END_MCP() };
#include "GigiViewerDX12/ViewerPythonFunctionList.h"

// Define the functions that fill out those structs, using the primitive Read() functions above
#define FUNCTION_BEGIN(NAME, DESCRIPTION) \
    template <typename DOCUMENT> \
    bool Read(DOCUMENT& document, const char* objectName, Params_##NAME& value, std::string& errorString) \
    { \
        bool firstParam = true; \
        rapidjson::Value* sourceObject = nullptr;

#define FUNCTION_BEGIN_MCP(NAME, DESCRIPTION) \
    template <typename DOCUMENT> \
    bool Read(DOCUMENT& document, const char* objectName, Params_##NAME& value, std::string& errorString) \
    { \
        bool firstParam = true; \
        rapidjson::Value* sourceObject = nullptr;

#define PARAMETER(TYPE, NAME, DESCRIPTION) \
        if (firstParam) \
        { \
            firstParam = false; \
            if (!document.HasMember(objectName)) \
                return false; \
            sourceObject = &document[objectName];\
        } \
        if (!Read(*sourceObject, #NAME, value.NAME)) \
        { \
            errorString = "Could not read " #NAME;\
            return false; \
        }

#define PARAMETER_MCP(TYPE, NAME, DESCRIPTION) \
        if (firstParam) \
        { \
            firstParam = false; \
            if (!document.HasMember(objectName)) \
                return false; \
            sourceObject = &document[objectName];\
        } \
        if (!Read(*sourceObject, #NAME, value.NAME)) \
        { \
            errorString = "Could not read " #NAME;\
            return false; \
        }

#define FUNCTION_END() \
        return true; \
    }

#define FUNCTION_END_MCP() \
        return true; \
    }

#include "GigiViewerDX12/ViewerPythonFunctionList.h"

template <typename T>
bool IsString(const T& v)
{
    return false;
}

bool IsString(const std::string& v)
{
    return true;
}

template <typename T>
std::string EscapeForPython(const T& value)
{
    return "";
}

// Escape a string for use in Python code
std::string EscapeForPython(const std::string& str)
{
    std::string result;
    result.reserve(str.length());
    for (char c : str)
    {
        switch (c)
        {
            case '\\': result += "\\\\"; break;
            case '"':  result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

// Make the ParamsToString functions
#define FUNCTION_BEGIN(NAME, DESCRIPTION) \
    std::string ParamsToString(const Params_##NAME& params) \
    { \
        std::ostringstream ret; \
        bool firstParam = true;

#define FUNCTION_BEGIN_MCP(NAME, DESCRIPTION) \
    std::string ParamsToString(const Params_##NAME& params) \
    { \
        std::ostringstream ret; \
        bool firstParam = true;

#define PARAMETER(TYPE, NAME, DESCRIPTION) \
        if (!firstParam) \
            ret << ", "; \
        firstParam = false; \
        if (IsString(params.NAME)) \
        { \
            ret << "\""; \
            ret << EscapeForPython(params.NAME); \
            ret << "\""; \
        } \
        else \
            ret << params.NAME; \

#define PARAMETER_MCP(TYPE, NAME, DESCRIPTION) \
        if (!firstParam) \
            ret << ", "; \
        firstParam = false; \
        if (IsString(params.NAME)) \
        { \
            ret << "\""; \
            ret << EscapeForPython(params.NAME); \
            ret << "\""; \
        } \
        else \
            ret << params.NAME; \

#define FUNCTION_END() \
        return ret.str(); \
    }

#define FUNCTION_END_MCP() \
        return ret.str(); \
    }

#include "GigiViewerDX12/ViewerPythonFunctionList.h"

template <typename DOCUMENT>
void HandleRequest_initialize(DOCUMENT& document)
{
    rapidjson::Document resultObject;
    resultObject.SetObject();
    resultObject.AddMember("protocolVersion", "2024-11-05", resultObject.GetAllocator());

        rapidjson::Document capabilitiesObject;
        capabilitiesObject.SetObject();

            rapidjson::Document toolsObject;
            toolsObject.SetObject();
            capabilitiesObject.AddMember("tools", toolsObject, resultObject.GetAllocator());

        resultObject.AddMember("capabilities", capabilitiesObject, resultObject.GetAllocator());

        rapidjson::Document serverInfoObject;
        serverInfoObject.SetObject();
        serverInfoObject.AddMember("name", "gigi-mcp", resultObject.GetAllocator());
        serverInfoObject.AddMember("version", "1.0", resultObject.GetAllocator());
        resultObject.AddMember("serverInfo", serverInfoObject, resultObject.GetAllocator());

    ResultResponse(resultObject);
}

template <typename DOCUMENT>
void HandleRequest_initialized(DOCUMENT& document)
{
    SuccessResponse();
}

// json schema data types: string, number, integer, object, array, boolean, null
static const char* GetJSONTypeName(int* dummy)
{
    return "integer";
}

static const char* GetJSONTypeName(std::string* dummy)
{
    return "string";
}

static const char* GetJSONTypeName(bool* dummy)
{
    return "boolean";
}

static const char* GetJSONTypeName(float* dummy)
{
    return "number";
}

template <typename DOCUMENT>
void HandleRequest_tools_list(DOCUMENT& document)
{
    rapidjson::Document resultObject;
    resultObject.SetObject();
    auto& allocator = resultObject.GetAllocator();
    {
        rapidjson::Document toolsArray;
        toolsArray.SetArray();

        #define FUNCTION_BEGIN(NAME, DESCRIPTION) \
        { \
            rapidjson::Document object; \
            object.SetObject(); \
            object.AddMember("name", #NAME, allocator); \
            object.AddMember("description", DESCRIPTION, allocator); \
            { \
                rapidjson::Document inputSchema; \
                inputSchema.SetObject(); \
                inputSchema.AddMember("type", "object", allocator); \
                rapidjson::Document required; \
                required.SetArray(); \
                { \
                    rapidjson::Document properties; \
                    properties.SetObject();

        #define PARAMETER(TYPE, NAME, DESCRIPTION) \
        { \
            rapidjson::Document parameter; \
            parameter.SetObject(); \
            parameter.AddMember("type", rapidjson::StringRef(GetJSONTypeName((TYPE*)nullptr)), allocator); \
            parameter.AddMember("description", DESCRIPTION, allocator); \
            properties.AddMember(#NAME, parameter, allocator); \
            required.PushBack(#NAME, allocator); \
        }

        #define FUNCTION_END() \
                    inputSchema.AddMember("properties", properties, allocator); \
                } \
                inputSchema.AddMember("required", required, allocator); \
                object.AddMember("inputSchema", inputSchema, allocator); \
            } \
            toolsArray.PushBack(object, allocator); \
        }

        #define FUNCTION_BEGIN_MCP(NAME, DESCRIPTION) \
        { \
            rapidjson::Document object; \
            object.SetObject(); \
            object.AddMember("name", #NAME, allocator); \
            object.AddMember("description", DESCRIPTION, allocator); \
            { \
                rapidjson::Document inputSchema; \
                inputSchema.SetObject(); \
                inputSchema.AddMember("type", "object", allocator); \
                rapidjson::Document required; \
                required.SetArray(); \
                { \
                    rapidjson::Document properties; \
                    properties.SetObject();

        #define PARAMETER_MCP(TYPE, NAME, DESCRIPTION) \
        { \
            rapidjson::Document parameter; \
            parameter.SetObject(); \
            parameter.AddMember("type", rapidjson::StringRef(GetJSONTypeName((TYPE*)nullptr)), allocator); \
            parameter.AddMember("description", DESCRIPTION, allocator); \
            properties.AddMember(#NAME, parameter, allocator); \
            required.PushBack(#NAME, allocator); \
        }

        #define FUNCTION_END_MCP() \
                    inputSchema.AddMember("properties", properties, allocator); \
                } \
                inputSchema.AddMember("required", required, allocator); \
                object.AddMember("inputSchema", inputSchema, allocator); \
            } \
            toolsArray.PushBack(object, allocator); \
        }

        #include "GigiViewerDX12/ViewerPythonFunctionList.h"

        resultObject.AddMember("tools", toolsArray, resultObject.GetAllocator());
    }
    ResultResponse(resultObject);
}

bool Send(const char* message)
{
    if (!g_viewerClient.Send(message))
    {
        ErrorResponse(ErrorCode::SendError, "Could not send message to viewer");
        return false;
    }
    else
    {
        return true;
    }
}

std::string GetResponse()
{
    std::string response;
    while (!g_viewerClient.PopMessage(response))
    {
        g_viewerClient.Tick();
        Sleep(1);
    }

    return response;
}

rapidjson::Document GetResponseAsJSONObject()
{
    std::string response = GetResponse();

    rapidjson::Document document;
    rapidjson::ParseResult ok = document.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(response.c_str());
    if (!ok)
    {
        int ijkl = 0;
    }

    return document;
}

// =============================================================== TOOLS BEGIN ===============================================================

// Generic tool handler. Specific tool implementations come below
template <typename T>
void HandleTool(const T& params, const char* pythonString)
{
    // Connect to server
    if (!ConnectToServer())
        return;

    // Send the python
    if (!Send(pythonString))
        return;

    // Get response from server
    std::string response = GetResponse();

    // Make the response:
    // a result object
    //   containing a content array
    //     containing a single object entry
    rapidjson::Document contentItem;
    contentItem.SetObject();
    contentItem.AddMember("type", "text", contentItem.GetAllocator());
    contentItem.AddMember("text", rapidjson::StringRef(response.c_str()), contentItem.GetAllocator());

    rapidjson::Document content;
    content.SetArray();
    content.PushBack(contentItem, content.GetAllocator());

    rapidjson::Document result;
    result.SetObject();
    result.AddMember("content", content, result.GetAllocator());

    // Report response
    ResultResponse(result);

    // Close connection
    g_viewerClient.Shutdown();
}

void HandleTool(const Params_RunPythonString& params, const char* pythonString)
{
    // Connect to server
    if (!ConnectToServer())
        return;

    // Send the python
    if (!Send(params.command.c_str()))
        return;

    // Get response from server
    std::string response = GetResponse();

    // Make the response:
    // a result object
    //   containing a content array
    //     containing a single object entry
    rapidjson::Document contentItem;
    contentItem.SetObject();
    contentItem.AddMember("type", "text", contentItem.GetAllocator());
    contentItem.AddMember("text", rapidjson::StringRef(response.c_str()), contentItem.GetAllocator());

    rapidjson::Document content;
    content.SetArray();
    content.PushBack(contentItem, content.GetAllocator());

    rapidjson::Document result;
    result.SetObject();
    result.AddMember("content", content, result.GetAllocator());

    // Report response
    ResultResponse(result);

    // Close connection
    g_viewerClient.Shutdown();
}

void HandleTool(const Params_ViewerPing& params, const char* pythonString)
{
    // Connect to server
    if (!ConnectToServer())
        return;

    // Report success
    SuccessResponse();

    // Close connection
    g_viewerClient.Shutdown();
}

void HandleTool(const Params_Ping& params, const char* pythonString)
{
    SuccessResponse();
}

void HandleTool(const Params_SetViewerIP& params, const char* pythonString)
{
    g_serverIP = params.IP;
    g_serverPort = params.port;
    SuccessResponse();
}

void HandleTool(const Params_LaunchViewer& params, const char* pythonString)
{
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;

    std::string commandLine = std::string("GigiViewerDX12.exe");

    // Add the port to listen on
    {
        char buffer[256];
        sprintf_s(buffer, " -listenPort %i", params.port);
        commandLine += buffer;
    }

    if (!params.commandLine.empty())
        commandLine = commandLine + std::string(" ") + params.commandLine;

    CreateProcessA(
        nullptr,
        (char*)commandLine.c_str(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi);

    // Wait for the process to finish initializing (max 10 seconds)
    if (WaitForInputIdle(pi.hProcess, 10000) == 0)
        SuccessResponse();
    else
        ErrorResponse(ErrorCode::Timeout, "Timeout waiting for viewer to launch");
}

// =============================================================== TOOLS END ===============================================================

template <typename DOCUMENT>
void HandleRequest_tools_call(DOCUMENT& document)
{
    std::string name;
    if (!Read(document, "name", name))
    {
        ErrorResponse(ErrorCode::NoTool, "Could not read tool name");
        return;
    }

    #define FUNCTION_BEGIN(NAME, DESCRIPTION) \
        if (name == #NAME) \
        { \
            std::string errorString; \
            Params_##NAME params; \
            if (!Read(document, "arguments", params, errorString)) \
                ErrorResponse(ErrorCode::BadParams, errorString.c_str()); \
            else \
            { \
                std::string pythonString = std::string("Host." #NAME "(") + ParamsToString(params) + std::string(")"); \
                HandleTool(params, pythonString.c_str()); \
            } \
            return; \
        }

    #define FUNCTION_BEGIN_MCP(NAME, DESCRIPTION) \
        if (name == #NAME) \
        { \
            std::string errorString; \
            Params_##NAME params; \
            if (!Read(document, "arguments", params, errorString)) \
                ErrorResponse(ErrorCode::BadParams, errorString.c_str()); \
            else \
            { \
                std::string pythonString = std::string("Host." #NAME "(") + ParamsToString(params) + std::string(")"); \
                HandleTool(params, pythonString.c_str()); \
            } \
            return; \
        }

    #include "GigiViewerDX12/ViewerPythonFunctionList.h"

    ErrorResponse(ErrorCode::UnknownTool, "Unknown tool");
}

void HandleRequest(rapidjson::Document& document)
{
    g_hasId = Read(document, "id", g_id);

    if (!document.IsObject())
    {
        ErrorResponse(ErrorCode::RootNotObject, "Root is not an object");
        return;
    }

    std::string jsonrpc;
    if (!Read(document, "jsonrpc", jsonrpc) || jsonrpc != "2.0")
    {
        ErrorResponse(ErrorCode::BadJSONRPC, "Could not read jsonrpc string, or it was not 2.0");
        return;
    }

    std::string method;
    if (!Read(document, "method", method))
    {
        ErrorResponse(ErrorCode::NoMethod, "Could not read method string");
        return;
    }

    rapidjson::Document emptyObject;
    emptyObject.SetObject();

    if (method == "initialize")
        HandleRequest_initialize(document.HasMember("params") ? document["params"] : emptyObject);
    else if (method == "initialized")
        HandleRequest_initialized(document.HasMember("params") ? document["params"] : emptyObject);
    else if (method == "tools/list")
        HandleRequest_tools_list(document.HasMember("params") ? document["params"] : emptyObject);
    else if (method == "tools/call")
        HandleRequest_tools_call(document.HasMember("params") ? document["params"] : emptyObject);
    else
        ErrorResponse(ErrorCode::UnknownMethod, "Unknown method");
}

int main(int argc, char** argv)
{
    std::string line;
    int content_length = 0;

#if 0
    // Useful for testing basic functionality
    {
        Params_SetCameraPos params;
        params.x = 1.0f;
        params.y = 2.0f;
        params.z = 3.0f;
        std::string pythonString = std::string("Host.SetCameraPos(") + ParamsToString(params) + std::string(")");
        HandleTool(params, pythonString.c_str());

        // Or do this for a simpler test
        //ConnectToServer();
        //g_viewerClient.Send("test!");
        //g_viewerClient.Shutdown();
        return 0;
    }
#endif

#if WAIT_FOR_DEBUGGER()
    static bool stopHere = true;
    while (stopHere && !IsDebuggerPresent())
    {
        Sleep(1);
    }
#endif

    while (std::getline(std::cin, line))
    {
        // Check if this line starts with JSON (no Content-Length header)
        if (!line.empty() && line[0] == '{')
        {
            g_wantsContentLength = false;

            // No headers, just raw JSON. Read until we have a complete JSON object.
            std::string content = line;
            int braceCount = 1; // We already have the opening brace

            // Count braces to find the end of the JSON object
            for (size_t i = 1; i < line.length(); i++)
            {
                if (line[i] == '{') braceCount++;
                else if (line[i] == '}') braceCount--;
            }

            // If we haven't found the closing brace, keep reading
            while (braceCount > 0 && std::getline(std::cin, line))
            {
                content += "\n" + line;
                for (char c : line)
                {
                    if (c == '{') braceCount++;
                    else if (c == '}') braceCount--;
                }
            }

            rapidjson::Document document;
            rapidjson::ParseResult ok = document.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(content.c_str());

            if (ok)
            {
                HandleRequest(document);
            }
            else
            {
                // Force a response, even if they didn't ask for one, to make this more visible
                g_id = -1;
                g_hasId = true;
                ErrorResponse(ErrorCode::JSONParseError, "Could not parse json");
            }
        }
        // Parse headers (original behavior with Content-Length)
        else if (line.find("Content-Length:") == 0)
        {
            content_length = std::stoi(line.substr(15));
        }
        else if (line == "\r" || line.empty())
        {
            // End of headers, read content
            if (content_length > 0)
            {
                std::string content(content_length, '\0');
                std::cin.read(&content[0], content_length);

                const char* data = content.data();

                rapidjson::Document document;
                rapidjson::ParseResult ok = document.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(data);

                if (ok)
                {
                    HandleRequest(document);
                }
                else
                {
                    // Force a response, even if they didn't ask for one, to make this more visible
                    g_id = -1;
                    g_hasId = true;
                    ErrorResponse(ErrorCode::JSONParseError, "Could not parse json");
                }

                content_length = 0;
            }
        }
    }

    return 0;
}
