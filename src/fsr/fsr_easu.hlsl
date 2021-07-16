#define A_GPU 1
#define A_HLSL 1
//#define A_HALF
#define FSR_EASU_F 1

#include "ffx_a.h"

cbuffer cb : register(b0) {
	uint4 Const0;
	uint4 Const1;
	uint4 Const2;
	uint4 Const3;
	uint4 Centre;
	uint4 Radius;
};

SamplerState samLinearClamp : register(s0);
Texture2D<AF4> InputTexture : register(t0);
RWTexture2D<AF4> OutputTexture: register(u0);

AF4 FsrEasuRF(AF2 p) { AF4 res = InputTexture.GatherRed(samLinearClamp, p, int2(0, 0)); return res; }
AF4 FsrEasuGF(AF2 p) { AF4 res = InputTexture.GatherGreen(samLinearClamp, p, int2(0, 0)); return res; }
AF4 FsrEasuBF(AF2 p) { AF4 res = InputTexture.GatherBlue(samLinearClamp, p, int2(0, 0)); return res; }	

#include "ffx_fsr1.h"

void Upscale(int2 pos) {
	AF3 c;
	FsrEasuF(c, pos, Const0, Const1, Const2, Const3);
	OutputTexture[pos] = AF4(c, 1);
}

void Bilinear(int2 pos) {
	AF3 c = InputTexture.SampleLevel(samLinearClamp, float2(pos) / Radius.zw, 0).rgb;
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
		// only do the expensive EASU for workgroups inside the given radius
		Upscale(gxy);
		gxy.x += 8u;
		Upscale(gxy);
		gxy.y += 8u;
		Upscale(gxy);
		gxy.x -= 8u;
		Upscale(gxy);
	} else {
		// resort to cheaper bilinear sampling
		Bilinear(gxy);
		gxy.x += 8u;
		Bilinear(gxy);
		gxy.y += 8u;
		Bilinear(gxy);
		gxy.x -= 8u;
		Bilinear(gxy);
	}
}
