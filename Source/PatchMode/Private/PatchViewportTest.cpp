#if WITH_DEV_AUTOMATION_TESTS
#include "PatchEdMode.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "SceneView.h"
#include "Tests/AutomationEditorCommon.h"
#include "Widgets/SWindow.h"

class FPatchCapture : public IAutomationLatentCommand
{
  public:
    FPatchCapture(FAutomationTestBase *InTest) : Test(InTest), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Start < 3)
            return false;
        auto Window = FSlateApplication::Get().GetActiveTopLevelWindow();
        if (!Window)
        {
            auto Windows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
            if (!Windows.IsEmpty())
                Window = Windows[0];
        }
        if (!Window)
        {
            Test->AddError(TEXT("No editor window for the screenshot."));
            return true;
        }
        TArray<FColor> Pixels;
        FIntVector Size;
        if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size) || Pixels.IsEmpty())
        {
            Test->AddError(TEXT("Slate did not return a screenshot."));
            return true;
        }
        for (auto &Pixel : Pixels)
            Pixel.A = 255;
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
        Test->TestTrue(TEXT("Saved actual editor viewport"),
                       FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("PatchMode-Editor.png"))));
        GLevelEditorModeTools().DeactivateMode(FPatchEdMode::Id);
        return true;
    }

  private:
    FAutomationTestBase *Test;
    double Start;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchViewportTest, "PatchMode.Viewport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPatchViewportTest::RunTest(const FString &)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    auto &Modes = GLevelEditorModeTools();
    Modes.ActivateMode(FPatchEdMode::Id);
    auto *Mode = Modes.GetActiveModeTyped<FPatchEdMode>(FPatchEdMode::Id);
    if (!TestNotNull(TEXT("Mode active for viewport routing"), Mode))
        return false;
    auto *P = World->SpawnActor<APatchSurface>();
    P->Grid = FPatchGrid::Make(EPatchPrimitive::Terrain, 4, 7, FVector(560, 1000, 1), 0);
    for (auto &Point : P->Grid.Points)
        Point.Position.X += 180 * FMath::Square(Point.UV.Y);
    P->SetActorLabel(TEXT("Road_Patch_01"));
    P->Changed();
    if (FParse::Param(FCommandLine::Get(), TEXT("PatchPreviewPNG")))
    {
        auto Window=FSlateApplication::Get().GetActiveTopLevelWindow();
        if(!Window)
        {
            const auto Windows=FSlateApplication::Get().GetInteractiveTopLevelWindows();
            if(!Windows.IsEmpty()) Window=Windows[0];
        }
        if(Window) Window->Resize(FVector2D(1550,930));
        auto *Light = World->SpawnActor<ADirectionalLight>();
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetActorRotation(FRotator(-52, -35, 0));
        World->SpawnActor<ASkyLight>();
        auto *Floor = World->SpawnActor<AStaticMeshActor>();
        auto *FloorMesh = Floor->GetStaticMeshComponent();
        FloorMesh->SetMobility(EComponentMobility::Movable);
        FloorMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->SetActorLabel(TEXT("Workshop_Floor"));
        Floor->SetActorLocation(FVector(0, 0, -55));
        Floor->SetActorScale3D(FVector(40, 40, 1));
        auto *Arch = World->SpawnActor<APatchSurface>();
        Arch->Grid = FPatchGrid::Make(EPatchPrimitive::EndCap, 5, 3, FVector(500, 500, 100), 0);
        for (auto &Point : Arch->Grid.Points)
            Point.Position = FVector(Point.Position.X, Point.Position.Z, Point.Position.Y);
        Arch->SetActorLocation(FVector(150, 340, 0));
        Arch->SetActorLabel(TEXT("Arch_Patch"));
        Arch->Changed();
    }
    Mode->SelectPatch(P);
    Mode->Execute(EPatchCommand::TurnEdge);
    FLevelEditorViewportClient *Client = nullptr;
    for (auto *Candidate : GEditor->GetLevelViewportClients())
        if (Candidate && Candidate->Viewport && Candidate->IsPerspective())
        {
            Client = Candidate;
            if (Candidate->Viewport == GEditor->GetActiveViewport())
                break;
        }
    if (!TestNotNull(TEXT("Perspective level viewport available"), Client))
        return false;
    const FVector Camera(610, -780, 650);
    Client->SetViewLocation(Camera);
    Client->SetViewRotation((FVector(65, 0, 0) - Camera).Rotation());
    FSceneViewFamilyContext Family(
        FSceneViewFamily::ConstructionValues(Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
    const FSceneView *View = Client->CalcSceneView(&Family);
    const FVector Center = (P->Grid.At(1, 1).Position + P->Grid.At(2, 2).Position) * .5;
    FVector2D Pixel=FVector2D::ZeroVector;
    if (!TestTrue(TEXT("Terrain cell projects to viewport"), View && View->WorldToPixel(Center, Pixel)))
        return false;
    FViewportClick Click(View, Client, EKeys::LeftMouseButton, IE_Pressed, FMath::RoundToInt(Pixel.X),
                         FMath::RoundToInt(Pixel.Y));
    TestTrue(TEXT("Turn Edge handles actual viewport click"), Mode->HandleClick(Client, nullptr, Click));
    TestTrue(TEXT("Only pointed terrain cell turned"), P->Grid.Turned[P->Grid.Cell(1, 1)] == 1);
    Client->Invalidate();
    if (FParse::Param(FCommandLine::Get(), TEXT("PatchPreviewPNG")))
    {
        ADD_LATENT_AUTOMATION_COMMAND(FPatchCapture(this));
    }
    else
        Modes.DeactivateMode(FPatchEdMode::Id);
    return true;
}
#endif
