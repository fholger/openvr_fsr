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

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(_MSC_VER)
#define NIS_ALIGNED(x) __declspec(align(x))
#else
#if defined(__GNUC__)
#define NIS_ALIGNED(x) __attribute__ ((aligned(x)))
#endif
#endif


struct NIS_ALIGNED(256) NISConfig
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

    uint32_t kInputViewportOriginX;
    uint32_t kInputViewportOriginY;
    uint32_t kInputViewportWidth;
    uint32_t kInputViewportHeight;

    uint32_t kOutputViewportOriginX;
    uint32_t kOutputViewportOriginY;
    uint32_t kOutputViewportWidth;
    uint32_t kOutputViewportHeight;

    float reserved0;
    float reserved1;

    uint32_t imageCentre[4];
    uint32_t radius[4];
};

enum class NISHDRMode : uint32_t
{
    None = 0,
    Linear = 1,
    PQ = 2
};

enum class NISGPUArchitecture : uint32_t
{
    NVIDIA_Generic = 0,
    AMD_Generic = 1,
    Intel_Generic = 2
};

struct NISOptimizer
{
    bool isUpscaling;
    NISGPUArchitecture gpuArch;

    constexpr NISOptimizer(bool isUpscaling = true, NISGPUArchitecture gpuArch = NISGPUArchitecture::NVIDIA_Generic)
        : isUpscaling(isUpscaling)
        , gpuArch(gpuArch)
    {}

    constexpr uint32_t GetOptimalBlockWidth()
    {
        switch (gpuArch) {
        case NISGPUArchitecture::NVIDIA_Generic:
            return 32;
        case NISGPUArchitecture::AMD_Generic:
            return 32;
        case NISGPUArchitecture::Intel_Generic:
            return 32;
        }
        return 32;
    }

    constexpr uint32_t GetOptimalBlockHeight()
    {
        switch (gpuArch) {
        case NISGPUArchitecture::NVIDIA_Generic:
            return isUpscaling ? 24 : 32;
        case NISGPUArchitecture::AMD_Generic:
            return isUpscaling ? 24 : 32;
        case NISGPUArchitecture::Intel_Generic:
            return isUpscaling ? 24 : 32;
        }
        return isUpscaling ? 24 : 32;
    }

    constexpr uint32_t GetOptimalThreadGroupSize()
    {
        switch (gpuArch) {
        case NISGPUArchitecture::NVIDIA_Generic:
            return 256;
        case NISGPUArchitecture::AMD_Generic:
            return 256;
        case NISGPUArchitecture::Intel_Generic:
            return 256;
        }
        return 256;
    }
};


inline bool NVScalerUpdateConfig(NISConfig& config, float sharpness,
    uint32_t inputViewportOriginX, uint32_t inputViewportOriginY,
    uint32_t inputViewportWidth, uint32_t inputViewportHeight,
    uint32_t inputTextureWidth, uint32_t inputTextureHeight,
    uint32_t outputViewportOriginX, uint32_t outputViewportOriginY,
    uint32_t outputViewportWidth, uint32_t outputViewportHeight,
    uint32_t outputTextureWidth, uint32_t outputTextureHeight,
    NISHDRMode hdrMode = NISHDRMode::None)
{
    // adjust params based on value from sharpness slider
    sharpness = std::max<float>(std::min<float>(1.f, sharpness), 0.f);
    float sharpen_slider = sharpness - 0.5f;   // Map 0 to 1 to -0.5 to +0.5

    // Different range for 0 to 50% vs 50% to 100%
    // The idea is to make sure sharpness of 0% map to no-sharpening,
    // while also ensuring that sharpness of 100% doesn't cause too much over-sharpening.
    const float MaxScale = (sharpen_slider >= 0.0f) ? 1.25f : 1.75f;
    const float MinScale = (sharpen_slider >= 0.0f) ? 1.25f : 1.0f;
    const float LimitScale = (sharpen_slider >= 0.0f) ? 1.25f : 1.0f;

    float kDetectRatio = 1127.f / 1024.f;

    // Params for SDR
    float kDetectThres = 64.0f / 1024.0f;
    float kMinContrastRatio = 2.0f;
    float kMaxContrastRatio = 10.0f;

    float kSharpStartY = 0.45f;
    float kSharpEndY = 0.9f;
    float kSharpStrengthMin = std::max<float>(0.0f, 0.4f + sharpen_slider * MinScale * 1.2f);
    float kSharpStrengthMax = 1.6f + sharpen_slider * 1.8f;
    float kSharpLimitMin = std::max<float>(0.1f, 0.14f + sharpen_slider * LimitScale * 0.32f);
    float kSharpLimitMax = 0.5f + sharpen_slider * LimitScale * 0.6f;

    if (hdrMode == NISHDRMode::Linear || hdrMode == NISHDRMode::PQ)
    {
        kDetectThres = 32.0f / 1024.0f;

        kMinContrastRatio = 1.5f;
        kMaxContrastRatio = 5.0f;

        kSharpStrengthMin = std::max<float>(0.0f, 0.4f + sharpen_slider * MinScale * 1.1f);
        kSharpStrengthMax = 2.2f + sharpen_slider * MaxScale * 1.8f;
        kSharpLimitMin = std::max<float>(0.06f, 0.10f + sharpen_slider * LimitScale * 0.28f);
        kSharpLimitMax = 0.6f + sharpen_slider * LimitScale * 0.6f;

        if (hdrMode == NISHDRMode::PQ)
        {
            kSharpStartY = 0.35f;
            kSharpEndY = 0.55f;
        }
        else
        {
            kSharpStartY = 0.3f;
            kSharpEndY = 0.5f;
        }
    }

    float kRatioNorm = 1.0f / (kMaxContrastRatio - kMinContrastRatio);
    float kSharpScaleY = 1.0f / (kSharpEndY - kSharpStartY);
    float kSharpStrengthScale = kSharpStrengthMax - kSharpStrengthMin;
    float kSharpLimitScale = kSharpLimitMax - kSharpLimitMin;

    config.kInputViewportWidth = inputViewportWidth == 0 ? inputTextureWidth : inputViewportWidth;
    config.kInputViewportHeight = inputViewportHeight == 0 ? inputTextureHeight : inputViewportHeight;
    config.kOutputViewportWidth = outputViewportWidth == 0 ? outputTextureWidth : outputViewportWidth;
    config.kOutputViewportHeight = outputViewportHeight == 0 ? outputTextureHeight : outputViewportHeight;
    if (config.kInputViewportWidth == 0 || config.kInputViewportHeight == 0 ||
        config.kOutputViewportWidth == 0 || config.kOutputViewportHeight == 0)
        return false;

    config.kInputViewportOriginX = inputViewportOriginX;
    config.kInputViewportOriginY = inputViewportOriginY;
    config.kOutputViewportOriginX = outputViewportOriginX;
    config.kOutputViewportOriginY = outputViewportOriginY;

    config.kSrcNormX = 1.f / inputTextureWidth;
    config.kSrcNormY = 1.f / inputTextureHeight;
    config.kDstNormX = 1.f / outputTextureWidth;
    config.kDstNormY = 1.f / outputTextureHeight;
    config.kScaleX = config.kInputViewportWidth / float(config.kOutputViewportWidth);
    config.kScaleY = config.kInputViewportHeight / float(config.kOutputViewportHeight);
    if (config.kScaleX < 0.5f || config.kScaleX > 1.f || config.kScaleY < 0.5f || config.kScaleY > 1.f)
        return false;
    config.kDetectRatio = kDetectRatio;
    config.kDetectThres = kDetectThres;
    config.kMinContrastRatio = kMinContrastRatio;
    config.kRatioNorm = kRatioNorm;
    config.kContrastBoost = 1.0f;
    config.kEps = 1.0f;
    config.kSharpStartY = kSharpStartY;
    config.kSharpScaleY = kSharpScaleY;
    config.kSharpStrengthMin = kSharpStrengthMin;
    config.kSharpStrengthScale = kSharpStrengthScale;
    config.kSharpLimitMin = kSharpLimitMin;
    config.kSharpLimitScale = kSharpLimitScale;
    return true;
}


inline bool NVSharpenUpdateConfig(NISConfig& config, float sharpness,
    uint32_t inputViewportOriginX, uint32_t inputViewportOriginY,
    uint32_t inputViewportWidth, uint32_t inputViewportHeight,
    uint32_t inputTextureWidth, uint32_t inputTextureHeight,
    uint32_t outputViewportOriginX, uint32_t outputViewportOriginY,
    NISHDRMode hdrMode = NISHDRMode::None)
{
    return NVScalerUpdateConfig(config, sharpness,
            inputViewportOriginX, inputViewportOriginY, inputViewportWidth, inputViewportHeight, inputTextureWidth, inputTextureHeight,
            outputViewportOriginX, outputViewportOriginY, inputViewportWidth, inputViewportHeight, inputTextureWidth, inputTextureHeight,
            hdrMode);
}

namespace {
    constexpr size_t kPhaseCount = 64;
    constexpr size_t kFilterSize = 8;

    constexpr float coef_scale[kPhaseCount][kFilterSize] = {
        {0.0,     0.0,    1.0000, 0.0,     0.0,    0.0, 0.0, 0.0},
        {0.0029, -0.0127, 1.0000, 0.0132, -0.0034, 0.0, 0.0, 0.0},
        {0.0063, -0.0249, 0.9985, 0.0269, -0.0068, 0.0, 0.0, 0.0},
        {0.0088, -0.0361, 0.9956, 0.0415, -0.0103, 0.0005, 0.0, 0.0},
        {0.0117, -0.0474, 0.9932, 0.0562, -0.0142, 0.0005, 0.0, 0.0},
        {0.0142, -0.0576, 0.9897, 0.0713, -0.0181, 0.0005, 0.0, 0.0},
        {0.0166, -0.0674, 0.9844, 0.0874, -0.0220, 0.0010, 0.0, 0.0},
        {0.0186, -0.0762, 0.9785, 0.1040, -0.0264, 0.0015, 0.0, 0.0},
        {0.0205, -0.0850, 0.9727, 0.1206, -0.0308, 0.0020, 0.0, 0.0},
        {0.0225, -0.0928, 0.9648, 0.1382, -0.0352, 0.0024, 0.0, 0.0},
        {0.0239, -0.1006, 0.9575, 0.1558, -0.0396, 0.0029, 0.0, 0.0},
        {0.0254, -0.1074, 0.9487, 0.1738, -0.0439, 0.0034, 0.0, 0.0},
        {0.0264, -0.1138, 0.9390, 0.1929, -0.0488, 0.0044, 0.0, 0.0},
        {0.0278, -0.1191, 0.9282, 0.2119, -0.0537, 0.0049, 0.0, 0.0},
        {0.0288, -0.1245, 0.9170, 0.2310, -0.0581, 0.0059, 0.0, 0.0},
        {0.0293, -0.1294, 0.9058, 0.2510, -0.0630, 0.0063, 0.0, 0.0},
        {0.0303, -0.1333, 0.8926, 0.2710, -0.0679, 0.0073, 0.0, 0.0},
        {0.0308, -0.1367, 0.8789, 0.2915, -0.0728, 0.0083, 0.0, 0.0},
        {0.0308, -0.1401, 0.8657, 0.3120, -0.0776, 0.0093, 0.0, 0.0},
        {0.0313, -0.1426, 0.8506, 0.3330, -0.0825, 0.0103, 0.0, 0.0},
        {0.0313, -0.1445, 0.8354, 0.3540, -0.0874, 0.0112, 0.0, 0.0},
        {0.0313, -0.1460, 0.8193, 0.3755, -0.0923, 0.0122, 0.0, 0.0},
        {0.0313, -0.1470, 0.8022, 0.3965, -0.0967, 0.0137, 0.0, 0.0},
        {0.0308, -0.1479, 0.7856, 0.4185, -0.1016, 0.0146, 0.0, 0.0},
        {0.0303, -0.1479, 0.7681, 0.4399, -0.1060, 0.0156, 0.0, 0.0},
        {0.0298, -0.1479, 0.7505, 0.4614, -0.1104, 0.0166, 0.0, 0.0},
        {0.0293, -0.1470, 0.7314, 0.4829, -0.1147, 0.0181, 0.0, 0.0},
        {0.0288, -0.1460, 0.7119, 0.5049, -0.1187, 0.0190, 0.0, 0.0},
        {0.0278, -0.1445, 0.6929, 0.5264, -0.1226, 0.0200, 0.0, 0.0},
        {0.0273, -0.1431, 0.6724, 0.5479, -0.1260, 0.0215, 0.0, 0.0},
        {0.0264, -0.1411, 0.6528, 0.5693, -0.1299, 0.0225, 0.0, 0.0},
        {0.0254, -0.1387, 0.6323, 0.5903, -0.1328, 0.0234, 0.0, 0.0},
        {0.0244, -0.1357, 0.6113, 0.6113, -0.1357, 0.0244, 0.0, 0.0},
        {0.0234, -0.1328, 0.5903, 0.6323, -0.1387, 0.0254, 0.0, 0.0},
        {0.0225, -0.1299, 0.5693, 0.6528, -0.1411, 0.0264, 0.0, 0.0},
        {0.0215, -0.1260, 0.5479, 0.6724, -0.1431, 0.0273, 0.0, 0.0},
        {0.0200, -0.1226, 0.5264, 0.6929, -0.1445, 0.0278, 0.0, 0.0},
        {0.0190, -0.1187, 0.5049, 0.7119, -0.1460, 0.0288, 0.0, 0.0},
        {0.0181, -0.1147, 0.4829, 0.7314, -0.1470, 0.0293, 0.0, 0.0},
        {0.0166, -0.1104, 0.4614, 0.7505, -0.1479, 0.0298, 0.0, 0.0},
        {0.0156, -0.1060, 0.4399, 0.7681, -0.1479, 0.0303, 0.0, 0.0},
        {0.0146, -0.1016, 0.4185, 0.7856, -0.1479, 0.0308, 0.0, 0.0},
        {0.0137, -0.0967, 0.3965, 0.8022, -0.1470, 0.0313, 0.0, 0.0},
        {0.0122, -0.0923, 0.3755, 0.8193, -0.1460, 0.0313, 0.0, 0.0},
        {0.0112, -0.0874, 0.3540, 0.8354, -0.1445, 0.0313, 0.0, 0.0},
        {0.0103, -0.0825, 0.3330, 0.8506, -0.1426, 0.0313, 0.0, 0.0},
        {0.0093, -0.0776, 0.3120, 0.8657, -0.1401, 0.0308, 0.0, 0.0},
        {0.0083, -0.0728, 0.2915, 0.8789, -0.1367, 0.0308, 0.0, 0.0},
        {0.0073, -0.0679, 0.2710, 0.8926, -0.1333, 0.0303, 0.0, 0.0},
        {0.0063, -0.0630, 0.2510, 0.9058, -0.1294, 0.0293, 0.0, 0.0},
        {0.0059, -0.0581, 0.2310, 0.9170, -0.1245, 0.0288, 0.0, 0.0},
        {0.0049, -0.0537, 0.2119, 0.9282, -0.1191, 0.0278, 0.0, 0.0},
        {0.0044, -0.0488, 0.1929, 0.9390, -0.1138, 0.0264, 0.0, 0.0},
        {0.0034, -0.0439, 0.1738, 0.9487, -0.1074, 0.0254, 0.0, 0.0},
        {0.0029, -0.0396, 0.1558, 0.9575, -0.1006, 0.0239, 0.0, 0.0},
        {0.0024, -0.0352, 0.1382, 0.9648, -0.0928, 0.0225, 0.0, 0.0},
        {0.0020, -0.0308, 0.1206, 0.9727, -0.0850, 0.0205, 0.0, 0.0},
        {0.0015, -0.0264, 0.1040, 0.9785, -0.0762, 0.0186, 0.0, 0.0},
        {0.0010, -0.0220, 0.0874, 0.9844, -0.0674, 0.0166, 0.0, 0.0},
        {0.0005, -0.0181, 0.0713, 0.9897, -0.0576, 0.0142, 0.0, 0.0},
        {0.0005, -0.0142, 0.0562, 0.9932, -0.0474, 0.0117, 0.0, 0.0},
        {0.0005, -0.0103, 0.0415, 0.9956, -0.0361, 0.0088, 0.0, 0.0},
        {0.0, -0.0068, 0.0269, 0.9985, -0.0249, 0.0063, 0.0, 0.0},
        {0.0, -0.0034, 0.0132, 1.0000, -0.0127, 0.0029, 0.0, 0.0}
    };

    constexpr float coef_usm[kPhaseCount][kFilterSize] = {
        {0,      -0.6001, 1.2002, -0.6001,  0,      0, 0, 0},
        {0.0029, -0.6084, 1.1987, -0.5903, -0.0029, 0, 0, 0},
        {0.0049, -0.6147, 1.1958, -0.5791, -0.0068, 0.0005, 0, 0},
        {0.0073, -0.6196, 1.1890, -0.5659, -0.0103, 0, 0, 0},
        {0.0093, -0.6235, 1.1802, -0.5513, -0.0151, 0, 0, 0},
        {0.0112, -0.6265, 1.1699, -0.5352, -0.0195, 0.0005, 0, 0},
        {0.0122, -0.6270, 1.1582, -0.5181, -0.0259, 0.0005, 0, 0},
        {0.0142, -0.6284, 1.1455, -0.5005, -0.0317, 0.0005, 0, 0},
        {0.0156, -0.6265, 1.1274, -0.4790, -0.0386, 0.0005, 0, 0},
        {0.0166, -0.6235, 1.1089, -0.4570, -0.0454, 0.0010, 0, 0},
        {0.0176, -0.6187, 1.0879, -0.4346, -0.0532, 0.0010, 0, 0},
        {0.0181, -0.6138, 1.0659, -0.4102, -0.0615, 0.0015, 0, 0},
        {0.0190, -0.6069, 1.0405, -0.3843, -0.0698, 0.0015, 0, 0},
        {0.0195, -0.6006, 1.0161, -0.3574, -0.0796, 0.0020, 0, 0},
        {0.0200, -0.5928, 0.9893, -0.3286, -0.0898, 0.0024, 0, 0},
        {0.0200, -0.5820, 0.9580, -0.2988, -0.1001, 0.0029, 0, 0},
        {0.0200, -0.5728, 0.9292, -0.2690, -0.1104, 0.0034, 0, 0},
        {0.0200, -0.5620, 0.8975, -0.2368, -0.1226, 0.0039, 0, 0},
        {0.0205, -0.5498, 0.8643, -0.2046, -0.1343, 0.0044, 0, 0},
        {0.0200, -0.5371, 0.8301, -0.1709, -0.1465, 0.0049, 0, 0},
        {0.0195, -0.5239, 0.7944, -0.1367, -0.1587, 0.0054, 0, 0},
        {0.0195, -0.5107, 0.7598, -0.1021, -0.1724, 0.0059, 0, 0},
        {0.0190, -0.4966, 0.7231, -0.0649, -0.1865, 0.0063, 0, 0},
        {0.0186, -0.4819, 0.6846, -0.0288, -0.1997, 0.0068, 0, 0},
        {0.0186, -0.4668, 0.6460, 0.0093, -0.2144, 0.0073, 0, 0},
        {0.0176, -0.4507, 0.6055, 0.0479, -0.2290, 0.0083, 0, 0},
        {0.0171, -0.4370, 0.5693, 0.0859, -0.2446, 0.0088, 0, 0},
        {0.0161, -0.4199, 0.5283, 0.1255, -0.2598, 0.0098, 0, 0},
        {0.0161, -0.4048, 0.4883, 0.1655, -0.2754, 0.0103, 0, 0},
        {0.0151, -0.3887, 0.4497, 0.2041, -0.2910, 0.0107, 0, 0},
        {0.0142, -0.3711, 0.4072, 0.2446, -0.3066, 0.0117, 0, 0},
        {0.0137, -0.3555, 0.3672, 0.2852, -0.3228, 0.0122, 0, 0},
        {0.0132, -0.3394, 0.3262, 0.3262, -0.3394, 0.0132, 0, 0},
        {0.0122, -0.3228, 0.2852, 0.3672, -0.3555, 0.0137, 0, 0},
        {0.0117, -0.3066, 0.2446, 0.4072, -0.3711, 0.0142, 0, 0},
        {0.0107, -0.2910, 0.2041, 0.4497, -0.3887, 0.0151, 0, 0},
        {0.0103, -0.2754, 0.1655, 0.4883, -0.4048, 0.0161, 0, 0},
        {0.0098, -0.2598, 0.1255, 0.5283, -0.4199, 0.0161, 0, 0},
        {0.0088, -0.2446, 0.0859, 0.5693, -0.4370, 0.0171, 0, 0},
        {0.0083, -0.2290, 0.0479, 0.6055, -0.4507, 0.0176, 0, 0},
        {0.0073, -0.2144, 0.0093, 0.6460, -0.4668, 0.0186, 0, 0},
        {0.0068, -0.1997, -0.0288, 0.6846, -0.4819, 0.0186, 0, 0},
        {0.0063, -0.1865, -0.0649, 0.7231, -0.4966, 0.0190, 0, 0},
        {0.0059, -0.1724, -0.1021, 0.7598, -0.5107, 0.0195, 0, 0},
        {0.0054, -0.1587, -0.1367, 0.7944, -0.5239, 0.0195, 0, 0},
        {0.0049, -0.1465, -0.1709, 0.8301, -0.5371, 0.0200, 0, 0},
        {0.0044, -0.1343, -0.2046, 0.8643, -0.5498, 0.0205, 0, 0},
        {0.0039, -0.1226, -0.2368, 0.8975, -0.5620, 0.0200, 0, 0},
        {0.0034, -0.1104, -0.2690, 0.9292, -0.5728, 0.0200, 0, 0},
        {0.0029, -0.1001, -0.2988, 0.9580, -0.5820, 0.0200, 0, 0},
        {0.0024, -0.0898, -0.3286, 0.9893, -0.5928, 0.0200, 0, 0},
        {0.0020, -0.0796, -0.3574, 1.0161, -0.6006, 0.0195, 0, 0},
        {0.0015, -0.0698, -0.3843, 1.0405, -0.6069, 0.0190, 0, 0},
        {0.0015, -0.0615, -0.4102, 1.0659, -0.6138, 0.0181, 0, 0},
        {0.0010, -0.0532, -0.4346, 1.0879, -0.6187, 0.0176, 0, 0},
        {0.0010, -0.0454, -0.4570, 1.1089, -0.6235, 0.0166, 0, 0},
        {0.0005, -0.0386, -0.4790, 1.1274, -0.6265, 0.0156, 0, 0},
        {0.0005, -0.0317, -0.5005, 1.1455, -0.6284, 0.0142, 0, 0},
        {0.0005, -0.0259, -0.5181, 1.1582, -0.6270, 0.0122, 0, 0},
        {0.0005, -0.0195, -0.5352, 1.1699, -0.6265, 0.0112, 0, 0},
        {0, -0.0151, -0.5513, 1.1802, -0.6235, 0.0093, 0, 0},
        {0, -0.0103, -0.5659, 1.1890, -0.6196, 0.0073, 0, 0},
        {0.0005, -0.0068, -0.5791, 1.1958, -0.6147, 0.0049, 0, 0},
        {0, -0.0029, -0.5903, 1.1987, -0.6084, 0.0029, 0, 0}
    };
}