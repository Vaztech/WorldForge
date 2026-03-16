using UnrealBuildTool;
using System.IO;

public class WorldForge : ModuleRules
{
    public WorldForge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "GameplayTags",
            "Json",
            "JsonUtilities",
            "ProceduralMeshComponent",
            "NavigationSystem",
            "AIModule",
            "RenderCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "PCG",
        });

        // PrivateIncludePaths: lets cpp files use flat includes
        PrivateIncludePaths.AddRange(new string[]
        {
            ModuleDirectory,
            Path.Combine(ModuleDirectory, "Colony"),
            Path.Combine(ModuleDirectory, "Colony", "Processors"),
            Path.Combine(ModuleDirectory, "Data"),
            Path.Combine(ModuleDirectory, "World"),
            Path.Combine(ModuleDirectory, "Economy"),
            Path.Combine(ModuleDirectory, "Save"),
            Path.Combine(ModuleDirectory, "Debug"),
            Path.Combine(ModuleDirectory, "Voxel"),
            Path.Combine(ModuleDirectory, "Visual"),
            Path.Combine(ModuleDirectory, "Races"),
            Path.Combine(ModuleDirectory, "Civilization"),
        });

        // Locate project Plugins directory
        string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
        string ProjectPluginsDir = Path.Combine(ProjectRoot, "Plugins");

        // PaperZD -- only if plugin installed
        string PaperZDPath = Path.Combine(ProjectPluginsDir, "PaperZD");
        if (Directory.Exists(PaperZDPath))
        {
            PublicDependencyModuleNames.Add("PaperZD");
            PublicDefinitions.Add("WITH_PAPERZD=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_PAPERZD=0");
        }

        // Voxel Plugin -- only if installed
        string VoxelPath = Path.Combine(ProjectPluginsDir, "VoxelPlugin");
        if (Directory.Exists(VoxelPath))
        {
            PublicDependencyModuleNames.AddRange(new string[]
            {
                "Voxel",
                "VoxelGraph",
                "VoxelHelpers"
            });

            PublicDefinitions.Add("WITH_VOXEL_PLUGIN=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_VOXEL_PLUGIN=0");
        }
    }
}