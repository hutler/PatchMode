using UnrealBuildTool;
public class PatchMode : ModuleRules
{
    public PatchMode(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "Slate", "SlateCore", "InputCore", "EditorFramework", "LevelEditor",
            "GeometryCore", "GeometryFramework", "MeshDescription", "StaticMeshDescription",
            "AssetRegistry", "PropertyEditor", "RenderCore", "ApplicationCore"
        });
    }
}
