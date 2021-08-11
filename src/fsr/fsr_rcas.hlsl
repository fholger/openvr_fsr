#define A_GPU 1
#define A_HLSL 1
//#define A_HALF
#define FSR_RCAS_F

#include "ffx_a.h"

cbuffer cb : register(b0) {
	uint4 Const0;
	uint4 Centre;
	uint4 Radius;
};

SamplerState samLinearClamp : register(s0);
Texture2D<AF4> InputTexture : register(t0);
RWTexture2D<AF4> OutputTexture: register(u0);

AF4 FsrRcasLoadF(ASU2 p) { return InputTexture.Load(int3(ASU2(p), 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

#include "ffx_fsr1.h"

void Sharpen(int2 pos) {
	AF3 c;
	FsrRcasF(c.r, c.g, c.b, pos, Const0);
	OutputTexture[pos] = AF4(c, 1);
}

[numthreads(64, 1, 1)]
void main(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID, uint3 Dtid : SV_DispatchThreadID) {
	// Do remapping of local xy in workgroup for a more PS-like swizzle pattern.
	AU2 gxy = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
	AU2 groupCentre = AU2((WorkGroupId.x << 4u) + 8u, (WorkGroupId.y << 4u) + 8u);
	AU2 dc1 = Centre.xy - groupCentre;
	AU2 dc2 = Centre.zw - groupCentre;
	if (dot(dc1, dc1) <= Radius.y || dot(dc2, dc2) <= Radius.y) {
		// only do RCAS for workgroups inside the given radius
		Sharpen(gxy);
		gxy.x += 8u;
		Sharpen(gxy);
		gxy.y += 8u;
		Sharpen(gxy);
		gxy.x -= 8u;
		Sharpen(gxy);
	} else {
		AF4 mul = AF4(1, 1, 1, 1) - Const0[3] * AF4(0, 0.3, 0.3, 0);
		OutputTexture[gxy] = mul * InputTexture[gxy];
		gxy.x += 8u;
		OutputTexture[gxy] = mul * InputTexture[gxy];
		gxy.y += 8u;
		OutputTexture[gxy] = mul * InputTexture[gxy];
		gxy.x -= 8u;
		OutputTexture[gxy] = mul * InputTexture[gxy];
	}
}
