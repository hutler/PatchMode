#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PatchOperations.h"
#include "PatchEdMode.h"
#include "PatchTestViewport.h"
#include "PatchBake.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Engine/SkyLight.h"
#include "Misc/PackageName.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerStart.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Tests/AutomationEditorCommon.h"

constexpr auto PatchTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchGeometryTest, "PatchMode.Geometry", PatchTestFlags)
bool FPatchGeometryTest::RunTest(const FString &)
{
    TArray<FPatchPoint> V;
    TArray<FIntVector> T;
    for (int32 I = 0; I <= int32(EPatchPrimitive::Rope); ++I)
    {
        auto G = FPatchGrid::Make(EPatchPrimitive(I), 3, 3, FVector(240, 80, 130), 20);
        TestTrue(TEXT("Valid primitive dimensions"), G.Valid());
        G.Tessellate(V, T);
        TestTrue(TEXT("Rendered surface exists"), !T.IsEmpty());
        for (const auto &P : V)
            if (P.Position.ContainsNaN())
            {
                AddError(TEXT("Non-finite primitive"));
                break;
            }
    }
    auto G = FPatchGrid::Make(EPatchPrimitive::Terrain, 2, 2, FVector(100), 0);
    G.At(1, 1).Position.Z = 64;
    G.Tessellate(V, T);
    TestEqual(TEXT("Two terrain triangles"), T.Num(), 2);
    const auto Before = T[0];
    G.Turned[0] = 1;
    G.Tessellate(V, T);
    TestTrue(TEXT("Diagonal has changed"), Before != T[0]);
    auto C = FPatchGrid::Make(EPatchPrimitive::Curve, 3, 3, FVector(200), 0);
    C.At(1, 1).Position.Z = 128;
    C.At(1, 1).Color.A = 0;
    const auto Original = C;
    TestEqual(TEXT("Quadratic center height"), C.Sample(.5, .5).Position.Z, 32.);
    TestEqual(TEXT("Quadratic center alpha"), C.Sample(.5, .5).Color.A, .75f);
    TestTrue(TEXT("de Casteljau insert"), PatchOps::Resize(C, EPatchAxis::Columns, 1, true, false));
    TestEqual(TEXT("Width after insert"), C.Width, 5);
    for (int32 I = 0; I <= 20; ++I)
        TestTrue(TEXT("Insertion preserves shape and UV"),
                 C.Sample(I / 20., .3).Position.Equals(Original.Sample(I / 20., .3).Position, .0001) &&
                     C.Sample(I / 20., .3).UV.Equals(Original.Sample(I / 20., .3).UV, .0001));
    FPatchGrid Right;
    TestTrue(TEXT("Split quadratic on anchor"), PatchOps::Split(C, EPatchAxis::Columns, 2, Right));
    TestTrue(TEXT("Split shares boundary"), C.Sample(1, .3).Position.Equals(Right.Sample(0, .3).Position));
    PatchOps::Transpose(G);
    PatchOps::Transpose(G);
    TestTrue(TEXT("Double transpose preserves diagonal"), G.Turned[0] == 1 && !G.bInverted);
    auto Shell = PatchOps::Thicken(G, 12, true);
    TestEqual(TEXT("Shell with four sides"), Shell.Num(), 5);
    TestEqual(TEXT("Open shell"), PatchOps::Thicken(G, 12, false).Num(), 1);
    TestTrue(TEXT("Back is reversed"), Shell[0].bInverted);
    auto Tube = FPatchGrid::Make(EPatchPrimitive::Cylinder, 3, 3, FVector(100), 0);
    TestEqual(TEXT("Closed tube has two open rims and no duplicate seam walls"),
              PatchOps::Thicken(Tube, 12, true).Num(), 3);
    auto Cap = PatchOps::Cap(Tube, 0, true, false);
    Cap.Tessellate(V, T);
    TestTrue(TEXT("Cap renders triangles"), !T.IsEmpty());
    for (int32 I = 0; I < Cap.Width; ++I)
        TestTrue(TEXT("Cap shares rim"), Cap.At(I, 0).Position.Equals(Tube.At(I, 0).Position));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchEditorTest, "PatchMode.EditorWorkflow", PatchTestFlags)
bool FPatchEditorTest::RunTest(const FString &)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    auto &Tools = GLevelEditorModeTools();
    Tools.ActivateMode(FPatchEdMode::Id);
    auto *Mode = Tools.GetActiveModeTyped<FPatchEdMode>(FPatchEdMode::Id);
    if (!TestNotNull(TEXT("Patch Mode registered and active"), Mode))
        return false;
    Mode->Execute(EPatchCommand::Terrain);
    auto *P = Mode->Active();
    if (!TestNotNull(TEXT("Terrain selected after creation"), P))
        return false;
    if (auto *Client = PatchTestViewport(); Client && Client->Viewport == GEditor->GetActiveViewport() && Client->IsOrtho())
        TestTrue(TEXT("Terrain front faces the orthographic camera"), P->GetActorUpVector().Equals(-Client->GetForwardVector()));
    Mode->SelectPoint(P, 0, false);
    Mode->Options->Position = FVector(-200, -200, 30);
    Mode->Execute(EPatchCommand::SetPosition);
    TestEqual(TEXT("Point edit"), P->Grid.Points[0].Position.Z, 30.);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo point edit"), P->Grid.Points[0].Position.Z, 0.);
    GEditor->RedoTransaction();
    TestEqual(TEXT("Redo point edit"), P->Grid.Points[0].Position.Z, 30.);
    Mode->Execute(EPatchCommand::Lock);
    Mode->Options->Position.Z = 99;
    Mode->Execute(EPatchCommand::SetPosition);
    TestEqual(TEXT("Locked point protected"), P->Grid.Points[0].Position.Z, 30.);
    Mode->Execute(EPatchCommand::Unlock);
    const auto OldUV = P->Grid.At(0, 0).LightUV;
    Mode->Execute(EPatchCommand::ShiftUV);
    TestTrue(TEXT("Texture edits preserve lightmap"), OldUV == P->Grid.At(0, 0).LightUV);
    Mode->Execute(EPatchCommand::BakeStaticMesh);
    auto *Baked = P->BakedActor.Get();
    if (!TestNotNull(TEXT("Baked native static mesh actor"), Baked))
        return false;
    UStaticMesh *Mesh = Baked->GetStaticMeshComponent()->GetStaticMesh();
    if (!TestNotNull(TEXT("Baked mesh asset"), Mesh))
        return false;
    Mode->Execute(EPatchCommand::BakeStaticMesh);
    TestTrue(TEXT("Rebake preserves both references"),
             P->BakedActor.Get() == Baked && Baked->GetStaticMeshComponent()->GetStaticMesh() == Mesh);
    FHitResult Hit;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(PatchTest), true);
    Q.AddIgnoredActor(P);
    const FVector Mid = P->GetActorTransform().TransformPosition(FVector(20, 20, 0));
    const FVector Above = P->GetActorUpVector() * 200.;
    TestTrue(
        TEXT("Baked mesh blocks visibility ray"),
        World->LineTraceSingleByChannel(Hit, Mid + Above, Mid - Above, ECC_Visibility, Q) &&
            Hit.GetActor() == Baked);
    Tools.DeactivateMode(FPatchEdMode::Id);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchToolsTest, "PatchMode.Operations", PatchTestFlags)
bool FPatchToolsTest::RunTest(const FString &)
{
    FAutomationEditorCommonUtils::CreateNewMap();
    auto &Modes = GLevelEditorModeTools();
    Modes.ActivateMode(FPatchEdMode::Id);
    auto *Mode = Modes.GetActiveModeTyped<FPatchEdMode>(FPatchEdMode::Id);
    if (!Mode)
        return false;
    Mode->Execute(EPatchCommand::Cylinder);
    auto *Tube = Mode->Active();
    if (!Tube)
        return false;
    Mode->Execute(EPatchCommand::Cap);
    int32 PatchCount = 0;
    for (TActorIterator<APatchSurface> It(Tube->GetWorld()); It; ++It)
        ++PatchCount;
    TestEqual(TEXT("Standard cylindrical cap closes both rims"), PatchCount, 3);
    Mode->Execute(EPatchCommand::Thicken);
    int32 ShellCount = 0;
    for (TActorIterator<APatchSurface> It(Tube->GetWorld()); It; ++It)
        ++ShellCount;
    TestTrue(TEXT("Thickening creates back and outer wall patches"), ShellCount > PatchCount);
    Mode->Execute(EPatchCommand::Curve);
    APatchSurface *A = Mode->Active();
    Mode->Execute(EPatchCommand::Insert);
    TestEqual(TEXT("Curve insertion adds two rows"), A->Grid.Height, 5);
    Mode->SelectPoint(A, A->Grid.Index(0, 2), false);
    Mode->Execute(EPatchCommand::Split);
    TestEqual(TEXT("Split selects an anchor"), A->Grid.Height, 3);
    Mode->Execute(EPatchCommand::IncreaseSubdivision);
    TestEqual(TEXT("Subdivision command"), A->Grid.Subdivisions, 5);
    Mode->Execute(EPatchCommand::ToTerrain);
    TestEqual(TEXT("Converted curve keeps resolution"), A->Grid.Width, 6);
    Mode->Options->Color = FLinearColor(.2f, .3f, .4f, .25f);
    Mode->Execute(EPatchCommand::SelectAllPoints);
    Mode->Execute(EPatchCommand::SetRGBA);
    TestEqual(TEXT("Alpha reaches the last control point"), A->Grid.Points.Last().Color.A, .25f);
    const auto UV = A->Grid.Points.Last().UV;
    Mode->Options->UVLayer = EPatchUVLayer::Smoothing;
    Mode->Execute(EPatchCommand::FlipU);
    TestTrue(TEXT("Smoothing field independent"),
             A->Grid.Points.Last().UV == UV && A->Grid.Points.Last().SmoothingUV.X != UV.X);
    Modes.DeactivateMode(FPatchEdMode::Id);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchBrushTest, "PatchMode.BrushAndWeld", PatchTestFlags)
bool FPatchBrushTest::RunTest(const FString &)
{
    auto *World = FAutomationEditorCommonUtils::CreateNewMap();
    auto &Modes = GLevelEditorModeTools();
    Modes.ActivateMode(FPatchEdMode::Id);
    auto *Mode = Modes.GetActiveModeTyped<FPatchEdMode>(FPatchEdMode::Id);
    if (!Mode)
        return false;
    Mode->Execute(EPatchCommand::Terrain);
    auto *A = Mode->Active();
    if (!A)
        return false;
    A->Grid = FPatchGrid::Make(EPatchPrimitive::Terrain, 3, 3, FVector(200), 0);
    A->Changed();
    const int32 Center = A->Grid.Index(1, 1);
    const FVector Normal = A->GetActorUpVector();
    const FVector Ray = A->GetActorLocation() + Normal * 200.;
    Mode->Options->Radius = 120;
    Mode->Options->Strength = 1;
    Mode->Options->Height = 12;
    A->Grid.At(0, 1).bLocked = true;
    Mode->Execute(EPatchCommand::Sculpt);
    Mode->BeginBrushStroke();
    TestTrue(TEXT("Brush ray hits the visible surface"), Mode->BrushRay(Ray, -Normal, 0));
    TestTrue(TEXT("Second stamp hits the raised surface"), Mode->BrushRay(Ray, -Normal, 0));
    Mode->EndBrushStroke();
    TestTrue(TEXT("Two stamps raise the vertex"), A->Grid.Points[Center].Position.Z > 20);
    TestEqual(TEXT("Locked neighbour unchanged"), A->Grid.At(0, 1).Position.Z, 0.);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Entire stroke undoes in one step"), A->Grid.Points[Center].Position.Z, 0.);
    Mode->Options->Brush = EPatchBrush::PaintAlpha;
    Mode->Options->Color.A = .25f;
    Mode->Execute(EPatchCommand::Paint);
    Mode->BeginBrushStroke();
    Mode->BrushRay(Ray, -Normal, 0);
    Mode->EndBrushStroke();
    TestEqual(TEXT("Painted alpha"), A->Grid.Points[Center].Color.A, .25f);
    TestEqual(TEXT("Paint does not change RGB"), A->Grid.Points[Center].Color.R, 1.f);
    Mode->Execute(EPatchCommand::Curve);
    auto *B = Mode->Active();
    Mode->Execute(EPatchCommand::Curve);
    auto *C = Mode->Active();
    B->SetActorLocation(FVector(0, 0, 0));
    C->SetActorLocation(FVector(0, 450, 0));
    GEditor->SelectActor(B, true, true);
    Mode->Execute(EPatchCommand::ConnectEdges);
    Mode->Execute(EPatchCommand::IncreaseSubdivision);
    TestTrue(TEXT("Welded curves share their tessellation count"),
             B->Grid.Subdivisions == 5 && C->Grid.Subdivisions == 5);
    Modes.DeactivateMode(FPatchEdMode::Id);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPatchPersistenceTest, "PatchMode.SaveReload", PatchTestFlags)
bool FPatchPersistenceTest::RunTest(const FString &)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("PatchBuildFixture")))
    {
        if (FPackageName::DoesPackageExist(TEXT("/Game/PatchWorkshop")) &&
            FApp::GetProjectName() == FString(TEXT("PatchStudio")))
        {
            const FString Filename = FPackageName::LongPackageNameToFilename(TEXT("/Game/PatchWorkshop"),
                                                                             FPackageName::GetMapPackageExtension());
            auto *Saved = UEditorLoadingAndSavingUtils::LoadMap(Filename);
            if (!TestNotNull(TEXT("Load example from disk"), Saved))
                return false;
            bool RoadFound = false;
            for (TActorIterator<APatchSurface> It(Saved); It; ++It)
                if (It->Grid.Kind == EPatchKind::Terrain && It->Grid.Valid())
                    RoadFound = It->Grid.Turned[0] == 1 && It->Grid.At(0, 3).Color.A == .5f && It->BakedActor.IsValid();
            TestTrue(TEXT("Reload persisted controls, attributes and baked reference"), RoadFound);
        }
        auto World = FAutomationEditorCommonUtils::CreateNewMap();
        auto *A = World->SpawnActor<APatchSurface>();
        A->Grid = FPatchGrid::Make(EPatchPrimitive::Curve, 5, 3, FVector(180), 0);
        A->Grid.At(1, 1).Color.A = .375f;
        A->Changed();
        // Duplicate through the ordinary editor serialization path.
        auto *Copy = CastChecked<APatchSurface>(StaticDuplicateObject(A, World->PersistentLevel));
        TestTrue(TEXT("Copied grid retains attributes"), Copy->Grid.At(1, 1).Color.A == .375f && Copy->Grid.Width == 5);
        TestTrue(TEXT("Copy has independent bake identity"), Copy->SourceId != A->SourceId);
        return true;
    }
    auto *World = FAutomationEditorCommonUtils::CreateNewMap();
    const FString Map = TEXT("/Game/PatchWorkshop");
    if (FPackageName::DoesPackageExist(Map))
    {
        AddError(TEXT("Fixture level already exists; refusing to overwrite it."));
        return false;
    }
    auto *Road = World->SpawnActor<APatchSurface>();
    Road->Grid = FPatchGrid::Make(EPatchPrimitive::Terrain, 4, 7, FVector(550, 1100, 1), 0);
    Road->SetActorLabel(TEXT("Road_Patch"));
    for (int32 Y = 0; Y < Road->Grid.Height; ++Y)
        for (int32 X = 0; X < Road->Grid.Width; ++X)
        {
            auto &P = Road->Grid.At(X, Y);
            P.Position.X += 140 * FMath::Square(double(Y) / 6.);
            P.Position.Z = 35 * FMath::Sin(double(Y) / 6. * PI) + 4 * X;
            P.Color.A = float(Y) / 6;
        }
    Road->Grid.Turned[0] = 1;
    Road->Changed();
    auto *Arch = World->SpawnActor<APatchSurface>();
    Arch->Grid = FPatchGrid::Make(EPatchPrimitive::EndCap, 5, 3, FVector(470, 480, 140), 0);
    for (auto &P : Arch->Grid.Points)
        P.Position = FVector(P.Position.X, P.Position.Z, P.Position.Y);
    Arch->SetActorLocation(FVector(70, 180, 0));
    Arch->SetActorLabel(TEXT("Arch_Patch"));
    Arch->Changed();
    TArray<UPackage *> Packages;
    for (auto *Patch : {Road, Arch})
    {
        FString Message;
        if (!TestTrue(TEXT("Bake example"), PatchBake::Bake(*Patch, Message)))
            return false;
        Packages.Add(Patch->BakedActor->GetStaticMeshComponent()->GetStaticMesh()->GetPackage());
    }
    auto *Light = World->SpawnActor<ADirectionalLight>();
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetActorRotation(FRotator(-58, -25, 0));
    World->SpawnActor<ASkyLight>();
    World->SpawnActor<APlayerStart>(FVector(0, -380, 140), FRotator(0, 90, 0));
    if (!TestTrue(TEXT("Save example assets"), UEditorLoadingAndSavingUtils::SavePackages(Packages, false)))
        return false;
    if (!TestTrue(TEXT("Save native level"), UEditorLoadingAndSavingUtils::SaveMap(World, Map)))
        return false;
    auto *Reopened = UEditorLoadingAndSavingUtils::LoadMap(
        FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension()));
    if (!TestNotNull(TEXT("Reopened level"), Reopened))
        return false;
    int32 Bakes = 0, Sources = 0;
    for (TActorIterator<APatchSurface> It(Reopened); It; ++It)
    {
        ++Sources;
        if (It->BakedActor.IsValid())
            ++Bakes;
        if (It->Grid.Kind == EPatchKind::Terrain)
            TestTrue(TEXT("Stored terrain attributes survive reopening"),
                     It->Grid.Turned[0] == 1 && It->Grid.At(0, 3).Color.A == .5f);
    }
    TestTrue(TEXT("Source and baked actor links survive reopening"), Bakes == 2 && Sources == 2);
    return true;
}
#endif
