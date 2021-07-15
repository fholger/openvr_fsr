#define A_GPU 1
#define A_HLSL 1
//#define A_HALF
#define FSR_RCAS_F

#include "ffx_a.h"

cbuffer cb : register(b0) {
	uint4 Const0;
	//uint4 Sample;
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
	//if (Sample.x == 1)
	//	c *= c;
	OutputTexture[pos] = AF4(c, 1);
}

[numthreads(64, 1, 1)]
void main(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID, uint3 Dtid : SV_DispatchThreadID) {
	// Do remapping of local xy in workgroup for a more PS-like swizzle pattern.
	AU2 gxy = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
	Sharpen(gxy);
	gxy.x += 8u;
	Sharpen(gxy);
	gxy.y += 8u;
	Sharpen(gxy);
	gxy.x -= 8u;
	Sharpen(gxy);
}
