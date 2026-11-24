#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PatchEdMode.h"
#include "PatchOperations.h"
#include "PatchTestViewport.h"
#include "Tests/AutomationEditorCommon.h"

using Cmd = EPatchCommand;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchFeatherTest, "PatchMode.FeatherEdgeAlpha",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPatchFeatherTest::RunTest(const FString &)
{
    const FTransform Transform(FRotator(20, 45, 5), FVector(120, 40, -10), FVector(2));
    for (int32 Side = 0; Side < 4; ++Side)
    {
        auto G = FPatchGrid::Make(EPatchPrimitive::Curve, 5, 3, FVector(200, 100, 1), 0);
        const auto Edge = PatchOps::EdgeIndices(G, Side);
        const int32 Inward[] = {G.Width, -1, -G.Width, 1};
        const int32 Inside = Edge[0] + Inward[Side];
        const TSet<int32> Selection{Edge[0], Edge[1], Inside};
        G.Points[Edge[1]].bLocked = true;
        const FLinearColor Original(.2f, .35f, .4f, .8f);
        G.Points[Inside].Color = Original;
        TestEqual(TEXT("Only unlocked selected controls change"),
                  PatchOps::FeatherEdgeAlpha(G, Side, Transform, 200, &Selection), 2);
        TestTrue(TEXT("Edge, world scale, RGB and lock respected"),
                 G.Points[Edge[0]].Color.A == 0 && G.Points[Edge[1]].Color.A == 1 &&
                 G.Points[Edge[2]].Color.A == 1 && G.Points[Inside].Color.Equals(FLinearColor(.2f,.35f,.4f,.5f)));
        TestEqual(TEXT("Repeating the mask is idempotent"), PatchOps::FeatherEdgeAlpha(G, Side, Transform, 200, &Selection), 0);
        G.Points[Inside].Color.A = .1f;
        TestEqual(TEXT("Lower prepainted alpha is preserved"), PatchOps::FeatherEdgeAlpha(G, Side, Transform, 200, &Selection), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchBoundaryTest, "PatchMode.BoundaryExtrusion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPatchBoundaryTest::RunTest(const FString &)
{
    const FTransform Transform(FRotator(20, 35, 0), FVector(40, 90, -30), FVector(2, 3, 1));
    for (int32 Side = 0; Side < 4; ++Side)
    {
        auto G = FPatchGrid::Make(EPatchPrimitive::Terrain, 4, 3, FVector(180, 240, 1), 0);
        auto Edge = PatchOps::EdgeIndices(G, Side);
        const int32 Inward[] = {G.Width, -1, -G.Width, 1};
        auto &Pt = G.Points[Edge[0]];
        Pt.Color = FLinearColor(.1f, .3f, .7f, .4f);
        Pt.bLocked = true;
        Pt.LightUV += FVector2D(3, 2);
        Pt.SmoothingUV -= FVector2D(1, 4);
        const auto Source = Pt, Inside = G.Points[Edge[0] + Inward[Side]];
        const FVector Offset = Transform.TransformVector(Source.Position - Inside.Position) * .5;
        TestTrue(TEXT("Extrusion accepted on every side"), PatchOps::ExtrudeBoundary(G, Side, Transform, Offset));
        const auto New = G.Points[PatchOps::EdgeIndices(G, Side)[0]];
        TestTrue(TEXT("World distance, rotation and scale"),
                 Transform.TransformVector(New.Position - Source.Position).Equals(Offset, .001));
        TestTrue(TEXT("All UV layers extend at source density"),
                 New.UV.Equals(Source.UV + (Source.UV - Inside.UV) * .5) &&
                     New.LightUV.Equals(Source.LightUV + (Source.LightUV - Inside.LightUV) * .5) &&
                     New.SmoothingUV.Equals(Source.SmoothingUV + (Source.SmoothingUV - Inside.SmoothingUV) * .5));
        TestTrue(TEXT("Boundary attributes survive"), New.Color.Equals(Source.Color) && New.bLocked);
        TestFalse(TEXT("Zero distance adds no topology"),
                  PatchOps::ExtrudeBoundary(G, Side, Transform, FVector::ZeroVector));
    }
    auto Full = FPatchGrid::Make(EPatchPrimitive::Terrain, 16, 16, FVector(100), 0);
    TestFalse(TEXT("Dimensions stay within the terrain limit"),
              PatchOps::ExtrudeBoundary(Full, 0, Transform, FVector::UpVector));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchInteractionTest, "PatchMode.Interaction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPatchInteractionTest::RunTest(const FString &)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    auto &Modes = GLevelEditorModeTools();
    Modes.ActivateMode(FPatchEdMode::Id);
    auto *Mode = Modes.GetActiveModeTyped<FPatchEdMode>(FPatchEdMode::Id);
    FLevelEditorViewportClient *Client = PatchTestViewport();
    if (!TestNotNull(TEXT("Active patch mode"), Mode) || !TestNotNull(TEXT("Level viewport"), Client))
        return false;
    AddInfo(FString::Printf(TEXT("Interaction viewport: %s"), *UEnum::GetValueAsString(Client->GetViewportType())));
    FViewport *VP = Client->Viewport;
    const FIntPoint Original(VP->GetMouseX(), VP->GetMouseY());
    ON_SCOPE_EXIT
    {
        VP->SetMouse(Original.X, Original.Y);
        Modes.DeactivateMode(FPatchEdMode::Id);
    };
    Modes.SetWidgetMode(UE::Widget::WM_Translate);
    const FVector Camera(400, -630, 610);
    FPatchTestCamera TestCamera(*Client, Camera, FVector::ZeroVector);
    FSceneViewFamilyContext Family(
        FSceneViewFamily::ConstructionValues(VP, Client->GetScene(), Client->EngineShowFlags));
    const auto *View = Client->CalcSceneView(&Family);
    if (!TestNotNull(TEXT("Scene view"), View))
        return false;
    // The floor and patches share the view plane in each orthographic projection.
    const FQuat Plane = Client->IsOrtho() ? FQuat::FindBetweenNormals(FVector::UpVector, -View->GetViewDirection())
                                          : FQuat::Identity;
    auto Pixel = [&](FVector Local) {
        FVector2D XY = FVector2D::ZeroVector;
        View->WorldToPixel(Plane.RotateVector(Local), XY);
        return XY;
    };
    auto Move = [&](FVector2D XY) {
        const int32 X = FMath::RoundToInt(XY.X), Y = FMath::RoundToInt(XY.Y);
        VP->SetMouse(X, Y);
        Mode->MouseMove(Client, VP, X, Y);
    };
    auto Press = [&](FKey Key = EKeys::LeftMouseButton) { return Mode->InputKey(Client, VP, Key, IE_Pressed); };
    auto Release = [&] { return Mode->InputKey(Client, VP, EKeys::LeftMouseButton, IE_Released); };
    auto Click = [&](FVector At) {
        Move(Pixel(At));
        Press();
        Release();
    };
    auto *Floor = World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Floor->GetStaticMeshComponent()->SetStaticMesh(
        LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->SetActorScale3D(FVector(15, 15, .5));
    Floor->SetActorLocationAndRotation(Plane.RotateVector(FVector(0, 0, -26)), Plane);
    auto G = FPatchGrid::Make(EPatchPrimitive::Terrain, 3, 3, FVector(200), 0);
    auto *P = Mode->Spawn(G, FTransform(Plane), TEXT("Pointer test"));
    Mode->SelectPatch(P);
    Mode->ActivateTool(Cmd::Select);
    const int32 I = G.Index(1, 1);
    Click(G.Points[I].Position);
    TestTrue(TEXT("Control selected above floor"), Mode->Active() == P && Mode->Points.Contains(I));
    const FVector Axis = Client->IsOrtho() ? Plane.UnrotateVector(Plane.GetAxisX().GetAbs()) : FVector::UpVector;
    const auto ScreenAxis = (Pixel(Axis * 100.) - Pixel(FVector::ZeroVector)).GetSafeNormal();
    Move(Pixel(FVector::ZeroVector) + ScreenAxis * 25);
    Press();
    Move(Pixel(FVector::ZeroVector) + ScreenAxis * 55);
    Release();
    const FVector Moved = P->Grid.Points[I].Position - G.Points[I].Position;
    TestTrue(TEXT("Axis moves only the chosen point"), Moved.Dot(Axis) > 0 &&
                 Moved.Equals(Axis * Moved.Dot(Axis), .01) && P->Grid.Points[0].Position.Equals(G.Points[0].Position));
    TestTrue(TEXT("Drag retains selection"), Mode->Active() == P && Mode->Points.Contains(I));
    GEditor->UndoTransaction();
    TestTrue(TEXT("One undo restores point"), P->Grid.Points[I].Position.Equals(G.Points[I].Position));
    Mode->ActivateTool(Cmd::Paint);
    Mode->Options->Edge = EPatchBoundary::FirstRow;
    Mode->Options->FeatherWidth = 200;
    Mode->Execute(Cmd::FeatherAlpha);
    TestTrue(TEXT("Paint feathers edge alpha in world centimeters"),
             P->Grid.At(1, 0).Color.A == 0 && FMath::IsNearlyEqual(P->Grid.At(1, 1).Color.A, .5f) &&
                 P->Grid.At(1, 2).Color.A == 1 && P->Display == EPatchPreview::VertexAlpha);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores the alpha mask"), P->Grid.At(1, 0).Color.A, 1.f);
    Mode->ActivateTool(Cmd::Extrude);
    const FVector Edge(100, 0, 0);
    Move(Pixel(Edge));
    Press();
    Move(Pixel(Edge + FVector(60, 0, 0)));
    Release();
    TestEqual(TEXT("Drag adds a single boundary"), P->Grid.Width, 4);
    if (P->Grid.Width == 4)
    {
        TestTrue(TEXT("New UV advances and original edge stays put"),
                 P->Grid.At(3, 1).UV.X > 1. && P->Grid.At(2, 1).Position.Equals(Edge));
        const FVector Rim = P->Grid.At(3, 1).Position;
        Move(Pixel(Rim));
        Press();
        Move(Pixel(Rim + FVector(80, 0, 0)));
        Press(EKeys::Escape);
        Release();
        TestEqual(TEXT("Escape discards second extrusion"), P->Grid.Width, 4);
        GEditor->UndoTransaction();
        TestEqual(TEXT("One undo removes the first extrusion"), P->Grid.Width, 3);
    }
    Mode->ActivateTool(Cmd::Extrude);
    Move(Pixel(Edge));
    Press();
    Move(Pixel(Edge + FVector(40, 0, 0)));
    Press(EKeys::Eight);
    Press(EKeys::BackSpace);
    Press(EKeys::NumPadSeven);
    Press(EKeys::Decimal);
    Press(EKeys::Five);
    Move(Pixel(Edge + FVector(80, 0, 0)));
    Press(EKeys::Enter);
    Release();
    TestTrue(TEXT("Typed 7.5 cm overrides snapping and mouse motion, Enter adds exactly one column"),
             P->Grid.Width == 4 && P->Grid.At(3, 1).Position.Equals(Edge + FVector(7.5, 0, 0), .001));
    GEditor->UndoTransaction();
    TestEqual(TEXT("One undo removes numeric extrusion"), P->Grid.Width, 3);
    Move(Pixel(Edge));
    Press();
    Press(EKeys::One);
    Press(EKeys::Escape);
    Release();
    TestEqual(TEXT("Esc cancels typed extrusion"), P->Grid.Width, 3);
    Mode->ActivateTool(Cmd::Cylinder);
    Click(FVector(0, -200, 0));
    Press(EKeys::Four);
    Press(EKeys::Zero);
    Press(EKeys::Enter);
    Press(EKeys::Nine);
    Press(EKeys::Zero);
    Press(EKeys::Enter);
    auto *Tube = Mode->Active();
    TestTrue(TEXT("Numeric radius and height commit at base"),
             Tube && Tube != P && Tube->Grid.Kind == EPatchKind::Curve &&
                 FMath::IsNearlyEqual(Tube->Grid.At(0, 0).Position.X, 40.) &&
                 FMath::IsNearlyEqual(Tube->Grid.At(0, 2).Position.Z, 90.));
    Mode->ActivateTool(Cmd::Rope);
    Click(FVector(0, -100, 0));
    Move(Pixel(FVector(0, 100, 0)));
    TestTrue(TEXT("Rope preview leaves actors untouched"), Mode->Active() == Tube);
    Click(FVector(0, 100, 0));
    TestTrue(TEXT("Second world anchor commits the rope"), Mode->Active() != Tube && Mode->Active()->Grid.Height == 5);
    auto *A = Mode->Spawn(FPatchGrid::Make(EPatchPrimitive::Curve, 3, 3, FVector(120), 0),
                          FTransform(Plane, Plane.RotateVector(FVector(-155, 0, 40))), TEXT("Source"));
    auto *B = Mode->Spawn(A->Grid, FTransform(Plane, Plane.RotateVector(FVector(155, 0, 40))), TEXT("Destination"));
    Mode->SelectPatch(A);
    Mode->ActivateTool(Cmd::ConnectEdges);
    const FVector Source(-95, 0, 40), Destination(95, 0, 40);
    const FPatchGrid OriginalDestination = B->Grid;
    B->Grid = FPatchGrid::Make(EPatchPrimitive::Curve, 3, 5, FVector(120), 0);
    B->Changed();
    Click(Source);
    Click(Destination);
    TestTrue(TEXT("Mismatched edges report both counts"), !Mode->CanConfirm() &&
             Mode->Status.ToString().Contains(TEXT("3 pts, target 5 pts")));
    B->Grid = OriginalDestination;
    B->Grid.Kind = EPatchKind::Terrain;
    B->Changed();
    Click(Destination);
    TestTrue(TEXT("Mismatched types identify the required patch"), !Mode->CanConfirm() &&
             Mode->Status.ToString().Contains(TEXT("Choose a Curve patch")));
    B->Grid = OriginalDestination;
    B->Changed();
    A->Grid.At(2, 1).bLocked = true;
    Click(Destination);
    TestTrue(TEXT("A blocked pair can be reversed but cannot be applied"), Mode->HasSeamPair() && !Mode->CanConfirm() &&
             Mode->Status.ToString().Contains(TEXT("Unlock source point")));
    Press(EKeys::Enter);
    TestTrue(TEXT("Locked edge remains unchanged"), A->GetActorTransform().TransformPosition(A->Grid.At(2, 1).Position)
                                                       .Equals(Plane.RotateVector(Source)));
    A->Grid.At(2, 1).bLocked = false;
    TestTrue(TEXT("Unlocking enables the same pair"), Mode->CanConfirm());
    TestTrue(TEXT("Two matching edges preview"), Mode->HasSeamPair());
    TestTrue(TEXT("Source untouched before apply"),
             A->GetActorTransform().TransformPosition(A->Grid.At(2, 1).Position).Equals(Plane.RotateVector(Source)));
    Press(EKeys::R);
    Press(EKeys::Escape);
    TestTrue(TEXT("Cancel leaves source unchanged"),
             A->GetActorTransform().TransformPosition(A->Grid.At(2, 1).Position).Equals(Plane.RotateVector(Source)));
    Mode->ActivateTool(Cmd::ConnectEdges);
    Click(Source);
    Click(Destination);
    Press(EKeys::Enter);
    TestTrue(TEXT("Confirm connects picked edges"),
             A->GetActorTransform().TransformPosition(A->Grid.At(2, 1).Position).Equals(Plane.RotateVector(Destination)));
    if (Client->IsOrtho())
    {
        // These controls occupy one pixel. Put the farther one first in storage.
        Mode->SelectPatch(P);
        Mode->ActivateTool(Cmd::Select);
        const bool SavedDrillDown = Mode->Options->bDrillDown;
        Mode->Options->bDrillDown = true;
        P->Grid.Points[0].Position = G.Points[I].Position - FVector::UpVector * 30.;
        P->Changed();
        Click(G.Points[I].Position);
        TestTrue(TEXT("Overlapping controls select the one nearest the camera"), Mode->Points.Contains(I) && !Mode->Points.Contains(0));
        Mode->Options->bDrillDown = SavedDrillDown;
        Mode->ActivateTool(Cmd::Curve);
        Click(FVector(180, 200, 0));
        Click(FVector(220, 230, 0));
        auto *Curve = Mode->Active();
        TestTrue(TEXT("Placed curve faces the camera"), Curve && Curve != A && Curve != B &&
                     Curve->Grid.Kind == EPatchKind::Curve && Curve->GetActorUpVector().Equals(-View->GetViewDirection()));
        Mode->ActivateTool(Cmd::DrawTerrain);
        Move(Pixel(FVector(180, 200, 0)));
        Press();
        Move(Pixel(FVector(125, 240, 0)));
        Release();
        auto *Drawn = Mode->Active();
        TestTrue(TEXT("Rectangle drag creates a view-facing terrain with the requested dimensions"),
                 Drawn && Drawn != Curve && Drawn->Grid.Kind == EPatchKind::Terrain &&
                 Drawn->GetActorUpVector().Equals(-View->GetViewDirection()) &&
                 Drawn->Grid.At(1, 1).Position.Equals(FVector(-55, 40, 0), 4.));
    }
    return true;
}
#endif
