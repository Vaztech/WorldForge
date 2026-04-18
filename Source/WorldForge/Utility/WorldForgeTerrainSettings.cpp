#include "Utility/WorldForgeTerrainSettings.h"

UWorldForgeTerrainSettings::UWorldForgeTerrainSettings()
{
    DefaultPerlinSettings.BaseFrequency = 0.005f;
    DefaultPerlinSettings.BaseAmplitude = 3.0f;
    DefaultPerlinSettings.OctaveCount = 4;
    DefaultPerlinSettings.Lacunarity = 2.0f;
    DefaultPerlinSettings.Persistence = 0.5f;
    DefaultPerlinSettings.MinHeight = 4;
    DefaultPerlinSettings.SeedOffset = 0;
}