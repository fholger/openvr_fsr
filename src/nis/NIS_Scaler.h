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

//---------------------------------------------------------------------------------
// NVIDIA Image Scaling SDK  - v1.0
//---------------------------------------------------------------------------------
// The NVIDIA Image Scaling SDK provides a single spatial scaling and sharpening algorithm 
// for cross-platform support. The scaling algorithm uses a 6-tap scaling filter combined 
// with 4 directional scaling and adaptive sharpening filters, which creates nice smooth images
// and sharp edges. In addition, the SDK provides a state-of-the-art adaptive directional sharpening algorithm
// for use in applications where no scaling is required. 
//
// The directional scaling and sharpening algorithm is named NVScaler while the adaptive-directional-sharpening-only 
// algorithm is named NVSharpen. Both algorithms are provided as compute shaders and 
// developers are free to integrate them in their applications. Note that if you integrate NVScaler, you 
// should NOT integrate NVSharpen, as NVScaler already includes a sharpening pass
// 
// Pipeline Placement
// ------------------
// The call into the NVIDIA Image Scaling shaders must occur during the post-processing phase after tone-mapping. 
// Applying the scaling in linear HDR in-game color-space may result in a sharpening effect that is
// either not visible or too strong. Since sharpening algorithms can enhance noisy or grainy regions, it is recommended
// that certain effects such as film grain should occur after NVScaler or NVSharpen. Low-pass filters such as motion blur or 
// light bloom are recommended to be applied before NVScaler or NVSharpen to avoid sharpening attenuation.
//
// Color Space and Ranges 
// ----------------------
// NVIDIA Image Scaling shaders can process color textures stored as either LDR or HDR with the following 
// restrictions:
// 1) LDR
//    - The range of color values must be in the [0, 1] range
//    - The input color texture must be in display-referred color-space after tone mapping and OETF (gamma-correction)
//      has been applied
// 2) HDR PQ
//    - The range of color values must be in the [0, 1] range
//    - The input color texture must be in display-referred color-space after tone mapping with Rec.2020 PQ OETF applied
// 3) HDR Linear
//    - The recommended range of color values is [0, 12.5], where luminance value (as per BT. 709) of 
//      1.0 maps to brightness value of 80nits (sRGB peak) and 12.5 maps to 1000nits
//    - The input color texture may have luminance values that are either linear and scene-referred or 
//      linear and display-referred (after tone mapping)
//
// If the input color texture sent to NVScaler/NVSharpen is in HDR format set NIS_HDR_MODE define to either 
// NIS_HDR_MODE_LINEAR (1) or NIS_HDR_MODE_PQ (2).
//
// Supported Texture Formats
// -------------------------
// Input and output formats:
// Input and output formats are expected to be in the rages defined in previous section and should be 
// specified using non-integer data types such as DXGI_FORMAT_R8G8B8A8_UNORM.
//
// Coefficients formats: 
// The scaler coefficients and USM coefficients format should be specified using float4 type such as
// DXGI_FORMAT_R32G32B32A32_FLOAT or DXGI_FORMAT_R16G16B16A16_FLOAT.
//
// Resource States, Buffers, and Sampler:
// The game or application calling NVIDIA Image Scaling SDK shaders must ensure that the textures are in 
// the correct state.
// - Input color textures must be in pixel shader read state. Shader Resource View (SRV) in DirectX
// - The output texture must be in read/write state. Unordered Access View (UAV) in DirectX
// - The coefficients texture for NVScaler must be in read state. Shader Resource View (SRV) in DirectX
// - The configuration variables must be passed as constant buffer. Constant Buffer View (CBV) in DirectX
// - The sampler for texture pixel sampling. Linear clamp SamplerState in Direct
//
// Adding NVIDIA Image Scaling SDK to a Project
// --------------------------------------------
// Include NIS_Scaler.h directly in your application or alternative use the provided NIS_Main.hlsl shader file.
// Use NIS_Config.h to get the ideal shader dispatch values for your platform, to configure the algorithm constant 
// values (NVScalerUpdateConfig, and NVSharpenUpdateConfig), and to access the algorithm coefficients (coef_scale and coef_USM).
//
// Defines:
// NIS_SCALER: default (1) NVScaler, (0) fast NVSharpen only, no upscaling
// NIS_HDR_MODE: default(0) disabled, (1) Linear, (2) PQ
// NIS_BLOCK_WIDTH: pixels per block width. Use GetOptimalBlockWidth query for your platform
// NIS_BLOCK_HEIGHT: pixels per block height. Use GetOptimalBlockHeight query for your platform
// NIS_THREAD_GROUP_SIZE: number of threads per group. Use GetOptimalThreadGroupSize query for your platform
// NIS_USE_HALF_PRECISION: default(0) disabled, (1) enable half pression computation
// NIS_HLSL_6_2: default (0) HLSL v5, (1) HLSL v6.2 
// NIS_VIEWPORT_SUPPORT: default(0) disabled, (1) enable input/output viewport support
// 
// Default NVScaler shader constants:
// [NIS_BLOCK_WIDTH, NIS_BLOCK_HEIGHT, NIS_THREAD_GROUP_SIZE] = [32, 24, 256]
//
// Default NVSharpen shader constants:
// [NIS_BLOCK_WIDTH, NIS_BLOCK_HEIGHT, NIS_THREAD_GROUP_SIZE] = [32, 32, 256]
//---------------------------------------------------------------------------------

// NVScaler enable by default. Set to 0 for NVSharpen only
#ifndef NIS_SCALER
#define NIS_SCALER 1
#endif

// HDR Modes
#define NIS_HDR_MODE_NONE  0 
#define NIS_HDR_MODE_LINEAR  1
#define NIS_HDR_MODE_PQ  2
#ifndef NIS_HDR_MODE
#define NIS_HDR_MODE NIS_HDR_MODE_NONE
#endif
#define kHDRCompressionFactor  0.282842712f

// Viewport support
#ifndef NIS_VIEWPORT_SUPPORT
#define NIS_VIEWPORT_SUPPORT 0
#endif

// Half precision
#ifndef NIS_USE_HALF_PRECISION
#define NIS_USE_HALF_PRECISION 0
#endif
#ifndef NIS_HLSL_6_2
#define NIS_HLSL_6_2 0
#endif

#if NIS_USE_HALF_PRECISION
#if NIS_HLSL_6_2
typedef float16_t4 NVF4;
typedef float16_t NVF;
#else
typedef min16float4 NVF4;
typedef min16float NVF;
#endif // NIS_HLSL_6_2
#define NIS_SCALE_INT 1
#define NIS_SCALE_FLOAT 1.0
#else
typedef float4 NVF4;
typedef float NVF;
#define NIS_SCALE_INT 255
#define NIS_SCALE_FLOAT 255.0
#endif // NIS_USE_HALF_PRECISION

// Loop unrolling
#ifndef NIS_UNROLL
#define NIS_UNROLL [unroll]
#endif 

// Texture gather
#ifndef NIS_TEXTURE_GATHER
#define NIS_TEXTURE_GATHER 0
#endif

float getY(float3 rgba)
{
#if NIS_HDR_MODE == NIS_HDR_MODE_PQ
    return 0.262f * rgba.x + 0.678f * rgba.y + 0.0593f * rgba.z;
#elif NIS_HDR_MODE == NIS_HDR_MODE_LINEAR    
    return sqrt(0.2126f * rgba.x + 0.7152f * rgba.y + 0.0722f * rgba.z) * kHDRCompressionFactor;
#else
    return 0.2126f * rgba.x + 0.7152f * rgba.y + 0.0722f * rgba.z;
#endif
}

float getYLinear(float3 rgba)
{
    return 0.2126f * rgba.x + 0.7152f * rgba.y + 0.0722f * rgba.z;
};

#if NIS_SCALER
float4 GetEdgeMap(float p[4][4], int i, int j)
#else
float4 GetEdgeMap(float p[5][5], int i, int j)
#endif
{
    const float g_0 = abs(p[0 + i][0 + j] + p[0 + i][1 + j] + p[0 + i][2 + j] - p[2 + i][0 + j] - p[2 + i][1 + j] - p[2 + i][2 + j]);
    const float g_45 = abs(p[1 + i][0 + j] + p[0 + i][0 + j] + p[0 + i][1 + j] - p[2 + i][1 + j] - p[2 + i][2 + j] - p[1 + i][2 + j]);
    const float g_90 = abs(p[0 + i][0 + j] + p[1 + i][0 + j] + p[2 + i][0 + j] - p[0 + i][2 + j] - p[1 + i][2 + j] - p[2 + i][2 + j]);
    const float g_135 = abs(p[1 + i][0 + j] + p[2 + i][0 + j] + p[2 + i][1 + j] - p[0 + i][1 + j] - p[0 + i][2 + j] - p[1 + i][2 + j]);

    const float g_0_90_max = max(g_0, g_90);
    const float g_0_90_min = min(g_0, g_90);
    const float g_45_135_max = max(g_45, g_135);
    const float g_45_135_min = min(g_45, g_135);

    float e_0_90 = 0;
    float e_45_135 = 0;

    float edge_0 = 0;
    float edge_45 = 0;
    float edge_90 = 0;
    float edge_135 = 0;

    if ((g_0_90_max + g_45_135_max) == 0)
    {
        e_0_90 = 0;
        e_45_135 = 0;
    }
    else
    {
        e_0_90 = g_0_90_max / (g_0_90_max + g_45_135_max);
        e_0_90 = min(e_0_90, 1.0f);
        e_45_135 = 1.0f - e_0_90;
    }

    if ((g_0_90_max > (g_0_90_min * kDetectRatio)) && (g_0_90_max > kDetectThres) && (g_0_90_max > g_45_135_min))
    {
        if (g_0_90_max == g_0)
        {
            edge_0 = 1.0f;
            edge_90 = 0;
        }
        else
        {
            edge_0 = 0;
            edge_90 = 1.0f;
        }
    }
    else
    {
        edge_0 = 0;
        edge_90 = 0;
    }

    if ((g_45_135_max > (g_45_135_min * kDetectRatio)) && (g_45_135_max > kDetectThres) &&
        (g_45_135_max > g_0_90_min))
    {

        if (g_45_135_max == g_45)
        {
            edge_45 = 1.0f;
            edge_135 = 0;
        }
        else
        {
            edge_45 = 0;
            edge_135 = 1.0f;
        }
    }
    else
    {
        edge_45 = 0;
        edge_135 = 0;
    }

    float weight_0, weight_90, weight_45, weight_135;
    if ((edge_0 + edge_90 + edge_45 + edge_135) >= 2.0f)
    {
        if (edge_0 == 1.0f)
        {
            weight_0 = e_0_90;
            weight_90 = 0;
        }
        else
        {
            weight_0 = 0;
            weight_90 = e_0_90;
        }

        if (edge_45 == 1.0f)
        {
            weight_45 = e_45_135;
            weight_135 = 0;
        }
        else
        {
            weight_45 = 0;
            weight_135 = e_45_135;
        }
    }
    else if ((edge_0 + edge_90 + edge_45 + edge_135) >= 1.0f)
    {
        weight_0 = edge_0;
        weight_90 = edge_90;
        weight_45 = edge_45;
        weight_135 = edge_135;
    }
    else
    {
        weight_0 = 0;
        weight_90 = 0;
        weight_45 = 0;
        weight_135 = 0;
    }

    return float4(weight_0, weight_90, weight_45, weight_135);
}

#if NIS_SCALER

#ifndef NIS_BLOCK_WIDTH
#define NIS_BLOCK_WIDTH 32
#endif 
#ifndef NIS_BLOCK_HEIGHT
#define NIS_BLOCK_HEIGHT 24
#endif
#ifndef NIS_THREAD_GROUP_SIZE
#define NIS_THREAD_GROUP_SIZE 256
#endif
#define kPhaseCount  64
#define kFilterSize  8
#define kSupportSize 6
#define kPadSize     kSupportSize
#define kTileSize    (NIS_BLOCK_WIDTH + kPadSize) * (NIS_BLOCK_HEIGHT + kPadSize)
#define blockDim     NIS_THREAD_GROUP_SIZE

groupshared NVF shPixelsY[kTileSize];
groupshared NVF shCoefScaler[kPhaseCount][kFilterSize];
groupshared NVF shCoefUSM[kPhaseCount][kFilterSize];
groupshared NVF4 shEdgeMap[kTileSize];

void LoadFilterBanksSh(int i0, int di)
{
    // load up filter banks to shared memory
    for (int i = i0; i < kFilterSize * kPhaseCount / 4 / 2; i += di)
    {
        NVF4 v0 = coef_scaler[int2(0, i)];
        NVF4 v1 = coef_scaler[int2(1, i)];
        shCoefScaler[i][0] = (NVF)v0.x;
        shCoefScaler[i][1] = (NVF)v0.y;
        shCoefScaler[i][2] = (NVF)v0.z;
        shCoefScaler[i][3] = (NVF)v0.w;
        shCoefScaler[i][4] = (NVF)v1.x;
        shCoefScaler[i][5] = (NVF)v1.y;

        v0 = coef_usm[int2(0, i)];
        v1 = coef_usm[int2(1, i)];
        shCoefUSM[i][0] = (NVF)v0.x;
        shCoefUSM[i][1] = (NVF)v0.y;
        shCoefUSM[i][2] = (NVF)v0.z;
        shCoefUSM[i][3] = (NVF)v0.w;
        shCoefUSM[i][4] = (NVF)v1.x;
        shCoefUSM[i][5] = (NVF)v1.y;
    }
}

float CalcLTI(float p0, float p1, float p2, float p3, float p4, float p5, int phase_index)
{
    float y0, y1, y2, y3, y4;

    if (phase_index <= kPhaseCount / 2)
    {
        y0 = p0;
        y1 = p1;
        y2 = p2;
        y3 = p3;
        y4 = p4;
    }
    else
    {
        y0 = p1;
        y1 = p2;
        y2 = p3;
        y3 = p4;
        y4 = p5;
    }

    const float a_min = min(min(y0, y1), y2);
    const float a_max = max(max(y0, y1), y2);

    const float b_min = min(min(y2, y3), y4);
    const float b_max = max(max(y2, y3), y4);

    const float a_cont = a_max - a_min;
    const float b_cont = b_max - b_min;

    const float cont_ratio = max(a_cont, b_cont) / (min(a_cont, b_cont) + kEps);
    return (1.0f - saturate((cont_ratio - kMinContrastRatio) * kRatioNorm)) * kContrastBoost;
}

float4 GetInterpEdgeMap(const float4 edge[2][2], float phase_frac_x, float phase_frac_y)
{
    float4 h0, h1, f;

    h0.x = lerp(edge[0][0].x, edge[0][1].x, phase_frac_x);
    h0.y = lerp(edge[0][0].y, edge[0][1].y, phase_frac_x);
    h0.z = lerp(edge[0][0].z, edge[0][1].z, phase_frac_x);
    h0.w = lerp(edge[0][0].w, edge[0][1].w, phase_frac_x);

    h1.x = lerp(edge[1][0].x, edge[1][1].x, phase_frac_x);
    h1.y = lerp(edge[1][0].y, edge[1][1].y, phase_frac_x);
    h1.z = lerp(edge[1][0].z, edge[1][1].z, phase_frac_x);
    h1.w = lerp(edge[1][0].w, edge[1][1].w, phase_frac_x);

    f.x = lerp(h0.x, h1.x, phase_frac_y);
    f.y = lerp(h0.y, h1.y, phase_frac_y);
    f.z = lerp(h0.z, h1.z, phase_frac_y);
    f.w = lerp(h0.w, h1.w, phase_frac_y);

    return f;
}

float EvalPoly6(const float pxl[6], int phase_int)
{
    float y = 0.f;
    {
        NIS_UNROLL
        for (int i = 0; i < 6; ++i)
        {
            y += shCoefScaler[phase_int][i] * pxl[i];
        }
    }
    float y_usm = 0.f;
    {
        NIS_UNROLL
        for (int i = 0; i < 6; ++i)
        {
            y_usm += shCoefUSM[phase_int][i] * pxl[i];
        }
    }

    // let's compute a piece-wise ramp based on luma
    const float y_scale = 1.0f - saturate((y * (1.0f / 255) - kSharpStartY) * kSharpScaleY);

    // scale the ramp to sharpen as a function of luma
    const float y_sharpness = y_scale * kSharpStrengthScale + kSharpStrengthMin;

    y_usm *= y_sharpness;

    // scale the ramp to limit USM as a function of luma
    const float y_sharpness_limit = (y_scale * kSharpLimitScale + kSharpLimitMin) * y;

    y_usm = min(y_sharpness_limit, max(-y_sharpness_limit, y_usm));
    // reduce ringing
    y_usm *= CalcLTI(pxl[0], pxl[1], pxl[2], pxl[3], pxl[4], pxl[5], phase_int);

    return y + y_usm;
}

float FilterNormal(const float p[6][6], int phase_x_frac_int, int phase_y_frac_int)
{
    float h_acc = 0.0f;
    NIS_UNROLL
    for (int j = 0; j < 6; ++j)
    {
        float v_acc = 0.0f;
        NIS_UNROLL
        for (int i = 0; i < 6; ++i)
        {
            v_acc += p[i][j] * shCoefScaler[phase_y_frac_int][i];
        }
        h_acc += v_acc * shCoefScaler[phase_x_frac_int][j];
    }

    // let's return the sum unpacked -> we can accumulate it later
    return h_acc;
}

float4 GetDirFilters(float p[6][6], float phase_x_frac, float phase_y_frac, int phase_x_frac_int, int phase_y_frac_int)
{
    float4 f;
    // 0 deg filter
    float interp0Deg[6];
    {
        NIS_UNROLL
        for (int i = 0; i < 6; ++i)
        {
            interp0Deg[i] = lerp(p[i][2], p[i][3], phase_x_frac);
        }
    }

    f.x = EvalPoly6(interp0Deg, phase_y_frac_int);

    // 90 deg filter
    float interp90Deg[6];
    {
        NIS_UNROLL
        for (int i = 0; i < 6; ++i)
        {
            interp90Deg[i] = lerp(p[2][i], p[3][i], phase_y_frac);
        }
    }

    f.y = EvalPoly6(interp90Deg, phase_x_frac_int);

    //45 deg filter
    float pphase_b45;
    pphase_b45 = 0.5f + 0.5f * (phase_x_frac - phase_y_frac);

    float temp_interp45Deg[7];
    temp_interp45Deg[1] = lerp(p[2][1], p[1][2], pphase_b45);
    temp_interp45Deg[3] = lerp(p[3][2], p[2][3], pphase_b45);
    temp_interp45Deg[5] = lerp(p[4][3], p[3][4], pphase_b45);

    if (pphase_b45 >= 0.5f)
    {
        pphase_b45 = pphase_b45 - 0.5f;

        temp_interp45Deg[0] = lerp(p[1][1], p[0][2], pphase_b45);
        temp_interp45Deg[2] = lerp(p[2][2], p[1][3], pphase_b45);
        temp_interp45Deg[4] = lerp(p[3][3], p[2][4], pphase_b45);
        temp_interp45Deg[6] = lerp(p[4][4], p[3][5], pphase_b45);
    }
    else
    {
        pphase_b45 = 0.5f - pphase_b45;

        temp_interp45Deg[0] = lerp(p[1][1], p[2][0], pphase_b45);
        temp_interp45Deg[2] = lerp(p[2][2], p[3][1], pphase_b45);
        temp_interp45Deg[4] = lerp(p[3][3], p[4][2], pphase_b45);
        temp_interp45Deg[6] = lerp(p[4][4], p[5][3], pphase_b45);
    }

    float interp45Deg[6];
    float pphase_p45 = phase_x_frac + phase_y_frac;
    if (pphase_p45 >= 1)
    {
        NIS_UNROLL
        for (int i = 0; i < 6; i++)
        {
            interp45Deg[i] = temp_interp45Deg[i + 1];
        }
        pphase_p45 = pphase_p45 - 1;
    }
    else
    {
        NIS_UNROLL
        for (int i = 0; i < 6; i++)
        {
            interp45Deg[i] = temp_interp45Deg[i];
        }
    }

    f.z = EvalPoly6(interp45Deg, (int)(pphase_p45 * 64));

    //135 deg filter
    float pphase_b135;
    pphase_b135 = 0.5f * (phase_x_frac + phase_y_frac);

    float temp_interp135Deg[7];

    temp_interp135Deg[1] = lerp(p[3][1], p[4][2], pphase_b135);
    temp_interp135Deg[3] = lerp(p[2][2], p[3][3], pphase_b135);
    temp_interp135Deg[5] = lerp(p[1][3], p[2][4], pphase_b135);

    if (pphase_b135 >= 0.5f)
    {
        pphase_b135 = pphase_b135 - 0.5f;

        temp_interp135Deg[0] = lerp(p[4][1], p[5][2], pphase_b135);
        temp_interp135Deg[2] = lerp(p[3][2], p[4][3], pphase_b135);
        temp_interp135Deg[4] = lerp(p[2][3], p[3][4], pphase_b135);
        temp_interp135Deg[6] = lerp(p[1][4], p[2][5], pphase_b135);
    }
    else
    {
        pphase_b135 = 0.5f - pphase_b135;

        temp_interp135Deg[0] = lerp(p[4][1], p[3][0], pphase_b135);
        temp_interp135Deg[2] = lerp(p[3][2], p[2][1], pphase_b135);
        temp_interp135Deg[4] = lerp(p[2][3], p[1][2], pphase_b135);
        temp_interp135Deg[6] = lerp(p[1][4], p[0][3], pphase_b135);
    }

    float interp135Deg[6];
    float pphase_p135 = 1 + (phase_x_frac - phase_y_frac);
    if (pphase_p135 >= 1)
    {
        NIS_UNROLL
        for (int i = 0; i < 6; ++i)
        {
            interp135Deg[i] = temp_interp135Deg[i + 1];
        }
        pphase_p135 = pphase_p135 - 1;
    }
    else
    {
        NIS_UNROLL
        for (int i = 0; i < 6; ++i)
        {
            interp135Deg[i] = temp_interp135Deg[i];
        }
    }

    f.w = EvalPoly6(interp135Deg, (int)(pphase_p135 * 64));
    return f;
}


//-----------------------------------------------------------------------------------------------
// NVScaler
//-----------------------------------------------------------------------------------------------
void NVScaler(uint2 blockIdx, uint threadIdx)
{
    // Figure out the range of pixels from input image that would be needed to be loaded for this thread-block
    const int dstBlockX = NIS_BLOCK_WIDTH * blockIdx.x;
    const int dstBlockY = NIS_BLOCK_HEIGHT * blockIdx.y;

    const int srcBlockStartX = floor((dstBlockX + 0.5f) * kScaleX - 0.5f);
    const int srcBlockStartY = floor((dstBlockY + 0.5f) * kScaleY - 0.5f);
    const int srcBlockEndX = ceil((dstBlockX + NIS_BLOCK_WIDTH + 0.5f) * kScaleX - 0.5f);
    const int srcBlockEndY = ceil((dstBlockY + NIS_BLOCK_HEIGHT + 0.5f) * kScaleY - 0.5f);
            
    int numPixelsX = srcBlockEndX - srcBlockStartX + kSupportSize - 1;
    int numPixelsY = srcBlockEndY - srcBlockStartY + kSupportSize - 1;

    // round-up load region to even size since we're loading in 2x2 batches
    numPixelsX += numPixelsX & 0x1;
    numPixelsY += numPixelsY & 0x1;

    const float invNumPixelX = 1.0f / numPixelsX;
    const uint numPixels = numPixelsX * numPixelsY;

    // fill in input luma tile in batches of 2x2 pixels
    // we use texture gather to get extra support necessary
    // to compute 2x2 edge map outputs too
    for (uint i = threadIdx * 2; i < numPixels / 2; i += blockDim * 2)
    {
        float py = floor(i * invNumPixelX);
        const float px = i - py * numPixelsX;
        py *= 2.0f;

        // 0.5 to be in the center of texel
        // -1.0 to sample top-left corner of 3x3 halo necessary
        // -kSupportSize/2 to shift by the kernel support size
        float kShift = 0.5f - 1.0f - (kSupportSize - 1) / 2;
#if NIS_VIEWPORT_SUPPORT
        const float tx = (srcBlockStartX + px + kInputViewportOriginX + kShift) * kSrcNormX;
        const float ty = (srcBlockStartY + py + kInputViewportOriginY + kShift) * kSrcNormY;
#else
        const float tx = (srcBlockStartX + px + kShift) * kSrcNormX;
        const float ty = (srcBlockStartY + py + kShift) * kSrcNormY;
#endif
        float p[4][4];
#if NIS_TEXTURE_GATHER
        NIS_UNROLL for (int j = 0; j < 4; j += 2)
        {
            NIS_UNROLL for (int k = 0; k < 4; k += 2)
            {
                const float4 sr = in_texture.GatherRed(samplerLinearClamp, float2(tx + k * kSrcNormX, ty + j * kSrcNormY), int2(0, 0));
                const float4 sg = in_texture.GatherGreen(samplerLinearClamp, float2(tx + k * kSrcNormX, ty + j * kSrcNormY), int2(0, 0));
                const float4 sb = in_texture.GatherBlue(samplerLinearClamp, float2(tx + k * kSrcNormX, ty + j * kSrcNormY), int2(0, 0));

                p[j + 0][k + 0] = getY(float3(sr.w, sg.w, sb.w));
                p[j + 0][k + 1] = getY(float3(sr.z, sg.z, sb.z));
                p[j + 1][k + 0] = getY(float3(sr.x, sg.x, sb.x));
                p[j + 1][k + 1] = getY(float3(sr.y, sg.y, sb.y));
            }
        }
#else
        NIS_UNROLL
        for (int j = 0; j < 4; j++)
        {
            NIS_UNROLL
            for (int k = 0; k < 4; k++)
            {
                const float3 px = in_texture.SampleLevel(samplerLinearClamp, float2(tx + k * kSrcNormX, ty + j * kSrcNormY), 0).xyz;
                p[j][k] = getY(px);
            }
        }
#endif
        const int idx = py * numPixelsX + px;
        shEdgeMap[idx] = (NVF4)GetEdgeMap(p, 0, 0);
        shEdgeMap[idx + 1] = (NVF4)GetEdgeMap(p, 0, 1);
        shEdgeMap[idx + numPixelsX] = (NVF4)GetEdgeMap(p, 1, 0);
        shEdgeMap[idx + numPixelsX + 1] = (NVF4)GetEdgeMap(p, 1, 1);

        // normalize luma to 255.0f and write out to shmem
        shPixelsY[idx] = (NVF)(p[1][1] * NIS_SCALE_FLOAT);
        shPixelsY[idx + 1] = (NVF)(p[1][2] * NIS_SCALE_FLOAT);
        shPixelsY[idx + numPixelsX] = (NVF)(p[2][1] * NIS_SCALE_FLOAT);
        shPixelsY[idx + numPixelsX + 1] = (NVF)(p[2][2] * NIS_SCALE_FLOAT);
    }

    LoadFilterBanksSh(threadIdx, blockDim);

    GroupMemoryBarrierWithGroupSync();

    for (uint k = threadIdx; k < NIS_BLOCK_WIDTH * NIS_BLOCK_HEIGHT; k += blockDim)
    {
        const int2 pos = int2(k % NIS_BLOCK_WIDTH, k / NIS_BLOCK_WIDTH);

        const int dstX = dstBlockX + pos.x;
        const int dstY = dstBlockY + pos.y;

        const float srcX = (0.5f + dstX) * kScaleX - 0.5f;
        const float srcY = (0.5f + dstY) * kScaleY - 0.5f;
#if NIS_VIEWPORT_SUPPORT
        if (srcX > kInputViewportWidth || srcY > kInputViewportHeight || 
            dstX > kOutputViewportWidth || dstY > kOutputViewportHeight)
        {
            return;
        }
#endif

        const int px = floor(srcX) - srcBlockStartX;
        const int py = floor(srcY) - srcBlockStartY;

        const int start_idx = py * numPixelsX + px;

        // load 6x6 support to regs
        float p[6][6];
        {
            NIS_UNROLL
            for (int i = 0; i < 6; ++i)
            {
                NIS_UNROLL
                for (int j = 0; j < 6; ++j)
                {
                    p[i][j] = shPixelsY[start_idx + i * numPixelsX + j];
                }
            }
        }

        // compute discretized filter phase
        const float fx = srcX - floor(srcX);
        const float fy = srcY - floor(srcY);
        const int fx_int = (int)(fx * kPhaseCount);
        const int fy_int = (int)(fy * kPhaseCount);

        // get traditional scaler filter output
        const float pixel_n = FilterNormal(p, fx_int, fy_int);

        // get directional filter bank output
        float4 opDirYU = GetDirFilters(p, fx, fy, fx_int, fy_int);

        // final luma is a weighted product of directional & normal filters

        // generate weights for directional filters
        const int kShift = (kSupportSize - 2) / 2;
        float4 edge[2][2];
        NIS_UNROLL
        for (int i = 0; i < 2; i++)
        {
            NIS_UNROLL
            for (int j = 0; j < 2; j++)
            {
                // need to shift edge map sampling since it's a 2x2 centered inside 6x6 grid                
                edge[i][j] = shEdgeMap[start_idx + (i + kShift) * numPixelsX + (j + kShift)];
            }
        }
        const float4 w = GetInterpEdgeMap(edge, fx, fy) * NIS_SCALE_INT;

        // final pixel is a weighted sum filter outputs
        const float opY = (opDirYU.x * w.x + opDirYU.y * w.y + opDirYU.z * w.z + opDirYU.w * w.w +
            pixel_n * (NIS_SCALE_FLOAT - w.x - w.y - w.z - w.w)) * (1.0f / NIS_SCALE_FLOAT);
        // do bilinear tap for chroma upscaling
#if NIS_VIEWPORT_SUPPORT
        float4 op = in_texture.SampleLevel(samplerLinearClamp, float2((srcX + kInputViewportOriginX) * kSrcNormX, (srcY + kInputViewportOriginY) * kSrcNormY), 0);
#else
        float4 op = in_texture.SampleLevel(samplerLinearClamp, float2((dstX + 0.5f) * kDstNormX, (dstY + 0.5f) * kDstNormY), 0);
#endif 
#if NIS_HDR_MODE == NIS_HDR_MODE_LINEAR
        const float kEps = 1e-4f;
        const float kNorm = 1.0f / (NIS_SCALE_FLOAT * kHDRCompressionFactor);
        const float opYN = max(opY, 0.0f) * kNorm;
        const float corr = (opYN * opYN + kEps) / (max(getYLinear(float3(op.x, op.y, op.z)), 0.0f) + kEps);
        op.x *= corr;
        op.y *= corr;
        op.z *= corr;
#else
        const float corr = opY * (1.0f / NIS_SCALE_FLOAT) - getY(float3(op.x, op.y, op.z));
        op.x += corr;
        op.y += corr;
        op.z += corr;
#endif

#if NIS_VIEWPORT_SUPPORT
        out_texture[uint2(dstX + kOutputViewportOriginX, dstY + kOutputViewportOriginY)] = op;
#else
        out_texture[uint2(dstX, dstY)] = op;
#endif
    }
}
#else

#ifndef NIS_BLOCK_WIDTH
#define NIS_BLOCK_WIDTH 32
#endif 
#ifndef NIS_BLOCK_HEIGHT
#define NIS_BLOCK_HEIGHT 32
#endif
#ifndef NIS_THREAD_GROUP_SIZE
#define NIS_THREAD_GROUP_SIZE 256
#endif

#define kSupportSize 5
#define kNumPixelsX  (NIS_BLOCK_WIDTH + kSupportSize + 1)
#define kNumPixelsY  (NIS_BLOCK_HEIGHT + kSupportSize + 1)
#define blockDim     NIS_THREAD_GROUP_SIZE

groupshared float shPixelsY[kNumPixelsY][kNumPixelsX];

float CalcLTIFast(const float y[5])
{
    const float a_min = min(min(y[0], y[1]), y[2]);
    const float a_max = max(max(y[0], y[1]), y[2]);

    const float b_min = min(min(y[2], y[3]), y[4]);
    const float b_max = max(max(y[2], y[3]), y[4]);

    const float a_cont = a_max - a_min;
    const float b_cont = b_max - b_min;

    const float cont_ratio = max(a_cont, b_cont) / (min(a_cont, b_cont) + kEps * (1.0f / 255.0f));
    return (1.0f - saturate((cont_ratio - kMinContrastRatio) * kRatioNorm)) * kContrastBoost;
}

float EvalUSM(const float pxl[5], const float sharpnessStrength, const float sharpnessLimit)
{
    // USM profile
    float y_usm = -0.6001f * pxl[1] + 1.2002f * pxl[2] - 0.6001f * pxl[3];
    // boost USM profile
    y_usm *= sharpnessStrength;
    // clamp to the limit
    y_usm = min(sharpnessLimit, max(-sharpnessLimit, y_usm));
    // reduce ringing
    y_usm *= CalcLTIFast(pxl);

    return y_usm;
}

float4 GetDirUSM(const float p[5][5])
{
    // sharpness boost & limit are the same for all directions
    const float scaleY = 1.0f - saturate((p[2][2] - kSharpStartY) * kSharpScaleY);
    // scale the ramp to sharpen as a function of luma
    const float sharpnessStrength = scaleY * kSharpStrengthScale + kSharpStrengthMin;
    // scale the ramp to limit USM as a function of luma
    const float sharpnessLimit = (scaleY * kSharpLimitScale + kSharpLimitMin) * p[2][2];

    float4 rval;
    // 0 deg filter
    float interp0Deg[5];
    {
        for (int i = 0; i < 5; ++i)
        {
            interp0Deg[i] = p[i][2];
        }
    }

    rval.x = EvalUSM(interp0Deg, sharpnessStrength, sharpnessLimit);

    // 90 deg filter
    float interp90Deg[5];
    {
        for (int i = 0; i < 5; ++i)
        {
            interp90Deg[i] = p[2][i];
        }
    }

    rval.y = EvalUSM(interp90Deg, sharpnessStrength, sharpnessLimit);

    //45 deg filter
    float interp45Deg[5];
    interp45Deg[0] = p[1][1];
    interp45Deg[1] = lerp(p[2][1], p[1][2], 0.5f);
    interp45Deg[2] = p[2][2];
    interp45Deg[3] = lerp(p[3][2], p[2][3], 0.5f);
    interp45Deg[4] = p[3][3];

    rval.z = EvalUSM(interp45Deg, sharpnessStrength, sharpnessLimit);

    //135 deg filter
    float interp135Deg[5];
    interp135Deg[0] = p[3][1];
    interp135Deg[1] = lerp(p[3][2], p[2][1], 0.5f);
    interp135Deg[2] = p[2][2];
    interp135Deg[3] = lerp(p[2][3], p[1][2], 0.5f);
    interp135Deg[4] = p[1][3];

    rval.w = EvalUSM(interp135Deg, sharpnessStrength, sharpnessLimit);
    return rval;
}

//-----------------------------------------------------------------------------------------------
// NVSharpen
//-----------------------------------------------------------------------------------------------
void NVSharpen(uint2 blockIdx, uint threadIdx)
{
    const int dstBlockX = NIS_BLOCK_WIDTH * blockIdx.x;
    const int dstBlockY = NIS_BLOCK_HEIGHT * blockIdx.y;

    // fill in input luma tile in batches of 2x2 pixels
    // we use texture gather to get extra support necessary
    // to compute 2x2 edge map outputs too
    const float kShift = 0.5f - kSupportSize / 2;
   
    for (uint i = threadIdx * 2; i < kNumPixelsX * kNumPixelsY / 2; i += blockDim * 2)
    {
        uint2 pos = uint2(i % kNumPixelsX, i / kNumPixelsX * 2);
        NIS_UNROLL
        for (int dy = 0; dy < 2; dy++)
        {
            NIS_UNROLL
            for (int dx = 0; dx < 2; dx++)
            {
#if NIS_VIEWPORT_SUPPORT
                const float tx = (dstBlockX + pos.x + kInputViewportOriginX + dx + kShift) * kSrcNormX;
                const float ty = (dstBlockY + pos.y + kInputViewportOriginY + dy + kShift) * kSrcNormY;
#else
                const float tx = (dstBlockX + pos.x + dx + kShift) * kSrcNormX;
                const float ty = (dstBlockY + pos.y + dy + kShift) * kSrcNormY;
#endif
                const float3 px = in_texture.SampleLevel(samplerLinearClamp, float2(tx, ty), 0).xyz;
                shPixelsY[pos.y + dy][pos.x + dx] = getY(px);                
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    for (int k = threadIdx; k < NIS_BLOCK_WIDTH * NIS_BLOCK_HEIGHT; k += blockDim)
    {
        const int2 pos = int2(k % NIS_BLOCK_WIDTH, k / NIS_BLOCK_WIDTH);

        // load 5x5 support to regs
        float p[5][5];
        NIS_UNROLL
        for (int i = 0; i < 5; ++i)
        {
            NIS_UNROLL
            for (int j = 0; j < 5; ++j)
            {
                p[i][j] = shPixelsY[pos.y + i][pos.x + j];
            }
        }

        // get directional filter bank output
        const float4 dirUSM = GetDirUSM(p);

        // generate weights for directional filters
        float4 w = GetEdgeMap(p, kSupportSize / 2 - 1, kSupportSize / 2 - 1);

        // final USM is a weighted sum filter outputs
        const float usmY = (dirUSM.x * w.x + dirUSM.y * w.y + dirUSM.z * w.z + dirUSM.w * w.w);

        // do bilinear tap and correct rgb texel so it produces new sharpened luma
        const int dstX = dstBlockX + pos.x;
        const int dstY = dstBlockY + pos.y;

#if NIS_VIEWPORT_SUPPORT
        if (dstX > kOutputViewportWidth || dstY > kOutputViewportHeight)
        {
            return;
        }
#endif

#if NIS_VIEWPORT_SUPPORT
        float4 op = in_texture.SampleLevel(samplerLinearClamp, float2((dstX + kInputViewportOriginX) * kSrcNormX, (dstY + kInputViewportOriginY) * kSrcNormY), 0);
#else
        float4 op = in_texture.SampleLevel(samplerLinearClamp, float2((dstX + 0.5f) * kDstNormX, (dstY + 0.5f) * kDstNormY), 0);
#endif 
#if NIS_HDR_MODE == NIS_HDR_MODE_LINEAR
        const float kEps = 1e-4f * kHDRCompressionFactor * kHDRCompressionFactor;
        float newY = p[2][2] + usmY;
        newY = max(newY, 0.0f);
        const float oldY = p[2][2];
        const float corr = (newY * newY + kEps) / (oldY * oldY + kEps);
        op.x *= corr;
        op.y *= corr;
        op.z *= corr;
#else
        op.x += usmY;
        op.y += usmY;
        op.z += usmY;
#endif
#if NIS_VIEWPORT_SUPPORT
        out_texture[uint2(dstX + kOutputViewportOriginX, dstY + kOutputViewportOriginY)] = op;
#else
        out_texture[uint2(dstX, dstY)] = op;
#endif
    }
}
#endif
