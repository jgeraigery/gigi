///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

// DX12 code generation for action_external nodes.
//
// The Viewer (Interpreter backend) implements the ONNX/DirectML external
// node today, but the DX12 backend does NOT yet emit generated C++ for it.
// The Viewer implementation uses a Close/Execute/Reset pattern on the
// interpreter's command list that doesn't translate directly to the
// "one long command list per frame" template the DX12 generated code
// follows; adapting it is a follow-up.
//
// Until that's in place, we fail cleanly at Gigi-compile time for any
// External node. That's preferable to emitting code that compiles but
// produces wrong GPU output.

static void MakeStringReplacementForNode(std::unordered_map<std::string, std::ostringstream>& stringReplacementMap, RenderGraph& renderGraph, RenderGraphNode_Action_External& node)
{
    const char* subtype = "(unknown)";
    switch (node.externalNodeData._index)
    {
        case ExternalNodeData::c_index_ONNX:
            subtype = "ONNX";
            break;
        case ExternalNodeData::c_index_AMD_FidelityFXSDK_Upscaling:
            subtype = "AMD_FidelityFXSDK_Upscaling";
            break;
        default:
            break;
    }

    GigiAssert(false,
        "DX12 code generation is not yet implemented for External nodes (node \"%s\", subtype %s). "
        "The Gigi Viewer supports this node type; see GigiViewerDX12/Interpreter/RenderGraphNode_Action_External.cpp. "
        "File an issue or a PR if you need DX12 code generation for this subtype.",
        node.name.c_str(), subtype);
}
