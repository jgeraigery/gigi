///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#pragma once

// Bundled HLSL for transposing between the Gigi ONNX node's Texture2DArray<float4>
// tensor representation (NHWC with 4-channel float4 slice packing) and the
// linear NCHW float32 buffer layout that DirectML tensors require.
//
// Two kernels, selected via CBStruct::mode:
//   mode == 0 : TextureToBuffer (NHWC float4 -> NCHW float32)
//   mode == 1 : BufferToTexture (NCHW float32 -> NHWC float4)
//
// Dispatched as (W, H, C) threads where C = slices * 4. One thread writes one
// scalar element in the destination.

#include <stdint.h>

// Direction constants for the transpose shader's "mode" CB field.
// Keep in sync with the HLSL `CBStruct::mode` reads below.
enum NHWCNCHWTranspose_Mode : int32_t
{
    NHWCNCHW_TRANSPOSE_TEXTURE_TO_BUFFER = 0,   // NHWC Texture2DArray<float4> -> NCHW StructuredBuffer<float>
    NHWCNCHW_TRANSPOSE_BUFFER_TO_TEXTURE = 1,   // NCHW StructuredBuffer<float> -> NHWC Texture2DArray<float4>
};

// Slot indices inside the transpose shader's single descriptor table. The
// root signature below and the C++ side that builds the table at dispatch
// time share these offsets, so changing the binding layout only requires
// touching one place.
enum NHWCNCHWTranspose_DescriptorSlot : int
{
    NHWCNCHW_SLOT_INTEX_SRV  = 0,   // t0: Texture2DArray<float4>  (mode 0 input)
    NHWCNCHW_SLOT_OUTBUF_UAV = 1,   // u0: RWStructuredBuffer<float> (mode 0 output)
    NHWCNCHW_SLOT_INBUF_SRV  = 2,   // t1: StructuredBuffer<float>   (mode 1 input)
    NHWCNCHW_SLOT_OUTTEX_UAV = 3,   // u1: RWTexture2DArray<float4>  (mode 1 output)
    NHWCNCHW_SLOT_CB_CBV     = 4,   // b0: NHWCNCHWTranspose_CBStruct
    NHWCNCHW_SLOT_COUNT,
};

// HLSL threadgroup footprint — must match [numthreads(...)] in the shader.
static constexpr int NHWCNCHW_NUMTHREADS_X = 8;
static constexpr int NHWCNCHW_NUMTHREADS_Y = 8;
static constexpr int NHWCNCHW_NUMTHREADS_Z = 1;

// Keep synchronized with HLSL CBStruct below.
struct NHWCNCHWTranspose_CBStruct
{
    int32_t W;
    int32_t H;
    int32_t C;       // total channels = slices * 4
    int32_t mode;    // NHWCNCHWTranspose_Mode
};

static const char* s_NHWCNCHWTransposeHLSL = R"<<<<<(

struct CBStruct
{
    int W;
    int H;
    int C;
    int mode;
};

ConstantBuffer<CBStruct> CB : register(b0);

// Texture -> Buffer (mode 0): InTex is the SRV; OutBuf is the UAV.
Texture2DArray<float4>      InTex  : register(t0);
RWStructuredBuffer<float>   OutBuf : register(u0);

// Buffer -> Texture (mode 1): InBuf is the SRV; OutTex is the UAV.
StructuredBuffer<float>     InBuf  : register(t1);
RWTexture2DArray<float4>    OutTex : register(u1);

// NCHW linear index for (c, y, x): c * (H*W) + y * W + x
uint NCHWIndex(uint c, uint y, uint x, uint H, uint W)
{
    return c * (H * W) + y * W + x;
}

[numthreads(8, 8, 1)]
void csmain(uint3 DTid : SV_DispatchThreadID)
{
    uint x = DTid.x;
    uint y = DTid.y;
    uint c = DTid.z;
    if ((int)x >= CB.W || (int)y >= CB.H || (int)c >= CB.C) return;

    uint slice = c / 4u;
    uint lane  = c % 4u;

    if (CB.mode == 0)
    {
        // Texture2DArray<float4> NHWC -> RWStructuredBuffer<float> NCHW.
        float4 v = InTex[uint3(x, y, slice)];
        float s = (lane == 0u) ? v.x : (lane == 1u) ? v.y : (lane == 2u) ? v.z : v.w;
        OutBuf[NCHWIndex(c, y, x, (uint)CB.H, (uint)CB.W)] = s;
    }
    else
    {
        // StructuredBuffer<float> NCHW -> RWTexture2DArray<float4> NHWC.
        // Every thread for the same (x,y,slice) races on the same texel but
        // writes a distinct component via a read-modify-write. Since each
        // thread owns exactly one of the four float4 lanes, the writes are
        // independent in practice, but D3D doesn't give us componentwise
        // UAV writes on Texture2DArray<float4>. Instead, have lane-0 threads
        // do the whole-float4 assembly: only lane 0 writes, reading the
        // other three lanes from the buffer directly.
        if (lane != 0u) return;
        uint baseC = slice * 4u;
        float4 out4 = float4(0.0f, 0.0f, 0.0f, 0.0f);
        [unroll]
        for (uint i = 0u; i < 4u; ++i)
        {
            uint ch = baseC + i;
            if ((int)ch < CB.C)
            {
                out4[i] = InBuf[NCHWIndex(ch, y, x, (uint)CB.H, (uint)CB.W)];
            }
        }
        OutTex[uint3(x, y, slice)] = out4;
    }
}

)<<<<<";
