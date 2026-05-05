///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2024 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

// clang-format off
#include <vector>
#include "ViewerPython.h"
#include <filesystem>
#include "ViewerServer.h"
#include "Schemas/Types.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#define PY_SSIZE_T_CLEAN
#ifdef __unix
  #ifdef _DEBUG
    #undef _DEBUG
    #include <Python.h>
    #include <tupleobject.h>
    #include <bytesobject.h>
    #define _DEBUG
  #else
    #include <Python.h>
    #include <tupleobject.h>
    #include <bytesobject.h>
  #endif
#else
  #ifdef _DEBUG
    #undef _DEBUG
    #include <python.h>
    #include <tupleobject.h>
    #include <bytesobject.h>
    #define _DEBUG 1
  #else
    #include <python.h>
    #include <tupleobject.h>
    #include <bytesobject.h>
  #endif
#endif
// clang-format on

// external functions
void PyInit_GigiArray();
struct _object* PyCreate_GigiArray(const GigiArray& array);

// Forward declaration of python functions. Makes for better error messages when the implementation is missing
#define FUNCTION_BEGIN(NAME, DESCRIPTION) static PyObject* Python_##NAME(PyObject* self, PyObject* args);
#include "ViewerPythonFunctionList.h"

static PythonInterface* g_interface = nullptr;

static std::vector<std::wstring> g_argvText;
static std::vector<wchar_t*> g_argv;

static std::string g_lastCommandResult;
static std::string g_lastCommandError;

static std::string g_lastCommandErrorForPythonFn; // g_lastCommandError is stashed here, so a python function can return it

// Persistent dictionary for PythonExecuteString (REPL mode)
static PyObject* g_persistentDict = nullptr;

static void Base64Decode(const char* inputString, std::vector<unsigned char>& outBytes)
{
    static const unsigned char base64_decode_table[256] = {
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
        64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
        64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
    };

    outBytes.clear();

    size_t inputLen = strlen(inputString);
    size_t outputLen = (inputLen / 4) * 3;

    // Adjust for padding
    if (inputLen > 0 && inputString[inputLen - 1] == '=') outputLen--;
    if (inputLen > 1 && inputString[inputLen - 2] == '=') outputLen--;

    outBytes.reserve(outputLen);

    for (size_t i = 0; i < inputLen; i += 4)
    {
        unsigned char a = base64_decode_table[(unsigned char)inputString[i]];
        unsigned char b = (i + 1 < inputLen) ? base64_decode_table[(unsigned char)inputString[i + 1]] : 0;
        unsigned char c = (i + 2 < inputLen) ? base64_decode_table[(unsigned char)inputString[i + 2]] : 0;
        unsigned char d = (i + 3 < inputLen) ? base64_decode_table[(unsigned char)inputString[i + 3]] : 0;

        outBytes.push_back((a << 2) | (b >> 4));
        if (i + 2 < inputLen && inputString[i + 2] != '=')
            outBytes.push_back((b << 4) | (c >> 2));
        if (i + 3 < inputLen && inputString[i + 3] != '=')
            outBytes.push_back((c << 6) | d);
    }
}

static void Base64Encode(const unsigned char* inputBytes, size_t inputLen, std::string& outString)
{
    static const char base64_encode_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    outString.clear();

    size_t outputLen = ((inputLen + 2) / 3) * 4;
    outString.reserve(outputLen);

    for (size_t i = 0; i < inputLen; i += 3)
    {
        unsigned char b0 = inputBytes[i];
        unsigned char b1 = (i + 1 < inputLen) ? inputBytes[i + 1] : 0;
        unsigned char b2 = (i + 2 < inputLen) ? inputBytes[i + 2] : 0;

        outString.push_back(base64_encode_table[b0 >> 2]);
        outString.push_back(base64_encode_table[((b0 & 0x03) << 4) | (b1 >> 4)]);
        outString.push_back((i + 1 < inputLen) ? base64_encode_table[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
        outString.push_back((i + 2 < inputLen) ? base64_encode_table[b2 & 0x3F] : '=');
    }
}

std::string PyObjectToJsonString(PyObject* obj) {
    // Import json module
    PyObject* jsonModule = PyImport_ImportModule("json");
    if (!jsonModule) {
        PyErr_Print();
        return "";
    }

    // Get json.dumps function
    PyObject* dumpsFunc = PyObject_GetAttrString(jsonModule, "dumps");
    Py_DECREF(jsonModule);

    if (!dumpsFunc || !PyCallable_Check(dumpsFunc)) {
        Py_XDECREF(dumpsFunc);
        return "";
    }

    // Call json.dumps(obj)
    PyObject* jsonStr = PyObject_CallFunctionObjArgs(dumpsFunc, obj, NULL);
    Py_DECREF(dumpsFunc);

    if (!jsonStr) {
        PyErr_Print();
        return "";
    }

    // Convert to C++ string
    const char* str = PyUnicode_AsUTF8(jsonStr);
    std::string result;
    if (str)
        result = str;

    Py_DECREF(jsonStr);
    return result;
}

static bool FileExists(const char* fileName)
{
    FILE* file = nullptr;
    fopen_s(&file, fileName, "rb");
    if (!file)
        return false;

    fclose(file);
    return true;
}

static PyObject* Python_GetViewableResourceList(PyObject* self, PyObject* args)
{
    std::vector<PythonInterface::ViewableResourceInfo> viewableResources = g_interface->GetViewableResourceList();

    PyObject* ret = PyList_New(viewableResources.size());
    for (size_t i = 0; i < viewableResources.size(); ++i)
    {
        const PythonInterface::ViewableResourceInfo& info = viewableResources[i];

        PyObject* resourceDict = PyDict_New();
        PyDict_SetItemString(resourceDict, "type", Py_BuildValue("s", info.type.c_str()));
        PyDict_SetItemString(resourceDict, "viewableResourceName", Py_BuildValue("s", info.displayName.c_str()));

        PyList_SetItem(ret, i, resourceDict);
    }

    return ret;
}

static PyObject* Python_GetVariableList(PyObject* self, PyObject* args)
{
    const RenderGraph& renderGraph = g_interface->GetRenderGraph();

    PyObject* ret = PyList_New(renderGraph.variables.size());
    for (size_t i = 0; i < renderGraph.variables.size(); ++i)
    {
        const Variable& var = renderGraph.variables[i];

        PyObject* varDict = PyDict_New();
        PyDict_SetItemString(varDict, "name", Py_BuildValue("s", var.name.c_str()));
        PyDict_SetItemString(varDict, "comment", Py_BuildValue("s", var.comment.c_str()));
        PyDict_SetItemString(varDict, "type", Py_BuildValue("s", EnumToString(var.type)));
        PyDict_SetItemString(varDict, "visibility", Py_BuildValue("s", EnumToString(var.visibility)));
        PyDict_SetItemString(varDict, "dflt", Py_BuildValue("s", var.dflt.c_str()));
        PyDict_SetItemString(varDict, "Const", PyBool_FromLong(var.Const ? 1 : 0));
        //PyDict_SetItemString(varDict, "Static", PyBool_FromLong(var.Static ? 1 : 0));
        //PyDict_SetItemString(varDict, "transient", PyBool_FromLong(var.transient ? 1 : 0));
        //PyDict_SetItemString(varDict, "system", PyBool_FromLong(var.system ? 1 : 0));
        PyDict_SetItemString(varDict, "Enum", Py_BuildValue("s", var.Enum.c_str()));
        //PyDict_SetItemString(varDict, "UIGroup", Py_BuildValue("s", var.UIGroup.c_str()));
        //PyDict_SetItemString(varDict, "scope", Py_BuildValue("s", var.scope.c_str()));

        std::string variableValue;
        if (g_interface->GetVariable(var.name.c_str(), variableValue))
            PyDict_SetItemString(varDict, "value", Py_BuildValue("s", variableValue.c_str()));

        // Add UI settings
        //PyObject* uiSettings = PyDict_New();
        //PyDict_SetItemString(uiSettings, "UIHint", Py_BuildValue("s", EnumToString(var.UISettings.UIHint)));
        //PyDict_SetItemString(uiSettings, "min", Py_BuildValue("s", var.UISettings.min.c_str()));
        //PyDict_SetItemString(uiSettings, "max", Py_BuildValue("s", var.UISettings.max.c_str()));
        //PyDict_SetItemString(uiSettings, "step", Py_BuildValue("s", var.UISettings.step.c_str()));
        //PyDict_SetItemString(varDict, "UISettings", uiSettings);

        PyList_SetItem(ret, i, varDict);
    }

    return ret;
}

static PyObject* Python_GetResourceList(PyObject* self, PyObject* args)
{
    const RenderGraph& renderGraph = g_interface->GetRenderGraph();

    PyObject* ret = PyList_New(0);

    for (const RenderGraphNode& node : renderGraph.nodes)
    {
        // Only include buffer and texture nodes
        if (node._index != RenderGraphNode::c_index_resourceBuffer &&
            node._index != RenderGraphNode::c_index_resourceTexture)
            continue;

        const RenderGraphNode_ResourceBase* resourceBase = nullptr;
        if (node._index == RenderGraphNode::c_index_resourceBuffer)
            resourceBase = &node.resourceBuffer;
        else
            resourceBase = &node.resourceTexture;

        PyObject* resourceDict = PyDict_New();

        // Common fields for all resources
        PyDict_SetItemString(resourceDict, "name", Py_BuildValue("s", resourceBase->name.c_str()));
        PyDict_SetItemString(resourceDict, "comment", Py_BuildValue("s", resourceBase->comment.c_str()));
        PyDict_SetItemString(resourceDict, "transient", PyBool_FromLong(resourceBase->transient ? 1 : 0));

        // Type-specific fields
        if (node._index == RenderGraphNode::c_index_resourceBuffer)
        {
            PyDict_SetItemString(resourceDict, "type", Py_BuildValue("s", "Buffer"));
            PyDict_SetItemString(resourceDict, "visibility", Py_BuildValue("s", EnumToString(node.resourceBuffer.visibility)));

            // Get runtime information
            RuntimeBufferInfo runtimeInfo = g_interface->GetRuntimeBufferInfo(resourceBase->name.c_str());
            if (runtimeInfo.exists)
            {
                PyObject* runtimeDict = PyDict_New();

                PyDict_SetItemString(runtimeDict, "format", Py_BuildValue("s", runtimeInfo.format.c_str()));
                PyDict_SetItemString(runtimeDict, "formatCount", Py_BuildValue("i", runtimeInfo.formatCount));
                PyDict_SetItemString(runtimeDict, "stride", Py_BuildValue("i", runtimeInfo.stride));
                PyDict_SetItemString(runtimeDict, "size", Py_BuildValue("i", runtimeInfo.size));
                PyDict_SetItemString(runtimeDict, "count", Py_BuildValue("i", runtimeInfo.count));

                // Include struct name if it's a structured buffer
                if (runtimeInfo.structIndex >= 0 && runtimeInfo.structIndex < (int)renderGraph.structs.size())
                {
                    PyDict_SetItemString(runtimeDict, "structType", Py_BuildValue("s", renderGraph.structs[runtimeInfo.structIndex].name.c_str()));
                }

                PyDict_SetItemString(resourceDict, "runtime", runtimeDict);
            }
        }
        else if (node._index == RenderGraphNode::c_index_resourceTexture)
        {
            PyDict_SetItemString(resourceDict, "type", Py_BuildValue("s", "Texture"));
            PyDict_SetItemString(resourceDict, "visibility", Py_BuildValue("s", EnumToString(node.resourceTexture.visibility)));
            PyDict_SetItemString(resourceDict, "dimension", Py_BuildValue("s", EnumToString(node.resourceTexture.dimension)));

            // Get runtime information
            RuntimeTextureInfo runtimeInfo = g_interface->GetRuntimeTextureInfo(resourceBase->name.c_str());
            if (runtimeInfo.exists)
            {
                PyObject* runtimeDict = PyDict_New();

                PyDict_SetItemString(runtimeDict, "format", Py_BuildValue("s", runtimeInfo.format.c_str()));
                PyDict_SetItemString(runtimeDict, "width", Py_BuildValue("i", runtimeInfo.size[0]));
                PyDict_SetItemString(runtimeDict, "height", Py_BuildValue("i", runtimeInfo.size[1]));
                PyDict_SetItemString(runtimeDict, "depth", Py_BuildValue("i", runtimeInfo.size[2]));
                PyDict_SetItemString(runtimeDict, "numMips", Py_BuildValue("i", runtimeInfo.numMips));
                PyDict_SetItemString(runtimeDict, "sampleCount", Py_BuildValue("I", runtimeInfo.sampleCount));

                PyDict_SetItemString(resourceDict, "runtime", runtimeDict);
            }
        }

        PyList_Append(ret, resourceDict);
        Py_DECREF(resourceDict);
    }

    return ret;
}

static PyObject* Python_GetLog(PyObject* self, PyObject* args)
{
    std::string log = g_interface->GetLog();
    return Py_BuildValue("s", log.c_str());
}

static PyObject* Python_ClearLog(PyObject* self, PyObject* args)
{
    g_interface->ClearLog();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_RunPythonFile(PyObject* self, PyObject* args)
{
    const char* fileName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_RunPythonFile", &fileName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    // Resolve relative paths relative to the current script location
    std::filesystem::path fileNamePath = fileName;
    if (fileNamePath.is_relative())
        fileNamePath = std::filesystem::weakly_canonical(std::filesystem::path(g_interface->m_scriptLocation).remove_filename() / fileNamePath);

    if (!FileExists(fileNamePath.string().c_str()))
        return PyErr_Format(PyExc_FileNotFoundError, "File not found: %s", fileNamePath.string().c_str());

    // Read the file
    std::vector<char> fileData;
    {
        FILE* file = nullptr;
        fopen_s(&file, fileNamePath.string().c_str(), "rb");
        if (!file)
            return PyErr_Format(PyExc_IOError, "Could not open file: %s", fileNamePath.string().c_str());

        fseek(file, 0, SEEK_END);
        fileData.resize(ftell(file) + 1);
        fseek(file, 0, SEEK_SET);

        fread(fileData.data(), 1, fileData.size() - 1, file);
        fileData[fileData.size() - 1] = 0;
        fclose(file);
    }

    // Save the current script location and temporarily update it
    std::string previousScriptLocation = g_interface->m_scriptLocation;
    g_interface->m_scriptLocation = fileNamePath.string();

    // Execute the file using the persistent dictionary (so it shares the same namespace as the calling script)
    PyObject* ret = PyRun_String(fileData.data(), Py_file_input, g_persistentDict, g_persistentDict);

    // Restore the previous script location
    g_interface->m_scriptLocation = previousScriptLocation;

    // Check for errors and Let Python's error propagate naturally
    if (ret == nullptr)
        return nullptr;

    Py_DECREF(ret);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_GetLastPythonError(PyObject* self, PyObject* args)
{
    return Py_BuildValue("s", g_lastCommandErrorForPythonFn.c_str());
}

static PyObject* Python_LoadGG(PyObject* self, PyObject* args)
{
    const char* fileName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_LoadGG", &fileName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    // If the filename is relative, try relative to the python script first
    bool success = false;
    std::filesystem::path fileNamePath = fileName;
    if (fileNamePath.is_relative())
    {
        fileNamePath = std::filesystem::weakly_canonical(std::filesystem::path(g_interface->m_scriptLocation).remove_filename() / fileNamePath);
        if (FileExists(fileNamePath.string().c_str()))
            success = g_interface->LoadGG(fileNamePath.string().c_str());
    }

    // If the path wasn't relative, or it wasn't found relative to the python script,
    // try it as given.  If it was relative but not found, this will look for it relative
    // to the viewer exe file location.
    if (!success)
        success = g_interface->LoadGG(fileName);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_Exit(PyObject* self, PyObject* args)
{
    int value = 0;
    if (!PyArg_ParseTuple(args, "|i:Python_Exit", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->RequestExit(value);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetHideUI(PyObject* self, PyObject* args)
{
    int value = 1;
    if (!PyArg_ParseTuple(args, "p:Python_SetHideUI", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetHideUI(value != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetVSync(PyObject* self, PyObject* args)
{
    int value = 1;
    if (!PyArg_ParseTuple(args, "p:Python_SetVSync", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetVSync(value != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetSyncInterval(PyObject* self, PyObject* args)
{
    int value = 1;
    if (!PyArg_ParseTuple(args, "i:Python_SetSyncInterval", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetSyncInterval(value);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetStablePowerState(PyObject* self, PyObject* args)
{
    int value = 1;
    if (!PyArg_ParseTuple(args, "p:Python_SetStablePowerState", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetStablePowerState(value != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetProfilingMode(PyObject* self, PyObject* args)
{
    int value = 1;
    if (!PyArg_ParseTuple(args, "p:Python_SetProfilingMode", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetProfilingMode(value != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetVariable(PyObject* self, PyObject* args)
{
    const char* variableName = nullptr;
    const char* variableValue = nullptr;
    if (!PyArg_ParseTuple(args, "ss:Python_SetVariable", &variableName, &variableValue))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    if (!g_interface->SetVariable(variableName, variableValue))
        return PyErr_Format(PyExc_TypeError, "Could not set variable \"%s\" in " __FUNCTION__ "()", variableName);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_GetScriptPath(PyObject* self, PyObject* args)
{
    std::string ret = std::filesystem::path(g_interface->GetScriptLocation()).remove_filename().string();
    return Py_BuildValue("s", ret.c_str());
}

static PyObject* Python_GetScriptFileName(PyObject* self, PyObject* args)
{
    std::string ret = std::filesystem::path(g_interface->GetScriptLocation()).filename().string();
    return Py_BuildValue("s", ret.c_str());
}

static PyObject* Python_GetVariable(PyObject* self, PyObject* args)
{
    const char* variableName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_GetVariable", &variableName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    std::string variableValue;
    if (!g_interface->GetVariable(variableName, variableValue))
    {
        Py_INCREF(Py_None);
        return Py_None;
    }

    return Py_BuildValue("s", variableValue.c_str());
}

static PyObject* Python_DisableGGUserSave(PyObject* self, PyObject* args)
{
    int value = 1;
    if (!PyArg_ParseTuple(args, "p:Python_DisableGGUserSave", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetDisableGGUserSave(value != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetWantReadback(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    int wantsReadback = 1;
    int autoClear = 1;
    if (!PyArg_ParseTuple(args, "s|pp:Python_SetWantReadback", &viewableResourceName, &wantsReadback, &autoClear))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetWantReadback(viewableResourceName, wantsReadback != 0, autoClear != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_Readback(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    int arrayIndex = 0;
    int mipIndex = 0;
    if (!PyArg_ParseTuple(args, "s|ii:Python_Readback", &viewableResourceName, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    // Get the data
    GigiArray data;
    bool success = g_interface->Readback(viewableResourceName, arrayIndex, mipIndex, data);

    // return the result
    PyObject* ret = PyTuple_New(2);
    PyTuple_SetItem(ret, 0, PyCreate_GigiArray(data));
    PyTuple_SetItem(ret, 1, PyBool_FromLong(success ? 1 : 0));
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_ReadbackBase64(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    int arrayIndex = 0;
    int mipIndex = 0;
    if (!PyArg_ParseTuple(args, "s|ii:Python_ReadbackBase64", &viewableResourceName, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    // Get the data
    GigiArray data;
    bool success = g_interface->Readback(viewableResourceName, arrayIndex, mipIndex, data);

    // base64 encode it
    std::string dataBase64;
    Base64Encode((const unsigned char*)data.data.data(), data.data.size(), dataBase64);

    // return the result as a dictionary
    PyObject* ret = PyDict_New();
    PyDict_SetItemString(ret, "base64", Py_BuildValue("s", dataBase64.c_str()));
    PyDict_SetItemString(ret, "success", PyBool_FromLong(success ? 1 : 0));
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsPNG(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    if (!PyArg_ParseTuple(args, "ss|ii:" __FUNCTION__, &fileName , &viewableResourceName, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsPNG(fileName, viewableResourceName, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsDDS_BC4(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    int signedData = 0;
    if (!PyArg_ParseTuple(args, "ss|pii:" __FUNCTION__, &fileName, &viewableResourceName, &signedData, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsDDS_BC4(fileName, viewableResourceName, signedData, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsDDS_BC5(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    int signedData = 0;
    if (!PyArg_ParseTuple(args, "ss|pii:" __FUNCTION__, &fileName, &viewableResourceName, &signedData, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsDDS_BC5(fileName, viewableResourceName, signedData, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsDDS_BC6(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    int signedData = 0;
    if (!PyArg_ParseTuple(args, "ss|pii:" __FUNCTION__, &fileName, &viewableResourceName, &signedData, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsDDS_BC6(fileName, viewableResourceName, signedData, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsDDS_BC7(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    int sRGB = 1;
    if (!PyArg_ParseTuple(args, "ss|pii:" __FUNCTION__, &fileName, &viewableResourceName, &sRGB, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsDDS_BC7(fileName, viewableResourceName, sRGB, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsEXR(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    if (!PyArg_ParseTuple(args, "ss|ii:" __FUNCTION__, &fileName, &viewableResourceName, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsEXR(fileName, viewableResourceName, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsHDR(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    if (!PyArg_ParseTuple(args, "ss|ii:" __FUNCTION__, &fileName, &viewableResourceName, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsHDR(fileName, viewableResourceName, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsCSV(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    if (!PyArg_ParseTuple(args, "ss|ii:" __FUNCTION__, &fileName, &viewableResourceName, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsCSV(fileName, viewableResourceName, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SaveAsBinary(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* fileName = nullptr;
    int arrayIndex = -1;
    int mipIndex = -1;
    if (!PyArg_ParseTuple(args, "ss|ii:" __FUNCTION__, &fileName, &viewableResourceName, &arrayIndex, &mipIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->SaveAsBinary(fileName, viewableResourceName, arrayIndex, mipIndex);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_RunTechnique(PyObject* self, PyObject* args)
{
    int runCount = 1;
    if (!PyArg_ParseTuple(args, "|i:Python_RunTechnique", &runCount))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->RunTechnique(runCount);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_Log(PyObject* self, PyObject* args)
{
    const char* severity = nullptr;
    const char* message = nullptr;
    if (!PyArg_ParseTuple(args, "ss:Python_Log", &severity, &message))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    LogLevel logLevel = LogLevel::Info;
    if (!_stricmp(severity, "Info"))
        logLevel = LogLevel::Info;
    else if (!_stricmp(severity, "Warn"))
        logLevel = LogLevel::Warn;
    else if (!_stricmp(severity, "Error"))
        logLevel = LogLevel::Error;

    g_interface->Log(logLevel, "Python_Log(): %s", message);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_Print(PyObject* self, PyObject* args)
{
    const char* message = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_Print", &message))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->Log(LogLevel::Info, "Python: %s", message);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_Warn(PyObject* self, PyObject* args)
{
    const char* message = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_Print", &message))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->Log(LogLevel::Warn, "Python: %s", message);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_Error(PyObject* self, PyObject* args)
{
    const char* message = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_Print", &message))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->Log(LogLevel::Error, "Python: %s", message);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetFrameIndex(PyObject* self, PyObject* args)
{
    int frameIndex = 0;
    if (!PyArg_ParseTuple(args, "i:Python_SetFrameIndex", &frameIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetFrameIndex(frameIndex);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_WaitOnGPU(PyObject* self, PyObject* args)
{
    g_interface->WaitOnGPU();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_Pause(PyObject* self, PyObject* args)
{
    int pause = 1;
    if (!PyArg_ParseTuple(args, "p:Python_Pause", &pause))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->Pause(pause != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_PixCaptureNextFrames(PyObject* self, PyObject* args)
{
    const char* fileName = nullptr;
    int frameCount = 1;
    if (!PyArg_ParseTuple(args, "s|i:Python_PixCaptureNextFrames", &fileName, &frameCount))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->PixCaptureNextFrames(fileName, frameCount);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedBufferMaterialShaderFile(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    const char* fileName = nullptr;
    if (!PyArg_ParseTuple(args, "ss:Python_SetImportedBufferMaterialShaderFile", &bufferName, &fileName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedBufferMaterialShaderFile(bufferName, fileName);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedBufferFile(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    const char* fileName = nullptr;
    if (!PyArg_ParseTuple(args, "ss:Python_SetImportedBufferFile", &bufferName, &fileName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedBufferFile(bufferName, fileName);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedBufferStruct(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    const char* structName = nullptr;
    if (!PyArg_ParseTuple(args, "ss:Python_SetImportedBufferStruct", &bufferName, &structName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedBufferStruct(bufferName, structName);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedBufferType(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    int type = 0;
    if (!PyArg_ParseTuple(args, "si:Python_SetImportedBufferType", &bufferName, &type))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedBufferType(bufferName, (DataFieldType)type);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedBufferCount(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    int count = 0;
    if (!PyArg_ParseTuple(args, "si:Python_SetImportedBufferCount", &bufferName, &count))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedBufferCount(bufferName, count);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_GetImportedBufferBounds(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_GetImportedBufferBounds", &bufferName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    float minx = 0.0f;
    float miny = 0.0f;
    float minz = 0.0f;
    float maxx = 0.0f;
    float maxy = 0.0f;
    float maxz = 0.0f;
    g_interface->GetImportedBufferBounds(bufferName, minx, miny, minz, maxx, maxy, maxz);

    return Py_BuildValue("ffffff", minx, miny, minz, maxx, maxy, maxz);
}

static PyObject* Python_SetImportedBufferCSVHeaderRow(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    int CSVHeaderRow = 0;
    if (!PyArg_ParseTuple(args, "sp:Python_SetImportedBufferCSVHeaderRow", &bufferName, &CSVHeaderRow))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedBufferCSVHeaderRow(bufferName, CSVHeaderRow);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureFile(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    const char* fileName = nullptr;
    if (!PyArg_ParseTuple(args, "ss:Python_SetImportedTextureFile", &textureName, &fileName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureFile(textureName, fileName);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureMakeMips(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    int makeMips = 1;
    if (!PyArg_ParseTuple(args, "sp:Python_SetImportedTextureMakeMips", &textureName, &makeMips))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureMakeMips(textureName, makeMips);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureSourceIsSRGB(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    int sourceIsSRGB = 1;
    if (!PyArg_ParseTuple(args, "sp:Python_SetImportedTextureSourceIsSRGB", &textureName, &sourceIsSRGB))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureSourceIsSRGB(textureName, sourceIsSRGB);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureFormat(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    int textureFormat = 0;
    if (!PyArg_ParseTuple(args, "si:Python_SetImportedTextureFormat", &textureName, &textureFormat))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureFormat(textureName, textureFormat);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureColor(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    float r, g, b, a;
    if (!PyArg_ParseTuple(args, "sffff:Python_SetImportedTextureColor", &textureName, &r, &g, &b, &a))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureColor(textureName, r, g, b, a);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureSize(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    int x, y, z;
    if (!PyArg_ParseTuple(args, "siii:Python_SetImportedTextureSize", &textureName, &x, &y, &z))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureSize(textureName, x, y, z);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedBufferDataStream(PyObject* self, PyObject* args)
{
    const char* bufferName = nullptr;
    int index = 0;
    if (!PyArg_ParseTuple(args, "si:Python_SetImportedBufferDataStream", &bufferName, &index))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedBufferDataStream(bufferName, index);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureBinarySize(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    int x, y, z;
    if (!PyArg_ParseTuple(args, "siii:Python_SetImportedTextureBinarySize", &textureName, &x, &y, &z))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureBinarySize(textureName, x, y, z);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetImportedTextureBinaryFormat(PyObject* self, PyObject* args)
{
    const char* textureName = nullptr;
    int textureFormat = 0;
    if (!PyArg_ParseTuple(args, "si:Python_SetImportedTextureBinaryFormat", &textureName, &textureFormat))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetImportedTextureFormat(textureName, textureFormat);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetFrameDeltaTime(PyObject* self, PyObject* args)
{
    float deltaTime = 0.0f;
    if (!PyArg_ParseTuple(args, "f:Python_SetFrameDeltaTime", &deltaTime))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetFrameDeltaTime(deltaTime);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetCameraPos(PyObject* self, PyObject* args)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!PyArg_ParseTuple(args, "fff:Python_SetCameraPos", &x, &y, &z))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetCameraPos(x, y, z);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetCameraFOV(PyObject* self, PyObject* args)
{
    float fov = 0.0f;
    if (!PyArg_ParseTuple(args, "f:Python_SetCameraFOV", &fov))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetCameraFOV(fov);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetCameraAltitudeAzimuth(PyObject* self, PyObject* args)
{
    float altitude = 0.0f;
    float azimuth = 0.0f;
    if (!PyArg_ParseTuple(args, "ff:SetCameraAltitudeAzimuth", &altitude, &azimuth))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetCameraAltitudeAzimuth(altitude, azimuth);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetCameraNearFarZ(PyObject* self, PyObject* args)
{
    float nearZ = 0.0f;
    float farZ = 0.0f;
    if (!PyArg_ParseTuple(args, "ff:SetCameraNearFarZ", &nearZ, &farZ))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetCameraNearFarZ(nearZ, farZ);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetCameraFlySpeed(PyObject* self, PyObject* args)
{
    float speed = 0.0f;
    if (!PyArg_ParseTuple(args, "f:SetCameraFlySpeed", &speed))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetCameraFlySpeed(speed);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetProjMtxTextureName(PyObject* self, PyObject* args)
{
    const char* name = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_SetProjMtxTextureName", &name))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetProjMtxTextureName(name);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetCameraReverseZInfiniteDepth(PyObject* self, PyObject* args)
{
    int reverse = 0;
    if (!PyArg_ParseTuple(args, "p:Python_SetCameraReverseZInfiniteDepth", &reverse))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetCameraReverseZInfiniteDepth(reverse != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_SetCameraJitterLength(PyObject* self, PyObject* args)
{
    int jitterLength = 0;
    if (!PyArg_ParseTuple(args, "i:Python_SetCameraJitterLength", &jitterLength))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetCameraJitterLength(jitterLength);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_GetCameraPos(PyObject* self, PyObject* args)
{
    float x, y, z;
    g_interface->GetCameraPos(x, y, z);

    PyObject* ret = PyTuple_New(3);
    PyTuple_SetItem(ret, 0, PyFloat_FromDouble(x));
    PyTuple_SetItem(ret, 1, PyFloat_FromDouble(y));
    PyTuple_SetItem(ret, 2, PyFloat_FromDouble(z));
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_GetCameraAltitudeAzimuth(PyObject* self, PyObject* args)
{
    float altitude, azimuth;
    g_interface->GetCameraAltitudeAzimuth(altitude, azimuth);

    PyObject* ret = PyTuple_New(2);
    PyTuple_SetItem(ret, 0, PyFloat_FromDouble(altitude));
    PyTuple_SetItem(ret, 1, PyFloat_FromDouble(azimuth));
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_WriteGPUResource(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    PyObject* bytesObj = nullptr;
    int resourceIndex = 0;
    if (!PyArg_ParseTuple(args, "sS|i:Python_WriteGPUResource", &viewableResourceName, &bytesObj, &resourceIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->WriteGPUResource(viewableResourceName, resourceIndex, PyBytes_AsString(bytesObj), PyBytes_Size(bytesObj));

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_WriteGPUResourceBase64(PyObject* self, PyObject* args)
{
    const char* viewableResourceName = nullptr;
    const char* inputString = nullptr;
    int resourceIndex = 0;
    if (!PyArg_ParseTuple(args, "ss|i:Python_WriteGPUResourceBase64", &viewableResourceName, &inputString, &resourceIndex))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    std::vector<unsigned char> bytes;
    Base64Decode(inputString, bytes);

    g_interface->WriteGPUResource(viewableResourceName, resourceIndex, (const char*)bytes.data(), bytes.size());

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* Python_ForceEnableProfiling(PyObject* self, PyObject* args)
{
    int forceEnableProfiling = 1;
    if (!PyArg_ParseTuple(args, "|p:p", &forceEnableProfiling))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->ForceEnableProfiling(forceEnableProfiling != 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_GetProfilingData(PyObject* self, PyObject* args)
{
    PyObject* ret = PyDict_New();
    std::vector<PythonInterface::ProfilingData> data = g_interface->GetProfilingData();
    for (const PythonInterface::ProfilingData& item : data)
    {
        PyObject* key = Py_BuildValue("s", item.label.c_str());
        PyObject* value = Py_BuildValue("ff", item.cpums, item.gpums);
        PyDict_SetItem(ret, key, value);
    }
    return ret;
}

static PyObject* Python_IsResourceCreated(PyObject* self, PyObject* args)
{
    const char* resourceName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_IsResourceCreated", &resourceName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    bool success = g_interface->IsResourceCreated(resourceName);

    PyObject* ret = PyBool_FromLong(success ? 1 : 0);
    Py_INCREF(ret);
    return ret;
}

static PyObject* Python_SetViewedResource(PyObject* self, PyObject* args)
{
    const char* resourceName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_SetViewedResource", &resourceName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetViewedResource(resourceName);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_GGEnumValue(PyObject* self, PyObject* args)
{
    const char* enumName = nullptr;
    const char* enumLabel = nullptr;
    if (!PyArg_ParseTuple(args, "ss:Python_GGEnumValue", &enumName, &enumLabel))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    int value = g_interface->GGEnumValue(enumName, enumLabel);

    if (value == -1)
        return PyErr_Format(PyExc_TypeError, "Enum type not found " __FUNCTION__ "()");
    else if (value == -2)
        return PyErr_Format(PyExc_TypeError, "Enum value not found " __FUNCTION__ "()");

    return Py_BuildValue("i", value);
}

static PyObject* Python_GGEnumLabel(PyObject* self, PyObject* args)
{
    const char* enumName = nullptr;
    int enumValue = 0;
    if (!PyArg_ParseTuple(args, "si:Python_GGEnumLabel", &enumName, &enumValue))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    std::string label = g_interface->GGEnumLabel(enumName, enumValue);

    if (label.empty())
        return PyErr_Format(PyExc_TypeError, "Enum type not found or value invalid " __FUNCTION__ "()");

    return Py_BuildValue("s", label.c_str());
}

static PyObject* Python_GGEnumCount(PyObject* self, PyObject* args)
{
    const char* enumName = nullptr;
    if (!PyArg_ParseTuple(args, "s:Python_GGEnumCount", &enumName))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    int count = g_interface->GGEnumCount(enumName);

    if (count == -1)
        return PyErr_Format(PyExc_TypeError, "Enum type not found " __FUNCTION__ "()");

    return Py_BuildValue("i", count);
}

static PyObject* Python_GetGPUString(PyObject* self, PyObject* args)
{
    return Py_BuildValue("s", g_interface->GetGPUString().c_str());
}

static PyObject* Python_SetShaderAssertsLogging(PyObject* self, PyObject* args)
{
    int value = 1;
    if (!PyArg_ParseTuple(args, "p:Python_SetShaderAssertsLogging", &value))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetShaderAssertsLogging(value == 1);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_GetCollectedShaderAssertsCount(PyObject* self, PyObject* args)
{
    const int count = g_interface->GetCollectedShaderAssertsCount();
    return Py_BuildValue("i", count);
}

static PyObject* Python_GetShaderAssertFormatStrId(PyObject* self, PyObject* args)
{
    int assertId = 1;
    if (!PyArg_ParseTuple(args, "i:Python_GetShaderAssertFormatStrId", &assertId))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    const int count = (int)g_interface->GetCollectedShaderAssertsCount();
    if (assertId >= count)
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "(). OOB access (id:%d, asserts count: %d)", assertId, count);

    const int strId = g_interface->GetShaderAssertFormatStrId(assertId);
    return Py_BuildValue("i", strId);
}

static PyObject* Python_GetShaderAssertFormatString(PyObject* self, PyObject* args)
{
    int assertId = 1;
    if (!PyArg_ParseTuple(args, "i:Python_GetShaderAssertFormatString", &assertId))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    const int count = (int)g_interface->GetCollectedShaderAssertsCount();
    if (assertId >= count)
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "(). OOB access (id:%d, asserts count: %d)", assertId, count);

    const std::string fmt = g_interface->GetShaderAssertFormatString(assertId);
    return Py_BuildValue("s", fmt.c_str());
}

static PyObject* Python_GetShaderAssertDisplayName(PyObject* self, PyObject* args)
{
    int assertId = 1;
    if (!PyArg_ParseTuple(args, "i:Python_GetShaderAssertDisplayName", &assertId))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    const int count = (int)g_interface->GetCollectedShaderAssertsCount();
    if (assertId >= count)
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "(). OOB access (id:%d, asserts count: %d)", assertId, count);

    const std::string displayName = g_interface->GetShaderAssertDisplayName(assertId);
    return Py_BuildValue("s", displayName.c_str());
}

static PyObject* Python_GetShaderAssertMsg(PyObject* self, PyObject* args)
{
    int assertId = 1;
    if (!PyArg_ParseTuple(args, "i:Python_GetShaderAssertMsg", &assertId))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    const int count = (int)g_interface->GetCollectedShaderAssertsCount();
    if (assertId >= count)
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "(). OOB access (id:%d, asserts count: %d)", assertId, count);

    const std::string msg = g_interface->GetShaderAssertMsg(assertId);
    return Py_BuildValue("s", msg.c_str());
}

static PyObject* Python_GetAppCommandLine(PyObject* self, PyObject* args)
{
    return Py_BuildValue("s", g_interface->GetAppCommandLine().c_str());
}

static PyObject* Python_SetWindowSize(PyObject* self, PyObject* args)
{
    int width = 0;
    int height = 0;
    if (!PyArg_ParseTuple(args, "ii:Python_SetWindowSize", &width, &height))
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()");

    g_interface->SetWindowSize(width, height);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_MinimizeWindow(PyObject* self, PyObject* args)
{
    g_interface->MinimizeWindow();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_MaximizeWindow(PyObject* self, PyObject* args)
{
    g_interface->MaximizeWindow();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Python_RestoreWindow(PyObject* self, PyObject* args)
{
    g_interface->RestoreWindow();
    Py_INCREF(Py_None);
    return Py_None;
}

#define MAKE_GETTER_SETTER_BOOL(NAME) \
    static PyObject* Python_##NAME(PyObject* self, PyObject* args) \
    { \
        bool wantToSet = true; \
        int value = -1; \
        if (!PyArg_ParseTuple(args, "|p:" __FUNCTION__, &value)) \
            return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()"); \
\
        wantToSet = (value != -1); \
\
        if (g_interface->##NAME(value == 1, wantToSet)) \
            Py_RETURN_TRUE; \
        else \
            Py_RETURN_FALSE; \
    }

#define MAKE_GETTER_SETTER_UNSIGNED_INT(NAME) \
    static PyObject* Python_##NAME(PyObject* self, PyObject* args) \
    { \
        bool wantToSet = true; \
        unsigned int value = (unsigned int)-1; \
        if (!PyArg_ParseTuple(args, "|I:" __FUNCTION__, &value)) \
            return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()"); \
\
        wantToSet = (value != (unsigned int)-1); \
\
        return Py_BuildValue("I", g_interface->##NAME(value, wantToSet)); \
    }

#define MAKE_GETTER_SETTER_STRING(NAME) \
    static PyObject* Python_##NAME(PyObject* self, PyObject* args) \
    { \
        bool wantToSet = true; \
        const char* value = nullptr; \
        if (!PyArg_ParseTuple(args, "|s:" __FUNCTION__, &value)) \
            return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()"); \
\
        wantToSet = (value != nullptr && value[0] != 0); \
\
        return Py_BuildValue("s", g_interface->##NAME(value, wantToSet).c_str()); \
    }

MAKE_GETTER_SETTER_BOOL(AMDFrameGen_Enabled);
MAKE_GETTER_SETTER_UNSIGNED_INT(AMDFrameGen_SleepMS);
MAKE_GETTER_SETTER_STRING(AMDFrameGen_Depth);
MAKE_GETTER_SETTER_STRING(AMDFrameGen_MotionVectors);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_ENABLE_ASYNC_WORKLOAD_SUPPORT);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_ENABLE_HIGH_DYNAMIC_RANGE);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_ENABLE_DEBUG_CHECKING);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_DRAW_DEBUG_TEAR_LINES);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_DRAW_DEBUG_RESET_INDICATORS);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_DRAW_DEBUG_VIEW);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_DRAW_DEBUG_PACING_LINES);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_allowAsyncWorkloads);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_onlyPresentGenerated);
MAKE_GETTER_SETTER_BOOL(AMDFrameGen_constrainToRectangle);
MAKE_GETTER_SETTER_STRING(AMDFrameGen_uiTexture);
MAKE_GETTER_SETTER_STRING(AMDFrameGen_hudlessTexture);

#undef MAKE_GETTER_SETTER_BOOL
#undef MAKE_GETTER_SETTER_UNSIGNED_INT
#undef MAKE_GETTER_SETTER_STRING

// Enum FromString
#include "external/df_serialize/_common.h"
#define ENUM_BEGIN(_NAME, _DESCRIPTION) \
static PyObject* Python_##_NAME##FromString(PyObject* self, PyObject* args) \
{ \
    const char* enumStr = nullptr; \
    if (!PyArg_ParseTuple(args, "s:Python_" #_NAME "FromString", &enumStr)) \
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()"); \
    int enumValue = -1; \
    typedef _NAME ThisType;

#define ENUM_ITEM(_NAME, _DESCRIPTION) \
    if (!_stricmp(enumStr, #_NAME)) \
        enumValue = (int)ThisType::##_NAME;

#define ENUM_END() \
    PyObject* ret = PyLong_FromLong(enumValue); \
    Py_INCREF(ret); \
    return ret; \
}
// clang-format off
#include "external/df_serialize/_fillunsetdefines.h"
#include "Schemas/Schemas.h"
// clang-format on

// Enum ToString
#include "external/df_serialize/_common.h"
#define ENUM_BEGIN(_NAME, _DESCRIPTION) \
static PyObject* Python_##_NAME##ToString(PyObject* self, PyObject* args) \
{ \
    int enumValue = -1; \
    if (!PyArg_ParseTuple(args, "i:Python_" #_NAME "ToString", &enumValue)) \
        return PyErr_Format(PyExc_TypeError, "type error in " __FUNCTION__ "()"); \
    const char* enumStr = nullptr; \
    typedef _NAME ThisType; \
    switch((ThisType)enumValue) \
    {

#define ENUM_ITEM(_NAME, _DESCRIPTION) \
    case ThisType::_NAME: return Py_BuildValue("s", #_NAME);

#define ENUM_END() \
    } \
    return Py_BuildValue("s", "Invalid"); \
}
// clang-format off
#include "external/df_serialize/_fillunsetdefines.h"
#include "Schemas/Schemas.h"
// clang-format on

static void AddEnums(PyObject* module)
{
    char buffer[4096];

    #include "external/df_serialize/_common.h"

    #define ENUM_BEGIN(_NAME, _DESCRIPTION) \
    { \
        const char* enumName = #_NAME; \
        sprintf_s(buffer, "%s_FIRST", enumName); \
        PyModule_AddIntConstant(module, buffer, 0); \
        int enumValue = 0;

    #define ENUM_ITEM(_NAME, _DESCRIPTION) \
        sprintf_s(buffer, "%s_" #_NAME, enumName); \
        PyModule_AddIntConstant(module, buffer, enumValue); \
        enumValue++;

    #define ENUM_END() \
        sprintf_s(buffer, "%s_LAST", enumName); \
        PyModule_AddIntConstant(module, buffer, enumValue-1); \
        sprintf_s(buffer, "%s_COUNT", enumName); \
        PyModule_AddIntConstant(module, buffer, enumValue); \
    }
    // clang-format off
    #include "external/df_serialize/_fillunsetdefines.h"
    #include "Schemas/Schemas.h"
    // clang-format on
}

void PythonInit(PythonInterface* interfacePtr)
{
    g_interface = interfacePtr;

    static PyMethodDef pythonModuleMethods[] =
    {
        // These don't make sense to expose to MCP
        {"GetScriptPath", Python_GetScriptPath, METH_VARARGS, "Returns the path of the python script file ran, without a file name."},
        {"GetScriptFileName", Python_GetScriptFileName, METH_VARARGS, "Returns the file name of the python script file ran, without the path."},
        {"SetShaderAssertsLogging", Python_SetShaderAssertsLogging, METH_VARARGS, "Toggles auto error logging of the collected shader asserts after a technique execution."},
        {"GetCollectedShaderAssertsCount", Python_GetCollectedShaderAssertsCount, METH_VARARGS, "Returns the number of collected shader asserts. Assert getters works with this collection."},
        {"GetShaderAssertFormatStrId", Python_GetShaderAssertFormatStrId, METH_VARARGS, "Returns the ID of format string of the specified shader assert."},
        {"GetShaderAssertFormatString", Python_GetShaderAssertFormatString, METH_VARARGS, "Returns the format string of the specified shader assert."},
        {"GetShaderAssertDisplayName", Python_GetShaderAssertDisplayName, METH_VARARGS, "Returns the display name of the specified shader assert."},
        {"GetShaderAssertMsg", Python_GetShaderAssertMsg, METH_VARARGS, "Returns the message of the specified assert."},
        {"GetAppCommandLine", Python_GetAppCommandLine, METH_VARARGS, "Returns the command line arguments without the executable name."},
        {"WriteGPUResource", Python_WriteGPUResource, METH_VARARGS, "Writes a gpu resource the next time the technique runs."},
        {"Readback", Python_Readback, METH_VARARGS, "Reads back a resource. Before you can call this, you must call SetWantReadback(), you must RunTechnique() at least once, then call WaitOnGPU(). After that, you can call this function to get the data."},

        // It is prefered to define new python functions in ViewerPythonFunctionList.h, so that they are also exposed to telnet and MCP.
        // You add it to the list, put the implementation here, and the rest should be automatic for most types of script functionality.
        #define FUNCTION_BEGIN(NAME, DESCRIPTION) {#NAME, Python_##NAME, METH_VARARGS, DESCRIPTION},
        #include "ViewerPythonFunctionList.h"

        // Enum FromString and ToString functions
        #include "external/df_serialize/_common.h"
        #define ENUM_BEGIN(_NAME, _DESCRIPTION) \
            { #_NAME "FromString", Python_##_NAME##FromString, METH_VARARGS, "Enum From String" }, \
            { #_NAME "ToString", Python_##_NAME##ToString, METH_VARARGS, "Enum To String" },
         // clang-format off
		#include "external/df_serialize/_fillunsetdefines.h"
        #include "Schemas/Schemas.h"
        // clang-format on

        {nullptr, nullptr, 0, nullptr}
    };

    static PyModuleDef pythonModule =
    {
        PyModuleDef_HEAD_INIT, "Host", NULL, -1, pythonModuleMethods,
        NULL, NULL, NULL, NULL
    };

    PyImport_AppendInittab("Host",
        []()
        {
            PyObject* module = PyModule_Create(&pythonModule);
            AddEnums(module);
            return module;
        }
    );

    PyInit_GigiArray();

    // Build a standard python config
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    // Set isolated mode to 1. This prevents python from loading any envionment variables,
    // which isolates this session from any global python the user may have installed.
	config.isolated = 1;

    // Set the `home` field - equivalent to PYTHONHOME environment variable -
    // to our python install directory which we expect to be always relative to the exe dir.
    // This is important because it enables python to find extra modules installed in that location.
    // Another candidate is the working directory, but that may be different if running GigiViewer from another directory
    // so this is a safer option.
    TCHAR exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    // Get the last slash address and fill all space after it with zeros
    TCHAR* pExeName = wcsrchr(exePath, L'\\');
    ZeroMemory(pExeName, (exePath + MAX_PATH - pExeName) * sizeof(TCHAR));
    // Write the python install directory to the end of the exe string
    const int pythonHomeBufSize = 4096;
    wchar_t pythonHomeBuf[pythonHomeBufSize] = { 0 };
    swprintf_s(pythonHomeBuf, L"%s\\GigiViewerDX12\\python\\Python310", exePath);

    config.home = pythonHomeBuf;
    Py_InitializeFromConfig(&config);

    // Initialize the persistent dictionary for REPL-style string execution
    g_persistentDict = PyDict_New();
    PyDict_SetItemString(g_persistentDict, "__builtins__", PyEval_GetBuiltins());

    // Import Host and GigiArray modules into the persistent dictionary
    PyObject* hostModule = PyImport_ImportModule("Host");
    if (hostModule) {
        PyDict_SetItemString(g_persistentDict, "Host", hostModule);
        Py_DECREF(hostModule);
    }

    PyObject* gigiArrayModule = PyImport_ImportModule("GigiArray");
    if (gigiArrayModule) {
        PyDict_SetItemString(g_persistentDict, "GigiArray", gigiArrayModule);
        Py_DECREF(gigiArrayModule);
    }
}

void PythonShutdown()
{
	// Clean up the persistent dictionary
	Py_XDECREF(g_persistentDict);
	g_persistentDict = nullptr;

	Py_Finalize();
}

void PythonExecuteNetwork(CViewerServer& server)
{
    std::string command;
    server.Tick();
    while (server.PopMessage(command))
    {
        g_interface->Log(LogLevel::Info, "Network Command: \"%s\"", command.c_str());

        std::vector<std::wstring> args;
        std::string fullCommand = std::string("lastCommandResult = ") + command;
        PythonExecuteString(fullCommand.c_str(), args);

        server.Send((g_lastCommandResult + "\r\n").c_str());
    }
}

bool PythonExecuteString(const char* text, const std::vector<std::wstring>& args)
{
    // Clear previous error
    g_lastCommandErrorForPythonFn = g_lastCommandError;
    g_lastCommandError.clear();

    // handle command line parameters
    {
        g_argvText = args;
        g_argv.resize(g_argvText.size());
        for (size_t i = 0; i < g_argv.size(); ++i)
            g_argv[i] = (wchar_t*)g_argvText[i].c_str();

        PySys_SetArgv(int(g_argv.size()), g_argv.data());
    }

    g_interface->m_scriptLocation = "./";

    // execute the script using the persistent dictionary for REPL-style execution
    PyObject* ret = PyRun_String(text, Py_file_input, g_persistentDict, g_persistentDict);

    g_interface->OnExecuteFinished();

    // report errors if there were any
    if (PyErr_Occurred() != NULL) {
        PyObject* pyExcType;
        PyObject* pyExcValue;
        PyObject* pyExcTraceback;
        PyErr_Fetch(&pyExcType, &pyExcValue, &pyExcTraceback);
        PyErr_NormalizeException(&pyExcType, &pyExcValue, &pyExcTraceback);

        PyObject* str_exc_type = PyObject_Repr(pyExcType);
        PyObject* pyStr = PyUnicode_AsEncodedString(str_exc_type, "utf-8", "Error ~");
        const char* strExcType = PyBytes_AS_STRING(pyStr);

        PyObject* str_exc_value = PyObject_Repr(pyExcValue);
        PyObject* pyExcValueStr = PyUnicode_AsEncodedString(str_exc_value, "utf-8", "Error ~");
        const char* strExcValue = PyBytes_AS_STRING(pyExcValueStr);

        const char* actual_file_name = "";
        if (PyObject_HasAttrString(pyExcValue, "filename"))
        {
            PyObject* file_name = PyObject_GetAttrString(pyExcValue, "filename");
            PyObject* file_name_str = PyObject_Str(file_name);
            PyObject* file_name_unicode = PyUnicode_AsEncodedString(file_name_str, "utf-8", "Error");
            actual_file_name = PyBytes_AsString(file_name_unicode);
        }

        const char* actual_line_no = "";
        if (PyObject_HasAttrString(pyExcValue, "lineno"))
        {
            PyObject* line_no = PyObject_GetAttrString(pyExcValue, "lineno");
            PyObject* line_no_str = PyObject_Str(line_no);
            PyObject* line_no_unicode = PyUnicode_AsEncodedString(line_no_str, "utf-8", "Error");
            actual_line_no = PyBytes_AsString(line_no_unicode);
        }

        char errorBuffer[1024];
        snprintf(errorBuffer, sizeof(errorBuffer), "Python Error: [%s:%s] %s", actual_file_name, actual_line_no, strExcValue);
        g_lastCommandError = errorBuffer;

        g_interface->Log(LogLevel::Error, "%s", errorBuffer);

        Py_XDECREF(pyExcType);
        Py_XDECREF(pyExcValue);
        Py_XDECREF(pyExcTraceback);

        Py_XDECREF(str_exc_type);
        Py_XDECREF(pyStr);

        Py_XDECREF(str_exc_value);
        Py_XDECREF(pyExcValueStr);

        g_lastCommandResult = g_lastCommandError;

        return false;
    }

    Py_XDECREF(ret);

    PyObject* result = PyDict_GetItemString(g_persistentDict, "lastCommandResult");
    if (result)
        g_lastCommandResult = PyObjectToJsonString(result);

    return true;
}

bool PythonExecuteFile(const char* fileName, const std::vector<std::wstring>& args)
{
    // handle command line parameters
    {
        g_argvText = args;
        g_argv.resize(g_argvText.size());
        for (size_t i = 0; i < g_argv.size(); ++i)
            g_argv[i] = (wchar_t*)g_argvText[i].c_str();

        PySys_SetArgv(int(g_argv.size()), g_argv.data());
    }

    g_interface->m_scriptLocation = std::filesystem::weakly_canonical(fileName).string();

    // Read the file in
    std::vector<char> fileData;
    {
        FILE* file = nullptr;
        fopen_s(&file, fileName, "rb");
        if (!file)
            return false;

        fseek(file, 0, SEEK_END);
        fileData.resize(ftell(file) + 1);
        fseek(file, 0, SEEK_SET);

        fread(fileData.data(), 1, fileData.size() - 1, file);
        fileData[fileData.size() - 1] = 0;
        fclose(file);
    }

    // execute the script, setting the command line args too
    PyObject* globals = PyDict_New();
    PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());

    // Import Host and GigiArray modules into the globals dictionary for the file execution
    PyObject* hostModule = PyImport_ImportModule("Host");
    if (hostModule) {
        PyDict_SetItemString(globals, "Host", hostModule);
        Py_DECREF(hostModule);
    }

    PyObject* gigiArrayModule = PyImport_ImportModule("GigiArray");
    if (gigiArrayModule) {
        PyDict_SetItemString(globals, "GigiArray", gigiArrayModule);
        Py_DECREF(gigiArrayModule);
    }

    PyObject* ret = PyRun_String(fileData.data(), Py_file_input, globals, globals);

    g_interface->OnExecuteFinished();

    // report errors if there were any
    if (PyErr_Occurred() != NULL) {
        PyObject* pyExcType;
        PyObject* pyExcValue;
        PyObject* pyExcTraceback;
        PyErr_Fetch(&pyExcType, &pyExcValue, &pyExcTraceback);
        PyErr_NormalizeException(&pyExcType, &pyExcValue, &pyExcTraceback);

        PyObject* str_exc_type = PyObject_Repr(pyExcType);
        PyObject* pyStr = PyUnicode_AsEncodedString(str_exc_type, "utf-8", "Error ~");
        const char* strExcType = PyBytes_AS_STRING(pyStr);

        PyObject* str_exc_value = PyObject_Repr(pyExcValue);
        PyObject* pyExcValueStr = PyUnicode_AsEncodedString(str_exc_value, "utf-8", "Error ~");
        const char* strExcValue = PyBytes_AS_STRING(pyExcValueStr);

        const char* actual_file_name = "";
        if (PyObject_HasAttrString(pyExcValue, "filename"))
        {
            PyObject* file_name = PyObject_GetAttrString(pyExcValue, "filename");
            PyObject* file_name_str = PyObject_Str(file_name);
            PyObject* file_name_unicode = PyUnicode_AsEncodedString(file_name_str, "utf-8", "Error");
            actual_file_name = PyBytes_AsString(file_name_unicode);
        }

        const char* actual_line_no = "";
        if (PyObject_HasAttrString(pyExcValue, "lineno"))
        {
            PyObject* line_no = PyObject_GetAttrString(pyExcValue, "lineno");
            PyObject* line_no_str = PyObject_Str(line_no);
            PyObject* line_no_unicode = PyUnicode_AsEncodedString(line_no_str, "utf-8", "Error");
            actual_line_no = PyBytes_AsString(line_no_unicode);
        }

        g_interface->Log(LogLevel::Error, "Python Error: [%s:%s] %s", actual_file_name, actual_line_no, strExcValue);

        Py_XDECREF(pyExcType);
        Py_XDECREF(pyExcValue);
        Py_XDECREF(pyExcTraceback);

        Py_XDECREF(str_exc_type);
        Py_XDECREF(pyStr);

        Py_XDECREF(str_exc_value);
        Py_XDECREF(pyExcValueStr);

        g_lastCommandResult = g_lastCommandError;

        return false;
    }

    Py_DECREF(globals);
    Py_DECREF(ret);

    PyObject* result = PyDict_GetItemString(globals, "lastCommandResult");
    if (result)
        g_lastCommandResult = PyObjectToJsonString(result);

    return true;
}

const std::string& PythonGetLastResult()
{
    return g_lastCommandResult;
}

const std::string& PythonGetLastError()
{
    return g_lastCommandError;
}
