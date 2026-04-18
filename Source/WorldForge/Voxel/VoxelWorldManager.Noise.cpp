#include "VoxelWorldManager.h"

const int32 UVoxelWorldManager::PermutationTable[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

float UVoxelWorldManager::Fade(float T)
{
    return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
}

float UVoxelWorldManager::Lerp(float T, float A, float B)
{
    return A + T * (B - A);
}

float UVoxelWorldManager::Grad(int32 Hash, float X, float Y)
{
    const int32 H = Hash & 3;
    const float U = (H < 2) ? X : -X;
    const float V = (H == 0 || H == 3) ? Y : -Y;
    return U + V;
}

float UVoxelWorldManager::PerlinNoise2D(float X, float Y)
{
    const int32 Xi = static_cast<int32>(FMath::FloorToFloat(X)) & 255;
    const int32 Yi = static_cast<int32>(FMath::FloorToFloat(Y)) & 255;

    const float Xf = X - FMath::FloorToFloat(X);
    const float Yf = Y - FMath::FloorToFloat(Y);

    const float U = Fade(Xf);
    const float V = Fade(Yf);

    const int32 AA = PermutationTable[PermutationTable[Xi] + Yi];
    const int32 AB = PermutationTable[PermutationTable[Xi] + Yi + 1];
    const int32 BA = PermutationTable[PermutationTable[Xi + 1] + Yi];
    const int32 BB = PermutationTable[PermutationTable[Xi + 1] + Yi + 1];

    const float X1 = Lerp(U, Grad(AA, Xf, Yf), Grad(BA, Xf - 1.0f, Yf));
    const float X2 = Lerp(U, Grad(AB, Xf, Yf - 1.0f), Grad(BB, Xf - 1.0f, Yf - 1.0f));

    return Lerp(V, X1, X2);
}

float UVoxelWorldManager::FractalNoise2D(float X, float Y, const FPerlinOctaveSettings& Settings)
{
    const int32 ClampedOctaves = FMath::Clamp(Settings.OctaveCount, 1, 8);
    const float SafeFreq = FMath::Max(0.0001f, Settings.BaseFrequency);
    const float SafeAmp = FMath::Max(0.1f, Settings.BaseAmplitude);

    float Total = 0.0f;
    float Frequency = SafeFreq;
    float Amplitude = 1.0f;
    float MaxAmplitude = 0.0f;

    for (int32 i = 0; i < ClampedOctaves; ++i)
    {
        const float OffsetX = static_cast<float>(Settings.SeedOffset * 17 + i * 31);
        const float OffsetY = static_cast<float>(Settings.SeedOffset * 13 + i * 47);

        Total += PerlinNoise2D(
            (X + OffsetX) * Frequency,
            (Y + OffsetY) * Frequency
        ) * Amplitude;

        MaxAmplitude += Amplitude;
        Frequency *= Settings.Lacunarity;
        Amplitude *= Settings.Persistence;
    }

    const float Normalized = Total / FMath::Max(0.001f, MaxAmplitude);
    return Normalized * SafeAmp;
}

FPerlinOctaveSettings UVoxelWorldManager::GetOctaveSettingsForProfile(const FVoxelBiomeProfile& Profile) const
{
    FPerlinOctaveSettings Settings = DefaultPerlinSettings;

    if (Profile.NoiseFrequency > KINDA_SMALL_NUMBER)
    {
        Settings.BaseFrequency = Profile.NoiseFrequency;
    }

    if (Profile.NoiseAmplitude > KINDA_SMALL_NUMBER)
    {
        const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;
        Settings.BaseAmplitude = FMath::Max(0.5f, Profile.NoiseAmplitude / SafeVoxelSize);
    }

    if (Profile.OctaveCount > 0)
    {
        Settings.OctaveCount = FMath::Clamp(Profile.OctaveCount, 1, 8);
    }

    return Settings;
}

int32 UVoxelWorldManager::ComputeSmoothSurfaceHeightAtVoxel(
    int32 VoxelX,
    int32 VoxelY,
    const FVoxelBiomeProfile& Profile
) const
{
    const FPerlinOctaveSettings Settings = GetOctaveSettingsForProfile(Profile);

    const float Height = FractalNoise2D(
        static_cast<float>(VoxelX),
        static_cast<float>(VoxelY),
        Settings
    );

    const float FinalHeight = FMath::Clamp(Height, -32.0f, 32.0f);
    return FMath::RoundToInt(FinalHeight);
}

float UVoxelWorldManager::ComputeDetailNoiseHeightBilinear(
    int32 MinX,
    int32 MinY,
    int32 MaxX,
    int32 MaxY,
    int32 X,
    int32 Y,
    const FVoxelBiomeProfile& Profile
) const
{
    const float FX = static_cast<float>(X - MinX) / static_cast<float>(FMath::Max(1, MaxX - MinX));
    const float FY = static_cast<float>(Y - MinY) / static_cast<float>(FMath::Max(1, MaxY - MinY));

    const float H00 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MinX, MinY, Profile));
    const float H10 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MaxX, MinY, Profile));
    const float H01 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MinX, MaxY, Profile));
    const float H11 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MaxX, MaxY, Profile));

    const float X0 = FMath::Lerp(H00, H10, FX);
    const float X1 = FMath::Lerp(H01, H11, FX);
    return FMath::Lerp(X0, X1, FY);
}