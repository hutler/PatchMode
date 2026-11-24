#include "Modules/ModuleManager.h"
#include "PatchEdMode.h"
#include "EditorModeRegistry.h"
#include "Styling/AppStyle.h"

class FPatchModeModule : public IModuleInterface
{
    void StartupModule() override
    {
        FEditorModeRegistry::Get().RegisterMode<FPatchEdMode>(
            FPatchEdMode::Id, FText::FromString(TEXT("Patch Mode")),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Modes"), true, 450);
    }
    void ShutdownModule() override
    {
        FEditorModeRegistry::Get().UnregisterMode(FPatchEdMode::Id);
    }
};
IMPLEMENT_MODULE(FPatchModeModule, PatchMode)
