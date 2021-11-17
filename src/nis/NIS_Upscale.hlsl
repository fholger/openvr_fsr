// The MIT License(MIT)
// 
// Copyright(c) 2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files(the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#define NIS_SCALER 1
#define NIS_HDR_MODE 0
#define NIS_BLOCK_WIDTH 32
#define NIS_BLOCK_HEIGHT 24
#define NIS_THREAD_GROUP_SIZE 256

cbuffer cb : register(b0)
{
	float kDetectRatio;
	float kDetectThres;
	float kMinContrastRatio;
	float kRatioNorm;

	float kContrastBoost;
	float kEps;
	float kSharpStartY;
	float kSharpScaleY;

	float kSharpStrengthMin;
	float kSharpStrengthScale;
	float kSharpLimitMin;
	float kSharpLimitScale;

	float kScaleX;
	float kScaleY;

	float kDstNormX;
	float kDstNormY;
	float kSrcNormX;
	float kSrcNormY;

	uint kInputViewportOriginX;
	uint kInputViewportOriginY;
	uint kInputViewportWidth;
	uint kInputViewportHeight;

	uint kOutputViewportOriginX;
	uint kOutputViewportOriginY;
	uint kOutputViewportWidth;
	uint kOutputViewportHeight;

	float reserved0;
	float reserved1;

	uint4 centre;
	uint4 radius;
};

SamplerState samplerLinearClamp : register(s0);
Texture2D in_texture            : register(t0);
RWTexture2D<unorm float4> out_texture : register(u0);
Texture2D coef_scaler           : register(t1);
Texture2D coef_usm              : register(t2);


void DirectCopy(uint2 blockIdx, uint threadIdx)
{
	const float4 mul = float4(1, 1, 1, 1) - reserved1 * float4(0, 0.3, 0.3, 0);
	const int dstBlockX = NIS_BLOCK_WIDTH * blockIdx.x;
	const int dstBlockY = NIS_BLOCK_HEIGHT * blockIdx.y;
	for (uint k = threadIdx; k < NIS_BLOCK_WIDTH * NIS_BLOCK_HEIGHT; k += NIS_THREAD_GROUP_SIZE)
	{
		const int2 pos = int2(k % NIS_BLOCK_WIDTH, k / NIS_BLOCK_WIDTH);
		const int dstX = dstBlockX + pos.x;
		const int dstY = dstBlockY + pos.y;
		float3 c = in_texture.SampleLevel(samplerLinearClamp, float2(dstX, dstY) / radius.zw, 0).rgb;
		out_texture[uint2(dstX, dstY)] = float4(c, 1) * mul;
	}
}


#include "NIS_Scaler.h"

[numthreads(NIS_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 blockIdx : SV_GroupID, uint3 threadIdx : SV_GroupThreadID)
{
	uint2 groupCentre = uint2((blockIdx.x * 32) + 16, (blockIdx.y * 24) + 12);
	uint2 dc1 = centre.xy - groupCentre;
	uint2 dc2 = centre.zw - groupCentre;
	if (dot(dc1, dc1) <= radius.y || dot(dc2, dc2) <= radius.y) {
		NVScaler(blockIdx.xy, threadIdx.x);
	}
	else {
		DirectCopy(blockIdx.xy, threadIdx.x);
	}
}
