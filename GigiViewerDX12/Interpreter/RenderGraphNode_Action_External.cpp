///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2024 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#include "GigiInterpreterPreviewWindowDX12.h"
#include "NodesShared.h"

#include "api/include/dx12/ffx_api_dx12.hpp"
#include "upscalers/include/ffx_upscale.hpp"
#include "framegeneration/include/ffx_framegeneration.hpp"
#include "framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp"
#include "Shared/HashAll.h"
#include "Shared/ffx_utils.h"

// ONNX Runtime + DirectML EP. Headers come from
// Microsoft.ML.OnnxRuntime.DirectML NuGet (restored into ./packages/).
// DirectML.h comes from the Windows SDK, which we already depend on for DX12.
#include <DirectML.h>
#include <unordered_set>
#include "onnxruntime_cxx_api.h"
#include "dml_provider_factory.h"

// Gigi helpers for the transpose pass.
#include "DX12Utils/CreateResources.h"
#include "DX12Utils/CompileShaders.h"
#include "DX12Utils/DescriptorTableCache.h"
#include "NHWCNCHWTransposeHLSL.h"

static void SetLogFunction(ffxApiMessage& messageFunc, GigiInterpreterPreviewWindowDX12* interpreter)
{
    static auto LogMessage = [](uint32_t type, const wchar_t* message)
        {
            GigiInterpreterPreviewWindowDX12::GetLogFn()(LogLevel::Warn, "FidelityFX: %s", FromWideString(message).c_str());
        }
    ;
    messageFunc = LogMessage;
}

static void PublishTexture(const RenderGraphNode_Action_External& node, RuntimeTypes::RenderGraphNode_Action_External& runtimeData, GigiInterpreterPreviewWindowDX12& interpreter, const RenderGraphNode_Resource_Texture* textureNode, const RuntimeTypes::RenderGraphNode_Resource_Texture& texture, const char* textureName, const char* extraLabel, bool resultOfWrite)
{
    std::string label = node.name + std::string(".") + std::string(textureName) + std::string(": ") + textureNode->name + std::string(extraLabel);
    runtimeData.HandleViewableTexture(interpreter, TextureDimensionTypeToViewableResourceType(textureNode->dimension), label.c_str(), texture.m_resource, texture.m_format, texture.m_size, texture.m_numMips, false, resultOfWrite);
}

// Get the texture nodes and make sure the required ones exist
static const RenderGraphNode_Resource_Texture* GetTextureResourceNode(int resourceNodeIndex, const std::vector<RenderGraphNode>& nodes)
{
    if (resourceNodeIndex < 0)
        return nullptr;

    if (nodes[resourceNodeIndex]._index != RenderGraphNode::c_index_resourceTexture)
        return nullptr;

    return &nodes[resourceNodeIndex].resourceTexture;
}

static void SetFfxApiResourceToTexture(const RuntimeTypes::RenderGraphNode_Resource_Texture& src, const RenderGraphNode_Resource_Texture* srcNode, FfxApiResource& dest, TransitionTracker& transitions)
{
    if (!srcNode || !src.m_resource)
        return;

    uint32_t additionalUses = 0;

    additionalUses |= ((srcNode->accessedAs & (1 << (unsigned int)ShaderResourceAccessType::RenderTarget)) ? FFX_API_RESOURCE_USAGE_RENDERTARGET : 0);
    additionalUses |= ((srcNode->accessedAs & (1 << (unsigned int)ShaderResourceAccessType::UAV)) ? FFX_API_RESOURCE_USAGE_UAV : 0);
    additionalUses |= ((srcNode->accessedAs & (1 << (unsigned int)ShaderResourceAccessType::DepthTarget)) ? FFX_API_RESOURCE_USAGE_DEPTHTARGET : 0);
    additionalUses |= ((srcNode->accessedAs & (1 << (unsigned int)ShaderResourceAccessType::Indirect)) ? FFX_API_RESOURCE_USAGE_INDIRECT : 0);

    if (srcNode->dimension == TextureDimensionType::Texture2DArray || srcNode->dimension == TextureDimensionType::TextureCube)
        additionalUses |= FFX_API_RESOURCE_USAGE_ARRAYVIEW;

    if (srcNode->accessedAs & (1 << (unsigned int)ShaderResourceAccessType::DepthTarget) && Get_DXGI_FORMAT_Info(src.m_format).isStencil)
        additionalUses |= FFX_API_RESOURCE_USAGE_STENCILTARGET;

    D3D12_RESOURCE_STATES dxState = transitions.GetCurrentState(src.m_resource);
    dest = ffxApiGetResourceDX12(src.m_resource, D3D12State_To_FfxState(dxState), additionalUses);
}

void RuntimeTypes::RenderGraphNode_Action_External::Release(GigiInterpreterPreviewWindowDX12& interpreter)
{
    // m_AMD_FidelityFXSDK_Upscaling
    {
        for (auto& it : m_AMD_FidelityFXSDK_Upscaling.m_contexts)
        {
            if (it.m_UpscalingContext)
            {
                ffx::DestroyContext(it.m_UpscalingContext);
                it.m_UpscalingContext = nullptr;
            }
        }
        m_AMD_FidelityFXSDK_Upscaling.m_contexts.clear();
    }

    // m_ONNX. Teardown order: DML allocations (hold refs on buffers) -> IO
    // binding -> session -> buffers -> PSO / root sig -> mem info -> DML
    // queue / fence -> DML device -> env. Reverse of lazy-init order, with
    // COM Release()/delete matching each acquisition.
    {
        const OrtDmlApi* dmlApi = nullptr;
        if (m_ONNX.m_inputDmlAlloc || m_ONNX.m_outputDmlAlloc)
        {
            Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dmlApi));
        }
        if (m_ONNX.m_inputDmlAlloc)  { if (dmlApi) dmlApi->FreeGPUAllocation(m_ONNX.m_inputDmlAlloc);  m_ONNX.m_inputDmlAlloc = nullptr; }
        if (m_ONNX.m_outputDmlAlloc) { if (dmlApi) dmlApi->FreeGPUAllocation(m_ONNX.m_outputDmlAlloc); m_ONNX.m_outputDmlAlloc = nullptr; }

        if (m_ONNX.m_ortIoBinding)    { delete reinterpret_cast<Ort::IoBinding*>(m_ONNX.m_ortIoBinding); m_ONNX.m_ortIoBinding = nullptr; }
        if (m_ONNX.m_ortMemInfoDML)   { delete reinterpret_cast<Ort::MemoryInfo*>(m_ONNX.m_ortMemInfoDML); m_ONNX.m_ortMemInfoDML = nullptr; }
        if (m_ONNX.m_ortSession)      { delete reinterpret_cast<Ort::Session*>(m_ONNX.m_ortSession); m_ONNX.m_ortSession = nullptr; }

        if (m_ONNX.m_inputBuffer)     { reinterpret_cast<IUnknown*>(m_ONNX.m_inputBuffer)->Release(); m_ONNX.m_inputBuffer = nullptr; }
        if (m_ONNX.m_outputBuffer)    { reinterpret_cast<IUnknown*>(m_ONNX.m_outputBuffer)->Release(); m_ONNX.m_outputBuffer = nullptr; }
        m_ONNX.m_inputBufferSize = 0;
        m_ONNX.m_outputBufferSize = 0;

        if (m_ONNX.m_transposePSO)      { reinterpret_cast<IUnknown*>(m_ONNX.m_transposePSO)->Release(); m_ONNX.m_transposePSO = nullptr; }
        if (m_ONNX.m_transposeRootSig)  { reinterpret_cast<IUnknown*>(m_ONNX.m_transposeRootSig)->Release(); m_ONNX.m_transposeRootSig = nullptr; }

        if (m_ONNX.m_dmlFence)          { reinterpret_cast<IUnknown*>(m_ONNX.m_dmlFence)->Release(); m_ONNX.m_dmlFence = nullptr; }
        m_ONNX.m_dmlFenceValue = 0;
        if (m_ONNX.m_dmlCommandQueue)   { reinterpret_cast<IUnknown*>(m_ONNX.m_dmlCommandQueue)->Release(); m_ONNX.m_dmlCommandQueue = nullptr; }
        if (m_ONNX.m_dmlDevice)         { reinterpret_cast<IUnknown*>(m_ONNX.m_dmlDevice)->Release(); m_ONNX.m_dmlDevice = nullptr; }

        if (m_ONNX.m_ortEnv)            { delete reinterpret_cast<Ort::Env*>(m_ONNX.m_ortEnv); m_ONNX.m_ortEnv = nullptr; }

        m_ONNX.m_sessionConfigHash = 0;
        m_ONNX.m_loadedFile.clear();
        m_ONNX.m_inputName.clear();
        m_ONNX.m_outputName.clear();
        m_ONNX.m_inputShape.clear();
        m_ONNX.m_outputShape.clear();
        m_ONNX.m_inputElemCount = 0;
        m_ONNX.m_outputElemCount = 0;
    }
}

bool GigiInterpreterPreviewWindowDX12::OnNodeAction_External_AMD_FidelityFXSDK_Upscaling(const RenderGraphNode_Action_External& node, RuntimeTypes::RenderGraphNode_Action_External& runtimeData_, NodeAction nodeAction)
{
    if (nodeAction == NodeAction::Init)
        return true;

    if (!runtimeData_.m_conditionIsTrue)
        return true;

    RuntimeTypes::RenderGraphNode_Action_External::AMD_FidelityFXSDK_Upscaling& runtimeData = runtimeData_.m_AMD_FidelityFXSDK_Upscaling;
    const ExternalNode_AMD_FidelityFXSDK_Upscaling& nodeData = node.externalNodeData.AMD_FidelityFXSDK_Upscaling;

    auto GetContext = [&runtimeData](const ffx::CreateContextDescUpscale& desc, ExternalNode_AMD_FidelityFXSDK_Upscaling_Version desiredVersion) -> RuntimeTypes::RenderGraphNode_Action_External::AMD_FidelityFXSDK_Upscaling::Context&
        {
            // Use an existing context if it exists
            size_t createFsrDescHash = HashAll(desc.flags, desiredVersion);
            for (auto& context : runtimeData.m_contexts)
            {
                if (context.m_UpscalingContextHash == createFsrDescHash &&
                    context.m_maxRenderSize[0] >= desc.maxRenderSize.width && context.m_maxRenderSize[1] >= desc.maxRenderSize.height &&
                    context.m_maxUpscaleSize[0] >= desc.maxUpscaleSize.width && context.m_maxUpscaleSize[1] >= desc.maxUpscaleSize.height)
                {
                    context.m_age = 0;
                    return context;
                }
            }

            // Otherwise create and return a new context
            RuntimeTypes::RenderGraphNode_Action_External::AMD_FidelityFXSDK_Upscaling::Context newContext;
            newContext.m_UpscalingContextHash = createFsrDescHash;
            newContext.m_maxRenderSize[0] = desc.maxRenderSize.width;
            newContext.m_maxRenderSize[1] = desc.maxRenderSize.height;
            newContext.m_maxUpscaleSize[0] = desc.maxUpscaleSize.width;
            newContext.m_maxUpscaleSize[1] = desc.maxUpscaleSize.height;
            runtimeData.m_contexts.push_back(newContext);
            return *runtimeData.m_contexts.rbegin();
        }
    ;

#define HandleTexture(NAME, extraLabel) \
        const RenderGraphNode_Resource_Texture* node_##NAME = GetTextureResourceNode(nodeData.##NAME.resourceNodeIndex, m_renderGraph.nodes); \
        bool textureExists_##NAME = false; \
        RuntimeTypes::RenderGraphNode_Resource_Texture& texture_##NAME = GetRuntimeNodeData_RenderGraphNode_Resource_Texture( node_##NAME ? node_##NAME->name.c_str() : "", textureExists_##NAME); \
        if (textureExists_##NAME) \
            PublishTexture(node, runtimeData_, *this, node_##NAME, texture_##NAME, #NAME, extraLabel, false);

    HandleTexture(color, " (SRV)")
        HandleTexture(colorOpaqueOnly, " (SRV)")
        HandleTexture(depth, " (SRV)")
        HandleTexture(motionVectors, " (SRV)")
        HandleTexture(exposure, " (SRV)")
        HandleTexture(reactive, " (UAV - Before)")
        //HandleTexture(transparencyAndComposition, " (SRV)")
        HandleTexture(output, " (UAV - Before)")

#undef HandleTexture

    if (!textureExists_color)
    {
        m_logFn(LogLevel::Error, "No color texture is connected for node \"%s\"\n", node.name.c_str());
        return true;
    }

    if (!textureExists_output)
    {
        m_logFn(LogLevel::Error, "No output texture is connected for node \"%s\"\n", node.name.c_str());
        return true;
    }

    // Input and output cannot be zero sized
    uint32_t renderSize[2] = { (uint32_t)texture_color.m_size[0], (uint32_t)texture_color.m_size[1] };
    uint32_t upscaleSize[2] = { (uint32_t)texture_output.m_size[0], (uint32_t)texture_output.m_size[1] };
    if (renderSize[0] == 0 || renderSize[1] == 0 || upscaleSize[0] == 0 || upscaleSize[1] == 0)
        return true;

    ffx::CreateContextDescUpscale createFsrDesc = {};
    createFsrDesc.maxRenderSize = { max(renderSize[0], nodeData.initialMaxRenderSize[0]), max(renderSize[1], nodeData.initialMaxRenderSize[1]) };
    createFsrDesc.maxUpscaleSize = { max(upscaleSize[0], nodeData.initialMaxUpscaleSize[0]), max(upscaleSize[1], nodeData.initialMaxUpscaleSize[1]) };
    createFsrDesc.flags = 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_HIGH_DYNAMIC_RANGE.variable.variableIndex) ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS.variable.variableIndex) ? FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_MOTION_VECTORS_JITTER_CANCELLATION.variable.variableIndex) ? FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DEPTH_INVERTED.variable.variableIndex) ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DEPTH_INFINITE.variable.variableIndex) ? FFX_UPSCALE_ENABLE_DEPTH_INFINITE : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_AUTO_EXPOSURE.variable.variableIndex) ? FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DYNAMIC_RESOLUTION.variable.variableIndex) ? FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DEBUG_CHECKING.variable.variableIndex) ? FFX_UPSCALE_ENABLE_DEBUG_CHECKING : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_NON_LINEAR_COLORSPACE.variable.variableIndex) ? FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE : 0;
    createFsrDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DEBUG_VISUALIZATION.variable.variableIndex) ? FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION : 0;
    SetLogFunction(createFsrDesc.fpMessage, this);

    ExternalNode_AMD_FidelityFXSDK_Upscaling_Version desiredVersion = (ExternalNode_AMD_FidelityFXSDK_Upscaling_Version)GetRuntimeVariableValueAllowCast_NoFail<int>(nodeData.version.variable.variableIndex);
    auto& context = GetContext(createFsrDesc, desiredVersion);

    // Create the upscaling context if it doesn't exist
    if (context.m_UpscalingContext == nullptr)
    {
        // Get and show available versions
        std::vector<uint64_t> FsrVersionIds;
        std::vector<const char*> FsrVersionNames;
        {
            ffxQueryDescGetVersions versionQuery = { 0 };
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            versionQuery.device = m_device;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;
            ffxReturnCode_t retCode_t = ffxQuery(nullptr, &versionQuery.header);

            FsrVersionIds.resize(versionCount);
            FsrVersionNames.resize(versionCount);
            versionQuery.versionIds = FsrVersionIds.data();
            versionQuery.versionNames = FsrVersionNames.data();
            retCode_t = ffxQuery(nullptr, &versionQuery.header);

            m_logFn(LogLevel::Info, "Initializing FSR. Versions available:");
            for (uint64_t versionIndex = 0; versionIndex < versionCount; ++versionIndex)
                m_logFn(LogLevel::Info, "    %s (0x%016llx)", FsrVersionNames[versionIndex], FsrVersionIds[versionIndex]);
        }

        // If the have chosen a version, match it by string version
        uint64_t overrideVersionId = 0;
        {
            const char* matchString = nullptr;

            switch (desiredVersion)
            {
            case ExternalNode_AMD_FidelityFXSDK_Upscaling_Version::Default: break;
            case ExternalNode_AMD_FidelityFXSDK_Upscaling_Version::v2_3_4:
            {
                matchString = "2.3.4";
                break;
            }
            case ExternalNode_AMD_FidelityFXSDK_Upscaling_Version::v3_1_5:
            {
                matchString = "3.1.5";
                break;
            }
            default:
            {
                m_logFn(LogLevel::Error, "Unhandled FSR version \"%s\"", EnumToString(desiredVersion));
                return false;
            }
            }

            // get overrideVersionId
            if (matchString)
            {
                for (size_t versionIndex = 0; versionIndex < FsrVersionNames.size(); ++versionIndex)
                {
                    if (strcmp(FsrVersionNames[versionIndex], matchString) == 0)
                    {
                        overrideVersionId = FsrVersionIds[versionIndex];
                        break;
                    }
                }
                if (overrideVersionId == 0)
                {
                    m_logFn(LogLevel::Error, "Could not find requested FSR version \"%s\"", matchString);
                    return false;
                }
            }
        }

        ffx::CreateBackendDX12Desc backendDesc = {};
        backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backendDesc.device = m_device;

        // Get RAM usage for settings
        FfxApiEffectMemoryUsage gpuMemoryUsageUpscaler = { 0 };
        ffxQueryDescUpscaleGetGPUMemoryUsageV2 upscalerGetGPUMemoryUsageV2 = {};
        upscalerGetGPUMemoryUsageV2.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE_V2;
        upscalerGetGPUMemoryUsageV2.device = m_device;
        upscalerGetGPUMemoryUsageV2.maxRenderSize = createFsrDesc.maxRenderSize;
        upscalerGetGPUMemoryUsageV2.maxUpscaleSize = createFsrDesc.maxUpscaleSize;
        upscalerGetGPUMemoryUsageV2.flags = createFsrDesc.flags;
        upscalerGetGPUMemoryUsageV2.gpuMemoryUsageUpscaler = &gpuMemoryUsageUpscaler;

        // Apply version override if we should
        ffxOverrideVersion versionOverride = { 0 };
        if (overrideVersionId != 0)
        {
            versionOverride.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
            versionOverride.versionId = overrideVersionId;
            upscalerGetGPUMemoryUsageV2.header.pNext = &versionOverride.header;
        }

        ffx::ReturnCode retCode = ffx::Query(upscalerGetGPUMemoryUsageV2);
        if (retCode != ffx::ReturnCode::Ok)
        {
            m_logFn(LogLevel::Error, "Could not query memory usage for node \"%s\" in " __FUNCTION__ "\n", node.name.c_str());
            return false;
        }

        ffx::CreateContextDescUpscaleVersion headerVersion = {};
        headerVersion.version = FFX_UPSCALER_VERSION;

        // Create the context
        if (overrideVersionId != 0)
            retCode = ffx::CreateContext(context.m_UpscalingContext, nullptr, createFsrDesc, backendDesc, headerVersion, versionOverride);
        else
            retCode = ffx::CreateContext(context.m_UpscalingContext, nullptr, createFsrDesc, backendDesc, headerVersion);
        if (retCode != ffx::ReturnCode::Ok)
        {
            m_logFn(LogLevel::Error, "Could not create upscaling context for node \"%s\" in " __FUNCTION__ "\n", node.name.c_str());
            return false;
        }

        // Get the version created
        ffxQueryGetProviderVersion getVersion = {};
        getVersion.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        ffxReturnCode_t retCode_t = ffxQuery(&context.m_UpscalingContext, &getVersion.header);
        if (retCode_t != FFX_API_RETURN_OK)
        {
            m_logFn(LogLevel::Error, "Could not query upscaling context version for node \"%s\" in " __FUNCTION__ "\n", node.name.c_str());
            return false;
        }

        // Report details of the success
        m_logFn(LogLevel::Info, "Initialized AMD FidelityFXSDK upscaling context for node \"%s\"\nversionid 0x%016llx, %s\ntotalUsageInBytes %0.2f MB aliasableUsageInBytes %0.2f MB", node.name.c_str(), getVersion.versionId, getVersion.versionName, gpuMemoryUsageUpscaler.totalUsageInBytes / 1048576.f, gpuMemoryUsageUpscaler.aliasableUsageInBytes / 1048576.f);
    }

    // If using a reactive mask, make sure the texture is connected
    ExternalNode_AMD_FidelityFXSDK_Upscaling_GenerateReactiveMask_ReactiveMaskMode reactiveMaskMode = (ExternalNode_AMD_FidelityFXSDK_Upscaling_GenerateReactiveMask_ReactiveMaskMode)GetRuntimeVariableValueAllowCast_NoFail<int>(nodeData.reactiveMask.reactiveMaskMode.variable.variableIndex);
    if (reactiveMaskMode != ExternalNode_AMD_FidelityFXSDK_Upscaling_GenerateReactiveMask_ReactiveMaskMode::Off && !textureExists_reactive)
    {
        m_logFn(LogLevel::Error, "Node \"%s\" is set to use a reactive mask, but no texture is connected to the reactive pin.\n", node.name.c_str());
        return false;
    }

    // flush the transitions so we can get the current state of the textures
    m_transitions.Flush(m_commandList);

    // Generate a reactive mask if we should
    if (reactiveMaskMode == ExternalNode_AMD_FidelityFXSDK_Upscaling_GenerateReactiveMask_ReactiveMaskMode::Generate)
    {
        if (!textureExists_colorOpaqueOnly)
        {
            m_logFn(LogLevel::Error, "Node \"%s\" is set to generate a reactive mask, but no texture is connected to the colorOpaqueOnly pin.\n", node.name.c_str());
            return false;
        }

        ffx::DispatchDescUpscaleGenerateReactiveMask dispatchDesc{};
        dispatchDesc.commandList = m_commandList;

        SetFfxApiResourceToTexture(texture_colorOpaqueOnly, node_colorOpaqueOnly, dispatchDesc.colorOpaqueOnly, m_transitions);
        SetFfxApiResourceToTexture(texture_color, node_color, dispatchDesc.colorPreUpscale, m_transitions);
        SetFfxApiResourceToTexture(texture_reactive, node_reactive, dispatchDesc.outReactive, m_transitions);

        dispatchDesc.renderSize = { renderSize[0], renderSize[1] };

        dispatchDesc.scale = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.reactiveMask.scale.variable.variableIndex);
        dispatchDesc.cutoffThreshold = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.reactiveMask.cutoffThreshold.variable.variableIndex);
        dispatchDesc.binaryValue = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.reactiveMask.binaryValue.variable.variableIndex);

        dispatchDesc.flags = 0;
        dispatchDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.reactiveMask.APPLY_TONEMAP.variable.variableIndex) ? FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_TONEMAP : 0;
        dispatchDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.reactiveMask.APPLY_INVERSETONEMAP.variable.variableIndex) ? FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_INVERSETONEMAP : 0;
        dispatchDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.reactiveMask.APPLY_THRESHOLD.variable.variableIndex) ? FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_THRESHOLD : 0;
        dispatchDesc.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.reactiveMask.USE_COMPONENTS_MAX.variable.variableIndex) ? FFX_UPSCALE_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX : 0;

        ffx::ReturnCode retCode = ffx::Dispatch(context.m_UpscalingContext, dispatchDesc);
        if (retCode != ffx::ReturnCode::Ok)
        {
            m_logFn(LogLevel::Error, "Could not dispatch generateing reactive mask for node \"%s\" in " __FUNCTION__ "\n", node.name.c_str());
            return false;
        }
    }

    // Query new FSR resource requirements (FSR4 API) after context creation
    {
        ffxQueryDescUpscaleGetResourceRequirements reqResourceReq = { 0 };
        reqResourceReq.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_RESOURCE_REQUIREMENTS;

        ffxReturnCode_t qret = ffxQuery(&context.m_UpscalingContext, &reqResourceReq.header);
        if (qret == FFX_API_RETURN_OK)
        {
            uint64_t required = reqResourceReq.required_resources;
            uint64_t optional = reqResourceReq.optional_resources;

            // Interpret the bitmask using values from ffx_upscale.h (FFX_API_QUERY_RESOURCE_*)

            // We have checked the existence of color texture already at the start of this function
            //bool requiresColor = (required & FFX_API_QUERY_RESOURCE_INPUT_COLOR) != 0;
            bool requiresDepth = (required & FFX_API_QUERY_RESOURCE_INPUT_DEPTH) != 0;
            bool requiresMV = (required & FFX_API_QUERY_RESOURCE_INPUT_MV) != 0;
            // Disable exposure texture check for now as there is bug in ffxQueryDescUpscaleGetResourceRequirements
            //bool requiresExposure = (required & FFX_API_QUERY_RESOURCE_INPUT_EXPOSURE) != 0;
            bool requiresReactive = (required & FFX_API_QUERY_RESOURCE_INPUT_REACTIVEMASK) != 0;
            //bool requiresTransparency = (required & FFX_API_QUERY_RESOURCE_INPUT_TRANSPARENCYCOMPOSITION) != 0;

            // Use these booleans to validate inputs or allocate/enable optional resources.
            if (requiresDepth && !textureExists_depth)
            {
                m_logFn(LogLevel::Error, "FSR upscaler requires 'depth' input but it is not connected for node \"%s\"\n", node.name.c_str());
                return false;
            }

            if (requiresMV && !textureExists_motionVectors)
            {
                m_logFn(LogLevel::Error, "FSR upscaler requires 'motionVectors' input but it is not connected for node \"%s\"\n", node.name.c_str());
                return false;
            }

            // Disable exposure texture check for now as there is bug in ffxQueryDescUpscaleGetResourceRequirements
            //if (requiresExposure && !textureExists_exposure)
            //{
            //    m_logFn(LogLevel::Error, "FSR upscaler requires 'exposure' input but it is not connected for node \"%s\"\n", node.name.c_str());
            //    return false;
            //}

            if (requiresReactive && !textureExists_reactive)
            {
                m_logFn(LogLevel::Error, "FSR upscaler requires 'reactive' input but it is not connected for node \"%s\"\n", node.name.c_str());
                return false;
            }
        }
        else if (qret == FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE)
        {
            // Provider binary is old and doesn't support this new query type. Fall back.
            m_logFn(LogLevel::Warn, "Provider does not support upscale resource-requirements query for node \"%s\"; falling back to older queries.", node.name.c_str());
        }
        else
        {
            m_logFn(LogLevel::Warn, "ffxQuery(FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_RESOURCE_REQUIREMENTS) returned %u for node \"%s\"; continuing.", (unsigned)qret, node.name.c_str());
        }
    }

    // Execute the upscaling
    {
        ffx::DispatchDescUpscale dispatchUpscale{};
        dispatchUpscale.commandList = m_commandList;

        // Set textures
        SetFfxApiResourceToTexture(texture_color, node_color, dispatchUpscale.color, m_transitions);
        SetFfxApiResourceToTexture(texture_depth, node_depth, dispatchUpscale.depth, m_transitions);
        SetFfxApiResourceToTexture(texture_motionVectors, node_motionVectors, dispatchUpscale.motionVectors, m_transitions);
        SetFfxApiResourceToTexture(texture_exposure, node_exposure, dispatchUpscale.exposure, m_transitions);
        SetFfxApiResourceToTexture(texture_reactive, node_reactive, dispatchUpscale.reactive, m_transitions);
        //SetFfxApiResourceToTexture(texture_transparencyAndComposition, node_transparencyAndComposition, dispatchUpscale.transparencyAndComposition, m_transitions);
        SetFfxApiResourceToTexture(texture_output, node_output, dispatchUpscale.output, m_transitions);

        // Set Params
        GetRuntimeVariableValueAllowCast_NoFail(nodeData.jitterOffset.variable.variableIndex, &dispatchUpscale.jitterOffset.x, 2);

        // This can happen on reload sometimes
        if (isnan(dispatchUpscale.jitterOffset.x) || isnan(dispatchUpscale.jitterOffset.y))
        {
            m_logFn(LogLevel::Warn, "nan jitter offset component for node \"%s\" in " __FUNCTION__ "\n", node.name.c_str());
            // Jitter shouldn't ever be zero
            dispatchUpscale.jitterOffset.x = 0.1f;
            dispatchUpscale.jitterOffset.y = 0.1f;
        }
        // Jitter shouldn't ever be zero
        else if (dispatchUpscale.jitterOffset.x == 0.0f && dispatchUpscale.jitterOffset.y == 0.0f)
        {
            m_logFn(LogLevel::Warn, "zero jitter offset component for node \"%s\" in " __FUNCTION__ "\n", node.name.c_str());
            dispatchUpscale.jitterOffset.x = 0.2f;
            dispatchUpscale.jitterOffset.y = 0.2f;
        }

        dispatchUpscale.jitterOffset.y *= -1.0f;

        GetRuntimeVariableValueAllowCast_NoFail(nodeData.motionVectorScale.variable.variableIndex, &dispatchUpscale.motionVectorScale.x, 2);
        dispatchUpscale.renderSize = { renderSize[0], renderSize[1] };
        dispatchUpscale.upscaleSize = { upscaleSize[0], upscaleSize[1] };
        dispatchUpscale.enableSharpening = GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.enableSharpening.variable.variableIndex);
        dispatchUpscale.sharpness = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.sharpness.variable.variableIndex);
        dispatchUpscale.frameTimeDelta = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.frameTimeDelta.variable.variableIndex);
        dispatchUpscale.preExposure = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.preExposure.variable.variableIndex);

        dispatchUpscale.reset = false;
        if (nodeData.reset.variableIndex != -1)
            dispatchUpscale.reset = GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.reset.variableIndex);

        // Near and far plane
        dispatchUpscale.cameraNear = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.cameraNear.variable.variableIndex);
        dispatchUpscale.cameraFar = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.cameraFar.variable.variableIndex);
        if (GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DEPTH_INVERTED.variable.variableIndex))
        {
            if (GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.ENABLE_DEPTH_INFINITE.variable.variableIndex))
                dispatchUpscale.cameraNear = FLT_MAX;
            else
                std::swap(dispatchUpscale.cameraNear, dispatchUpscale.cameraFar);
        }

        dispatchUpscale.cameraFovAngleVertical = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.cameraFovAngleVertical.variable.variableIndex);
        dispatchUpscale.viewSpaceToMetersFactor = GetRuntimeVariableValueAllowCast_NoFail<float>(nodeData.viewSpaceToMetersFactor.variable.variableIndex);

        // Set flags
        dispatchUpscale.flags = 0;
        dispatchUpscale.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.DRAW_DEBUG_VIEW.variable.variableIndex) ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0;
        dispatchUpscale.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.NON_LINEAR_COLOR_SRGB.variable.variableIndex) ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB : 0;
        dispatchUpscale.flags |= GetRuntimeVariableValueAllowCast_NoFail<bool>(nodeData.NON_LINEAR_COLOR_PQ.variable.variableIndex) ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ : 0;

        // Dispatch
        ffx::ReturnCode retCode = ffx::Dispatch(context.m_UpscalingContext, dispatchUpscale);
        if (retCode != ffx::ReturnCode::Ok)
        {
            m_logFn(LogLevel::Error, "Could not dispatch upscaling for node \"%s\" in " __FUNCTION__ "\n", node.name.c_str());
            return false;
        }
    }

    // Restore the descriptor heaps
    SetDescriptorHeaps();

    // Publish the output
    PublishTexture(node, runtimeData_, *this, node_output, texture_output, "output", " (UAV - After)", true);
    if (textureExists_reactive)
        PublishTexture(node, runtimeData_, *this, node_reactive, texture_reactive, "reactive", " (UAV - After)", true);

    // Destroy any contexts that haven't been used for a while
    auto it = std::remove_if(runtimeData.m_contexts.begin(), runtimeData.m_contexts.end(),
        [this](auto& context) {
            context.m_age++;
            if (context.m_age > 10)
            {
                if (context.m_UpscalingContext)
                    ffx::DestroyContext(context.m_UpscalingContext);
                return true;
            }
            return false;
        });
    runtimeData.m_contexts.erase(it, runtimeData.m_contexts.end());

    return true;
}

bool GigiInterpreterPreviewWindowDX12::OnNodeAction(const RenderGraphNode_Action_External& node, RuntimeTypes::RenderGraphNode_Action_External& runtimeData, NodeAction nodeAction)
{
    ScopeProfiler _p(m_profiler, (node.c_shorterTypeName + ": " + node.name).c_str(), nullptr, nodeAction == NodeAction::Execute, true);

    bool executionConditionMet = EvaluateCondition(node.condition);
    runtimeData.m_conditionIsTrue = executionConditionMet;

    switch (node.externalNodeData._index)
    {
#include "external/df_serialize/_common.h"
#define VARIANT_TYPE(_TYPE, _NAME, _DEFAULT, _DESCRIPTION) \
            case ExternalNodeData::c_index_##_NAME: return OnNodeAction_External_##_NAME(node, runtimeData, nodeAction);
#include "external/df_serialize/_fillunsetdefines.h"
#include "Schemas/ExternalNodeVariant.h"

    default:
    {
        GigiAssert(false, "Unknown External Node Type");
        return false;
    }
    }
}

// ---------------------------------------------------------------------------
// ONNX / DirectML node
// ---------------------------------------------------------------------------
//
// Runs a pre-trained ONNX model via ONNX Runtime's DirectML execution
// provider, fully on the GPU. No CPU round-trip for the tensors.
//
// Plumbing:
//   - The node owns two internal ID3D12 buffer resources (input + output),
//     sized to 1*C*H*W*sizeof(float) where C = Texture2DArray slices * 4.
//     DML's CreateGPUAllocationFromD3DResource only accepts buffer resources.
//   - A compute shader (NHWCNCHWTransposeHLSL.h) shuttles between Gigi's
//     Texture2DArray<float4> (NHWC with float4 slice packing) and the
//     linear NCHW buffer layout DML expects.
//   - DML runs on its own dedicated D3D12 command queue; we synchronize the
//     pre-transpose write and the post-transpose read with a shared fence.
//
// Per-Execute sequence:
//   1. Record NHWC->NCHW transpose onto Gigi's main command list (reads
//      input Texture2DArray, writes input buffer).
//   2. Close Gigi's main command list, ExecuteCommandLists on main queue,
//      Signal(fence, N). Reset the list so downstream recording can resume.
//   3. DML queue Wait(fence, N). session.Run(runOpts, ioBinding) submits
//      DML ops to its queue and CPU-blocks until they complete.
//   4. DML queue Signal(fence, N+1). Main queue Wait(fence, N+1) before any
//      subsequent submission reads the output buffer.
//   5. Record NCHW->NHWC transpose onto Gigi's main command list (reads
//      output buffer, writes output Texture2DArray).
//
// session.Run CPU-blocks internally so step 4's main-queue Wait is redundant
// for correctness but kept for explicitness and to let us skip the CPU wait
// once ORT gains an async Run variant.

namespace
{
    size_t HashOnnxSessionConfig(const ExternalNode_ONNX& nodeData)
    {
        size_t h = std::hash<std::string>()(nodeData.fileName);
        auto mix = [](size_t& acc, size_t v) { acc ^= v + 0x9e3779b9 + (acc << 6) + (acc >> 2); };
        mix(h, std::hash<int>()((int)nodeData.precision));
        for (const auto& dim : nodeData.freeDimensionOverrides)
        {
            mix(h, std::hash<std::string>()(dim.name));
            mix(h, std::hash<int>()(dim.value));
        }
        return h;
    }

    // Build the root signature used by the NHWC<->NCHW transpose shader.
    // Uses NHWCNCHWTranspose_DescriptorSlot offsets so the root sig and the
    // per-dispatch descriptor-table builder stay synchronized.
    bool CreateTransposeRootSignature(ID3D12Device* device, ID3D12RootSignature** outSig, LogFn logFn)
    {
        D3D12_DESCRIPTOR_RANGE ranges[NHWCNCHW_SLOT_COUNT] = {};

        ranges[NHWCNCHW_SLOT_INTEX_SRV].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[NHWCNCHW_SLOT_INTEX_SRV].NumDescriptors = 1;
        ranges[NHWCNCHW_SLOT_INTEX_SRV].BaseShaderRegister = 0; // t0
        ranges[NHWCNCHW_SLOT_INTEX_SRV].RegisterSpace = 0;
        ranges[NHWCNCHW_SLOT_INTEX_SRV].OffsetInDescriptorsFromTableStart = NHWCNCHW_SLOT_INTEX_SRV;

        ranges[NHWCNCHW_SLOT_OUTBUF_UAV].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[NHWCNCHW_SLOT_OUTBUF_UAV].NumDescriptors = 1;
        ranges[NHWCNCHW_SLOT_OUTBUF_UAV].BaseShaderRegister = 0; // u0
        ranges[NHWCNCHW_SLOT_OUTBUF_UAV].RegisterSpace = 0;
        ranges[NHWCNCHW_SLOT_OUTBUF_UAV].OffsetInDescriptorsFromTableStart = NHWCNCHW_SLOT_OUTBUF_UAV;

        ranges[NHWCNCHW_SLOT_INBUF_SRV].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[NHWCNCHW_SLOT_INBUF_SRV].NumDescriptors = 1;
        ranges[NHWCNCHW_SLOT_INBUF_SRV].BaseShaderRegister = 1; // t1
        ranges[NHWCNCHW_SLOT_INBUF_SRV].RegisterSpace = 0;
        ranges[NHWCNCHW_SLOT_INBUF_SRV].OffsetInDescriptorsFromTableStart = NHWCNCHW_SLOT_INBUF_SRV;

        ranges[NHWCNCHW_SLOT_OUTTEX_UAV].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[NHWCNCHW_SLOT_OUTTEX_UAV].NumDescriptors = 1;
        ranges[NHWCNCHW_SLOT_OUTTEX_UAV].BaseShaderRegister = 1; // u1
        ranges[NHWCNCHW_SLOT_OUTTEX_UAV].RegisterSpace = 0;
        ranges[NHWCNCHW_SLOT_OUTTEX_UAV].OffsetInDescriptorsFromTableStart = NHWCNCHW_SLOT_OUTTEX_UAV;

        ranges[NHWCNCHW_SLOT_CB_CBV].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        ranges[NHWCNCHW_SLOT_CB_CBV].NumDescriptors = 1;
        ranges[NHWCNCHW_SLOT_CB_CBV].BaseShaderRegister = 0; // b0
        ranges[NHWCNCHW_SLOT_CB_CBV].RegisterSpace = 0;
        ranges[NHWCNCHW_SLOT_CB_CBV].OffsetInDescriptorsFromTableStart = NHWCNCHW_SLOT_CB_CBV;

        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        param.DescriptorTable.NumDescriptorRanges = _countof(ranges);
        param.DescriptorTable.pDescriptorRanges = ranges;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 1;
        desc.pParameters = &param;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sig = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        if (FAILED(hr))
        {
            logFn(LogLevel::Error, "ONNX node: D3D12SerializeRootSignature failed (hr=0x%08X): %s",
                hr, err ? (const char*)err->GetBufferPointer() : "(no error blob)");
            if (sig) sig->Release();
            if (err) err->Release();
            return false;
        }
        hr = device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(outSig));
        if (sig) sig->Release();
        if (err) err->Release();
        if (FAILED(hr))
        {
            logFn(LogLevel::Error, "ONNX node: CreateRootSignature failed (hr=0x%08X).", hr);
            return false;
        }
        return true;
    }

    // Write the inline HLSL to a temp file and compile a PSO from it.
    bool CompileTransposePSO(const GigiInterpreterPreviewWindowDX12& interpreter, ID3D12Device* device,
                             ID3D12RootSignature* rootSig, ID3D12PipelineState** outPSO)
    {
        std::string hlslFile = (std::filesystem::path(interpreter.GetTempDirectory()) / "__GIGI_ONNX__" / "NHWCNCHWTranspose.hlsl").string();
        std::filesystem::create_directories(std::filesystem::path(hlslFile).remove_filename());

        FILE* f = nullptr;
        fopen_s(&f, hlslFile.c_str(), "wb");
        if (!f)
        {
            interpreter.GetLogFn()(LogLevel::Error, "ONNX node: could not open transpose HLSL temp file \"%s\" for write.", hlslFile.c_str());
            return false;
        }
        fwrite(s_NHWCNCHWTransposeHLSL, 1, strlen(s_NHWCNCHWTransposeHLSL), f);
        fclose(f);

        ShaderCompilationInfo info;
        info.fileName = hlslFile;
        info.entryPoint = "csmain";
        info.shaderModel = "cs_6_1";
        info.debugName = "ONNX_NHWCNCHWTranspose";
        info.flags |= ShaderCompilationFlags::WarningsAsErrors;

        MakeComputePSO_dxc(device, info, rootSig, outPSO, interpreter.GetLogFn());
        if (!*outPSO)
        {
            interpreter.GetLogFn()(LogLevel::Error, "ONNX node: MakeComputePSO_dxc returned null for transpose shader. HLSL path: %s", hlslFile.c_str());
            return false;
        }
        return true;
    }
}

bool GigiInterpreterPreviewWindowDX12::OnNodeAction_External_ONNX(const RenderGraphNode_Action_External& node, RuntimeTypes::RenderGraphNode_Action_External& runtimeData_, NodeAction nodeAction)
{
    if (nodeAction == NodeAction::Init)
        return true;

    if (!runtimeData_.m_conditionIsTrue)
        return true;

    const ExternalNode_ONNX& nodeData = node.externalNodeData.ONNX;
    RuntimeTypes::RenderGraphNode_Action_External::ONNX& state = runtimeData_.m_ONNX;

    if (nodeData.fileName.empty())
    {
        m_logFn(LogLevel::Error, "ONNX node \"%s\": fileName is empty.", node.name.c_str());
        return false;
    }

    // ---- Resolve input/output Gigi resources ----
    auto GetTexture = [this](int resourceNodeIndex) -> std::pair<const RenderGraphNode_Resource_Texture*, RuntimeTypes::RenderGraphNode_Resource_Texture*>
    {
        if (resourceNodeIndex < 0 || resourceNodeIndex >= (int)m_renderGraph.nodes.size())
            return { nullptr, nullptr };
        const RenderGraphNode& rn = m_renderGraph.nodes[resourceNodeIndex];
        if (rn._index != RenderGraphNode::c_index_resourceTexture)
            return { nullptr, nullptr };
        const RenderGraphNode_Resource_Texture* texNode = &rn.resourceTexture;
        bool exists = false;
        RuntimeTypes::RenderGraphNode_Resource_Texture& rt = GetRuntimeNodeData_RenderGraphNode_Resource_Texture(texNode->name.c_str(), exists);
        if (!exists)
            return { texNode, nullptr };
        return { texNode, &rt };
    };

    auto [inputNode, inputRT] = GetTexture(nodeData.input.resourceNodeIndex);
    auto [outputNode, outputRT] = GetTexture(nodeData.output.resourceNodeIndex);

    if (!inputNode || !outputNode)
    {
        m_logFn(LogLevel::Error, "ONNX node \"%s\": input and output must both be Texture2D or Texture2DArray resources.", node.name.c_str());
        return false;
    }
    if (!inputRT || !inputRT->m_resource || !outputRT || !outputRT->m_resource)
    {
        return true; // resources may be allocated later; skip quietly
    }

    // Publish input to inspector.
    {
        std::string label = node.name + std::string(".input: ") + inputNode->name + std::string(" (SRV)");
        runtimeData_.HandleViewableTexture(*this, TextureDimensionTypeToViewableResourceType(inputNode->dimension),
            label.c_str(), inputRT->m_resource, inputRT->m_format, inputRT->m_size, inputRT->m_numMips, false, false);
    }

    // ---- Resolve concrete NCHW shapes from Texture2DArray dims ----
    auto ResolveShape = [&](const std::vector<int64_t>& modelShape, const RuntimeTypes::RenderGraphNode_Resource_Texture& rt, const char* which,
                            std::vector<int64_t>& outShape, int64_t& outElems) -> bool
    {
        if (modelShape.size() != 4)
        {
            m_logFn(LogLevel::Error, "ONNX node \"%s\": \"%s\" tensor must be 4-D NCHW (got %zu dims).", node.name.c_str(), which, modelShape.size());
            return false;
        }

        DXGI_FORMAT_Info formatInfo = Get_DXGI_FORMAT_Info(rt.m_format);

        const int64_t W = rt.m_size[0];
        const int64_t H = rt.m_size[1];
        const int64_t C = (int64_t)rt.m_size[2] * formatInfo.channelCount;
        const int64_t want[4] = { 1, C, H, W };
        outShape.assign(modelShape.begin(), modelShape.end());
        outElems = 1;
        for (int i = 0; i < 4; ++i)
        {
            if (outShape[i] < 0) outShape[i] = want[i];
            else if (outShape[i] != want[i])
            {
                m_logFn(LogLevel::Error,
                    "ONNX node \"%s\": %s shape is (%lld, %lld, %lld, %lld) but wants (%lld, %lld, %lld, %lld)",
                    node.name.c_str(), which, outShape[0], outShape[1], outShape[2], outShape[3], want[0], want[1], want[2], want[3]);
                return false;
            }
            outElems *= outShape[i];
        }
        return true;
    };

    // ---- Lazy-init ORT env + DML device + DML queue + DML fence + session ----
    const size_t wantConfigHash = HashOnnxSessionConfig(nodeData);
    const bool modelChanged = state.m_ortSession == nullptr || state.m_sessionConfigHash != wantConfigHash;

    if (modelChanged)
    {
        // Tear down session-scoped state (keep env/device/queue/fence to reuse).
        if (state.m_ortIoBinding)     { delete reinterpret_cast<Ort::IoBinding*>(state.m_ortIoBinding); state.m_ortIoBinding = nullptr; }
        if (state.m_ortMemInfoDML)    { delete reinterpret_cast<Ort::MemoryInfo*>(state.m_ortMemInfoDML); state.m_ortMemInfoDML = nullptr; }
        if (state.m_ortSession)       { delete reinterpret_cast<Ort::Session*>(state.m_ortSession); state.m_ortSession = nullptr; }

        try
        {
            if (!state.m_ortEnv)
                state.m_ortEnv = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "gigi_onnx_node");

            // DML device.
            if (!state.m_dmlDevice)
            {
                IDMLDevice* dmlDevice = nullptr;
                HRESULT hr = DMLCreateDevice(m_device, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dmlDevice));
                if (FAILED(hr))
                {
                    m_logFn(LogLevel::Error, "ONNX node \"%s\": DMLCreateDevice failed (hr=0x%08X).", node.name.c_str(), hr);
                    return false;
                }
                state.m_dmlDevice = dmlDevice;
            }

            // DML runs on Gigi's main command queue (same pattern as
            // Microsoft's DxDispatch sample): ORT submits DML command lists
            // to this queue, and by submitting AFTER our pre-transpose
            // command list gets ExecuteCommandLists'd, DML's work is
            // naturally ordered after ours. No cross-queue fence needed.
            (void)state.m_dmlCommandQueue; // kept on struct for API compat; not used in v2 flow

            Ort::SessionOptions so;
            so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            so.DisableMemPattern();
            so.SetExecutionMode(ORT_SEQUENTIAL);
            for (const auto& dim : nodeData.freeDimensionOverrides)
            {
                OrtStatus* s = Ort::GetApi().AddFreeDimensionOverrideByName(so, dim.name.c_str(), (int64_t)dim.value);
                if (s)
                {
                    std::string msg = Ort::GetApi().GetErrorMessage(s);
                    Ort::GetApi().ReleaseStatus(s);
                    m_logFn(LogLevel::Error, "ONNX node \"%s\": AddFreeDimensionOverrideByName(%s) failed: %s", node.name.c_str(), dim.name.c_str(), msg.c_str());
                    return false;
                }
            }

            const OrtDmlApi* dmlApi = nullptr;
            Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dmlApi));
            if (!dmlApi)
            {
                m_logFn(LogLevel::Error, "ONNX node \"%s\": ORT DML API not available.", node.name.c_str());
                return false;
            }

            OrtStatus* st = dmlApi->SessionOptionsAppendExecutionProvider_DML1(so,
                reinterpret_cast<IDMLDevice*>(state.m_dmlDevice),
                m_commandQueue);
            if (st)
            {
                std::string msg = Ort::GetApi().GetErrorMessage(st);
                Ort::GetApi().ReleaseStatus(st);
                m_logFn(LogLevel::Error, "ONNX node \"%s\": DML EP append failed: %s", node.name.c_str(), msg.c_str());
                return false;
            }

            std::filesystem::path onnxPath = std::filesystem::path(m_renderGraph.baseDirectory) / nodeData.fileName;
            std::wstring onnxPathW = onnxPath.wstring();
            Ort::Env& env = *reinterpret_cast<Ort::Env*>(state.m_ortEnv);
            state.m_ortSession = new Ort::Session(env, onnxPathW.c_str(), so);
            state.m_loadedFile = onnxPath.string();

            Ort::Session& session = *reinterpret_cast<Ort::Session*>(state.m_ortSession);
            if (session.GetInputCount() != 1 || session.GetOutputCount() != 1)
            {
                m_logFn(LogLevel::Error, "ONNX node \"%s\": model must have exactly 1 input and 1 output (got %zu in, %zu out).",
                    node.name.c_str(), session.GetInputCount(), session.GetOutputCount());
                return false;
            }

            Ort::AllocatorWithDefaultOptions alloc;
            auto inNameAlloc = session.GetInputNameAllocated(0, alloc);
            state.m_inputName = inNameAlloc.get();
            auto outNameAlloc = session.GetOutputNameAllocated(0, alloc);
            state.m_outputName = outNameAlloc.get();
            state.m_inputShape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            state.m_outputShape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();

            // DML-typed memory info (matches the EP's allocator registration name).
            state.m_ortMemInfoDML = new Ort::MemoryInfo("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault);

            state.m_sessionConfigHash = wantConfigHash;

            m_logFn(LogLevel::Info, "ONNX node \"%s\": loaded %s (input \"%s\", output \"%s\").",
                node.name.c_str(), state.m_loadedFile.c_str(),
                state.m_inputName.c_str(), state.m_outputName.c_str());
        }
        catch (const Ort::Exception& e)
        {
            m_logFn(LogLevel::Error, "ONNX node \"%s\": ORT exception during init: %s", node.name.c_str(), e.what());
            return false;
        }
    }

    std::vector<int64_t> inShape, outShape;
    int64_t inElems = 0, outElems = 0;
    if (!ResolveShape(state.m_inputShape, *inputRT, "input", inShape, inElems)) return false;
    if (!ResolveShape(state.m_outputShape, *outputRT, "output", outShape, outElems)) return false;

    // ---- Lazy-create transpose root sig + PSO (once) ----
    if (!state.m_transposeRootSig)
    {
        ID3D12RootSignature* sig = nullptr;
        if (!CreateTransposeRootSignature(m_device, &sig, m_logFn))
            return false;
        state.m_transposeRootSig = sig;
    }
    if (!state.m_transposePSO)
    {
        ID3D12PipelineState* pso = nullptr;
        if (!CompileTransposePSO(*this, m_device, reinterpret_cast<ID3D12RootSignature*>(state.m_transposeRootSig), &pso))
            return false;
        state.m_transposePSO = pso;
    }

    // ---- Allocate / reallocate internal tensor buffers when shape changes ----
    const int inputBufBytes = (int)(inElems * sizeof(float));
    const int outputBufBytes = (int)(outElems * sizeof(float));
    const bool buffersOutOfDate =
        state.m_inputBuffer == nullptr || state.m_inputBufferSize != inputBufBytes ||
        state.m_outputBuffer == nullptr || state.m_outputBufferSize != outputBufBytes;

    if (buffersOutOfDate)
    {
        const OrtDmlApi* dmlApi = nullptr;
        Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dmlApi));
        if (state.m_inputDmlAlloc)   { if (dmlApi) dmlApi->FreeGPUAllocation(state.m_inputDmlAlloc);   state.m_inputDmlAlloc = nullptr; }
        if (state.m_outputDmlAlloc)  { if (dmlApi) dmlApi->FreeGPUAllocation(state.m_outputDmlAlloc);  state.m_outputDmlAlloc = nullptr; }
        if (state.m_ortIoBinding)    { delete reinterpret_cast<Ort::IoBinding*>(state.m_ortIoBinding); state.m_ortIoBinding = nullptr; }
        if (state.m_inputBuffer)     { reinterpret_cast<IUnknown*>(state.m_inputBuffer)->Release(); state.m_inputBuffer = nullptr; }
        if (state.m_outputBuffer)    { reinterpret_cast<IUnknown*>(state.m_outputBuffer)->Release(); state.m_outputBuffer = nullptr; }

        // UAV buffers, ALLOW_UNORDERED_ACCESS so the transpose shader can write.
        state.m_inputBuffer = CreateBuffer(m_device, (unsigned int)inputBufBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
            D3D12_HEAP_TYPE_DEFAULT, "ONNX input tensor buffer");
        state.m_outputBuffer = CreateBuffer(m_device, (unsigned int)outputBufBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
            D3D12_HEAP_TYPE_DEFAULT, "ONNX output tensor buffer");
        if (!state.m_inputBuffer || !state.m_outputBuffer)
        {
            m_logFn(LogLevel::Error, "ONNX node \"%s\": failed to create internal tensor buffers.", node.name.c_str());
            return false;
        }
        state.m_inputBufferSize = inputBufBytes;
        state.m_outputBufferSize = outputBufBytes;

        // Wrap the buffers as DML GPU allocations.
        if (!dmlApi)
        {
            m_logFn(LogLevel::Error, "ONNX node \"%s\": ORT DML API not available (post-buf alloc).", node.name.c_str());
            return false;
        }
        OrtStatus* s1 = dmlApi->CreateGPUAllocationFromD3DResource(reinterpret_cast<ID3D12Resource*>(state.m_inputBuffer), &state.m_inputDmlAlloc);
        OrtStatus* s2 = dmlApi->CreateGPUAllocationFromD3DResource(reinterpret_cast<ID3D12Resource*>(state.m_outputBuffer), &state.m_outputDmlAlloc);
        if (s1 || s2)
        {
            if (s1) { m_logFn(LogLevel::Error, "ONNX node \"%s\": CreateGPUAllocationFromD3DResource(input buf) failed: %s", node.name.c_str(), Ort::GetApi().GetErrorMessage(s1)); Ort::GetApi().ReleaseStatus(s1); }
            if (s2) { m_logFn(LogLevel::Error, "ONNX node \"%s\": CreateGPUAllocationFromD3DResource(output buf) failed: %s", node.name.c_str(), Ort::GetApi().GetErrorMessage(s2)); Ort::GetApi().ReleaseStatus(s2); }
            return false;
        }

        // Build IoBinding.
        Ort::Session& session = *reinterpret_cast<Ort::Session*>(state.m_ortSession);
        Ort::MemoryInfo& memInfoDML = *reinterpret_cast<Ort::MemoryInfo*>(state.m_ortMemInfoDML);
        auto* binding = new Ort::IoBinding(session);
        try
        {
            Ort::Value inVal = Ort::Value::CreateTensor(memInfoDML, state.m_inputDmlAlloc, (size_t)inputBufBytes, inShape.data(), inShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
            Ort::Value outVal = Ort::Value::CreateTensor(memInfoDML, state.m_outputDmlAlloc, (size_t)outputBufBytes, outShape.data(), outShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
            binding->BindInput(state.m_inputName.c_str(), inVal);
            binding->BindOutput(state.m_outputName.c_str(), outVal);
        }
        catch (const Ort::Exception& e)
        {
            delete binding;
            m_logFn(LogLevel::Error, "ONNX node \"%s\": IoBinding setup failed: %s", node.name.c_str(), e.what());
            return false;
        }
        state.m_ortIoBinding = binding;
    }

    // ---- Build an upload-constant-buffer with the CBStruct for this dispatch ----
    auto RecordTransposeDispatch = [&](bool textureToBuffer) -> bool
    {
        RuntimeTypes::RenderGraphNode_Resource_Texture* rt = textureToBuffer ? inputRT : outputRT;
        DXGI_FORMAT_Info formatInfo = Get_DXGI_FORMAT_Info(rt->m_format);

        NHWCNCHWTranspose_CBStruct cb = {};
        cb.W = (int)rt->m_size[0];
        cb.H = (int)rt->m_size[1];
        cb.C = (int)(rt->m_size[2] * formatInfo.channelCount);
        cb.mode = textureToBuffer ? NHWCNCHW_TRANSPOSE_TEXTURE_TO_BUFFER : NHWCNCHW_TRANSPOSE_BUFFER_TO_TEXTURE;

        //if (textureToBuffer)
            //m_logFn(LogLevel::Info, "ONNX node \"%s\": recording pre-transpose (NHWC->NCHW) W=%d H=%d C=%d.", node.name.c_str(), cb.W, cb.H, cb.C);
        //else
            //m_logFn(LogLevel::Info, "ONNX node \"%s\": recording post-transpose (NCHW->NHWC).", node.name.c_str());

        // 256-byte aligned CB upload.
        const unsigned int cbSizeAligned = (unsigned int)ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(cb));
        UploadBufferTracker::Buffer* cbBuf = m_uploadBufferTracker.GetBuffer(m_device, cbSizeAligned, true);
        {
            void* mapped = nullptr;
            HRESULT hr = cbBuf->buffer->Map(0, nullptr, &mapped);
            if (FAILED(hr))
            {
                m_logFn(LogLevel::Error, "ONNX node \"%s\": CB map failed (hr=0x%08X).", node.name.c_str(), hr);
                return false;
            }
            memset(mapped, 0, cbSizeAligned);
            memcpy(mapped, &cb, sizeof(cb));
            cbBuf->buffer->Unmap(0, nullptr);
        }

        ID3D12Resource* inputBuf = reinterpret_cast<ID3D12Resource*>(state.m_inputBuffer);
        ID3D12Resource* outputBuf = reinterpret_cast<ID3D12Resource*>(state.m_outputBuffer);

        // Descriptors addressed by NHWCNCHWTranspose_DescriptorSlot. Unused
        // slots (for the mode we're NOT running) get a nullptr resource,
        // which DescriptorTableCache turns into a null view.
        std::vector<DescriptorTableCache::ResourceDescriptor> descs(NHWCNCHW_SLOT_COUNT);

        // t0 SRV: input Texture2DArray<float4>. Used only in texture->buffer mode.
        auto& slotInTex = descs[NHWCNCHW_SLOT_INTEX_SRV];
        slotInTex.m_resource = textureToBuffer ? inputRT->m_resource : nullptr;
        slotInTex.m_format = inputRT->m_format;
        slotInTex.m_access = DescriptorTableCache::AccessType::SRV;
        slotInTex.m_resourceType = DescriptorTableCache::ResourceType::Texture2DArray;
        slotInTex.m_count = inputRT->m_size[2];

        // u0 UAV: RWStructuredBuffer<float> that holds the NCHW tensor we'll
        // hand to DML. Used only in texture->buffer mode.
        auto& slotOutBuf = descs[NHWCNCHW_SLOT_OUTBUF_UAV];
        slotOutBuf.m_resource = textureToBuffer ? inputBuf : nullptr;
        slotOutBuf.m_format = DXGI_FORMAT_UNKNOWN;
        slotOutBuf.m_access = DescriptorTableCache::AccessType::UAV;
        slotOutBuf.m_resourceType = DescriptorTableCache::ResourceType::Buffer;
        slotOutBuf.m_stride = sizeof(float);
        slotOutBuf.m_count = (unsigned int)inElems;
        slotOutBuf.m_raw = false;

        // t1 SRV: StructuredBuffer<float> containing DML's NCHW output. Used
        // only in buffer->texture mode.
        auto& slotInBuf = descs[NHWCNCHW_SLOT_INBUF_SRV];
        slotInBuf.m_resource = textureToBuffer ? nullptr : outputBuf;
        slotInBuf.m_format = DXGI_FORMAT_UNKNOWN;
        slotInBuf.m_access = DescriptorTableCache::AccessType::SRV;
        slotInBuf.m_resourceType = DescriptorTableCache::ResourceType::Buffer;
        slotInBuf.m_stride = sizeof(float);
        slotInBuf.m_count = (unsigned int)outElems;
        slotInBuf.m_raw = false;

        // u1 UAV: output RWTexture2DArray<float4>. Used only in buffer->texture mode.
        auto& slotOutTex = descs[NHWCNCHW_SLOT_OUTTEX_UAV];
        slotOutTex.m_resource = textureToBuffer ? nullptr : outputRT->m_resource;
        slotOutTex.m_format = outputRT->m_format;
        slotOutTex.m_access = DescriptorTableCache::AccessType::UAV;
        slotOutTex.m_resourceType = DescriptorTableCache::ResourceType::Texture2DArray;
        slotOutTex.m_count = outputRT->m_size[2];

        // b0 CBV. DescriptorTableCache reads CBV SizeInBytes from m_stride —
        // setting m_stride to zero is what silently produced an all-zero CB
        // and made every dispatch a no-op during initial development.
        auto& slotCB = descs[NHWCNCHW_SLOT_CB_CBV];
        slotCB.m_resource = cbBuf->buffer;
        slotCB.m_format = DXGI_FORMAT_UNKNOWN;
        slotCB.m_access = DescriptorTableCache::AccessType::CBV;
        slotCB.m_resourceType = DescriptorTableCache::ResourceType::Buffer;
        slotCB.m_stride = cbSizeAligned;
        slotCB.m_count = 1;

        // Resource transitions.
        if (textureToBuffer)
        {
            m_transitions.Transition(TRANSITION_DEBUG_INFO(inputRT->m_resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            m_transitions.Transition(TRANSITION_DEBUG_INFO(inputBuf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        }
        else
        {
            m_transitions.Transition(TRANSITION_DEBUG_INFO(outputBuf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            m_transitions.Transition(TRANSITION_DEBUG_INFO(outputRT->m_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        }
        m_transitions.Flush(m_commandList);

        D3D12_GPU_DESCRIPTOR_HANDLE table;
        std::string err;
        if (!m_descriptorTableCache.GetDescriptorTable(m_device, m_SRVHeapAllocationTracker, descs.data(), (int)descs.size(), table, err, HEAP_DEBUG_TEXT()))
        {
            m_logFn(LogLevel::Error, "ONNX node \"%s\": descriptor table alloc failed: %s", node.name.c_str(), err.c_str());
            return false;
        }

        m_commandList->SetComputeRootSignature(reinterpret_cast<ID3D12RootSignature*>(state.m_transposeRootSig));
        m_commandList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(state.m_transposePSO));
        m_commandList->SetComputeRootDescriptorTable(0, table);

        // One thread per tensor element; ceil-divide by the shader's
        // numthreads to get threadgroup counts.
        auto CeilDiv = [](int a, int b) { return (a + b - 1) / b; };
        const unsigned int gx = (unsigned int)CeilDiv(cb.W, NHWCNCHW_NUMTHREADS_X);
        const unsigned int gy = (unsigned int)CeilDiv(cb.H, NHWCNCHW_NUMTHREADS_Y);
        const unsigned int gz = (unsigned int)CeilDiv(cb.C, NHWCNCHW_NUMTHREADS_Z);
        m_commandList->Dispatch(gx, gy, gz);
        return true;
    };

    // ---- 1. Record NHWC->NCHW transpose onto Gigi's main command list ----
    if (!RecordTransposeDispatch(/*textureToBuffer=*/true)) return false;

    // After the transpose writes to the input buffer, DML will read it.
    // We need the input buffer to be in a queue-agnostic state (COMMON) and
    // we need the write to be observed by the other queue — achieved via the
    // fence below.
    m_transitions.Transition(TRANSITION_DEBUG_INFO(reinterpret_cast<ID3D12Resource*>(state.m_inputBuffer), D3D12_RESOURCE_STATE_COMMON));
    m_transitions.Flush(m_commandList);

    // ---- 2. Close / submit on main queue so the pre-transpose finishes
    //    before DML reads the input buffer. Reset with a fresh allocator for
    //    resumed recording. Matches Microsoft DxDispatch's Device::ExecuteCommandList.
    HRESULT hr = m_commandList->Close();
    if (FAILED(hr))
    {
        m_logFn(LogLevel::Error, "ONNX node \"%s\": commandList Close failed (hr=0x%08X).", node.name.c_str(), hr);
        return false;
    }
    ID3D12CommandList* lists[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists(1, lists);

    {
        // Fresh allocator for the resumed recording (the interpreter doesn't
        // expose the allocator its Execute() call was given).
        ID3D12CommandAllocator* tempAlloc = nullptr;
        HRESULT hr2 = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAlloc));
        if (FAILED(hr2))
        {
            m_logFn(LogLevel::Error, "ONNX node \"%s\": temp allocator create failed (hr=0x%08X).", node.name.c_str(), hr2);
            return false;
        }
        hr2 = m_commandList->Reset(tempAlloc, nullptr);
        if (FAILED(hr2))
        {
            tempAlloc->Release();
            m_logFn(LogLevel::Error, "ONNX node \"%s\": commandList Reset failed (hr=0x%08X).", node.name.c_str(), hr2);
            return false;
        }
        m_delayedRelease.Add(tempAlloc);
        SetDescriptorHeaps();
    }

    // ---- 3. Run the model. DML submits its ops to m_commandQueue (same
    //    queue we passed into SessionOptionsAppendExecutionProvider_DML1),
    //    which serializes DML's work after our just-submitted pre-transpose. ----
    try
    {
        Ort::Session& session = *reinterpret_cast<Ort::Session*>(state.m_ortSession);
        Ort::IoBinding& binding = *reinterpret_cast<Ort::IoBinding*>(state.m_ortIoBinding);
        Ort::RunOptions runOpts;
        session.Run(runOpts, binding);
    }
    catch (const Ort::Exception& e)
    {
        m_logFn(LogLevel::Error, "ONNX node \"%s\": Run failed: %s", node.name.c_str(), e.what());
        return false;
    }

    // ---- 5. Record NCHW->NHWC transpose ----
    if (!RecordTransposeDispatch(/*textureToBuffer=*/false)) return false;

    // Publish output to inspector.
    {
        std::string label = node.name + std::string(".output: ") + outputNode->name + std::string(" (UAV - After)");
        runtimeData_.HandleViewableTexture(*this, TextureDimensionTypeToViewableResourceType(outputNode->dimension),
            label.c_str(), outputRT->m_resource, outputRT->m_format, outputRT->m_size, outputRT->m_numMips, false, true);
    }

    return true;
}

