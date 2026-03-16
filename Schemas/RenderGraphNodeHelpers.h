///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#include "TextureFormats.h"

//========================================================
// Helper Structures
//========================================================

STRUCT_BEGIN(NodeReference, "A generic node reference")
    STRUCT_FIELD(std::string, name, "", "The name of the node", 0)

    STRUCT_FIELD(int, nodeIndex, -1, "Calculated for convenience.", SCHEMA_FLAG_NO_SERIALIZE)
STRUCT_END()

STRUCT_INHERIT_BEGIN(TextureNodeReference, NodeReference, "A texture node reference")
    STRUCT_FIELD(struct RenderGraphNode_Resource_Texture*, textureNode, nullptr, "A pointer to the texture node", SCHEMA_FLAG_NO_SERIALIZE)
STRUCT_END()

STRUCT_INHERIT_BEGIN(BufferNodeReference, NodeReference, "A buffer node reference")
    STRUCT_FIELD(struct RenderGraphNode_Resource_Buffer*, bufferNode, nullptr, "A pointer to the buffer node", SCHEMA_FLAG_NO_SERIALIZE)
STRUCT_END()

STRUCT_INHERIT_BEGIN(TextureOrBufferNodeReference, NodeReference, "A texture or buffer node reference")
    STRUCT_FIELD(struct RenderGraphNode_Resource_Texture*, textureNode, nullptr, "A pointer to the texture node", SCHEMA_FLAG_NO_SERIALIZE)
    STRUCT_FIELD(struct RenderGraphNode_Resource_Buffer*, bufferNode, nullptr, "A pointer to the texture node", SCHEMA_FLAG_NO_SERIALIZE)
STRUCT_END()
