using UnrealBuildTool;
using System.IO;

public class WorldForge : ModuleRules
{
    public WorldForge(ReadOnlyTargetRules Target) : base(Target)
    {
        // Use explicit/shared PCHs (standard for UE5 modules)
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // =========================
        // Public Dependencies
        // =========================
        // These modules are exposed to other modules that depend on WorldForge
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",              // Core UE types/macros
            "CoreUObject",       // UObject system
            "Engine",            // Core engine systems
            "InputCore",         // Input handling
            "EnhancedInput",     // UE5 enhanced input system

            "GameplayTags",      // FGameplayTag system + NativeGameplayTags.h support

            "Json",              // JSON parsing
            "JsonUtilities",     // JSON <-> UStruct conversion

            "ProceduralMeshComponent", // Runtime mesh generation
            "NavigationSystem",        // Nav system (used by NavigationContext)
            "AIModule",                // AI helpers (pathing, etc.)
            "RenderCore",              // Low-level rendering access
        });

        // =========================
        // Private Dependencies
        // =========================
        // Only used internally in this module
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",        // UI framework
            "SlateCore",    // Core UI types
            "PCG",          // Procedural Content Generation (used in world gen)
        });

        // =========================
        // Include Paths
        // =========================
        // Allows flat includes instead of long relative paths
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

        // =========================
        // Plugin Detection
        // =========================

        // Resolve project Plugins directory
        string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
        string ProjectPluginsDir = Path.Combine(ProjectRoot, "Plugins");

        // ---- PaperZD (2D sprite animation system) ----
        // Optional dependency: only enabled if plugin exists
        string PaperZDPath = Path.Combine(ProjectPluginsDir, "PaperZD");
        if (Directory.Exists(PaperZDPath))
        {
            PublicDependencyModuleNames.Add("PaperZD");
            PublicDefinitions.Add("WITH_PAPERZD=1"); // Used for #if guards in code
        }
        else
        {
            PublicDefinitions.Add("WITH_PAPERZD=0");
        }

        // ---- Voxel Plugin ----
        // Required for smooth SDF terrain (Phase 1+)
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