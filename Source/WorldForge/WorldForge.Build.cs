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
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "PCG",
        });

        // Resolve project plugin directory safely (since this is not a plugin module)
        string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../"));
        string PluginsDir = Path.Combine(ProjectRoot, "Plugins");

        // PaperZD -- only add if plugin folder exists
        string PaperZDPath = Path.Combine(PluginsDir, "PaperZD");
        bool bPaperZDInstalled = Directory.Exists(PaperZDPath);
        if (bPaperZDInstalled)
        {
            PublicDependencyModuleNames.Add("PaperZD");
            PublicDefinitions.Add("WITH_PAPERZD=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_PAPERZD=0");
        }

        // Voxel Plugin -- only add if plugin folder exists
        // Note: module name is 'Voxel' for the legacy free version
        string VoxelPath = Path.Combine(PluginsDir, "VoxelPlugin");
        bool bVoxelInstalled = Directory.Exists(VoxelPath);
        if (bVoxelInstalled)
        {
            PublicDependencyModuleNames.Add("Voxel");
            PublicDefinitions.Add("WITH_VOXEL_PLUGIN=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_VOXEL_PLUGIN=0");
        }
    }
}