cbuffer cb : register(b0) {
	uint4 const0;
	uint4 const1;
};

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

#define A_GPU 1
#define A_HLSL 1

#include "ffx_a.h"

AF3 CasLoad(ASU2 p) {
	return InputTexture.Load(int3(p, 0)).rgb;
}

// for transforming to linear color space, not needed (?)
void CasInput(inout AF1 r, inout AF1 g, inout AF1 b) {}

#include "ffx_cas.h"

[numthreads(64, 1, 1)]
void main(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID) {
	AU2 gxy = ARmp8x8( LocalThreadId.x ) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
#if CAS_SHARPEN_ONLY
	bool sharpenOnly = true;
#else
	bool sharpenOnly = false;
#endif

	AF3 c;

	CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
	OutputTexture[ASU2(gxy)] = AF4(c, 1);
	gxy.x += 8u;

	CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
	OutputTexture[ASU2(gxy)] = AF4(c, 1);
	gxy.y += 8u;

	CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
	OutputTexture[ASU2(gxy)] = AF4(c, 1);
	gxy.x -= 8u;

	CasFilter(c.r, c.g, c.b, gxy, const0, const1, sharpenOnly);
	OutputTexture[ASU2(gxy)] = AF4(c, 1);
}