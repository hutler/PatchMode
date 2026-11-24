#pragma once
#include "Toolkits/BaseToolkit.h"

enum class EPatchCommand : uint8;
class FPatchEdMode;
class SExpandableArea;
class FPatchToolkit : public FModeToolkit
{
  public:
    explicit FPatchToolkit(FPatchEdMode *InMode) : Mode(InMode)
    {
    }
    void Build();
    void OpenUV();
    virtual FName GetToolkitFName() const override
    {
        return TEXT("PatchMode");
    }
    virtual FText GetBaseToolkitName() const override
    {
        return FText::FromString(TEXT("Patch Mode"));
    }
    virtual FEdMode *GetEditorMode() const override;
    virtual TSharedPtr<SWidget> GetInlineContent() const override
    {
        return Panel;
    }

  private:
    void ChooseTool(EPatchCommand);
    FPatchEdMode *Mode;
    TSharedPtr<SWidget> Panel;
    TSharedPtr<SExpandableArea> ContextArea, UVArea;
};
