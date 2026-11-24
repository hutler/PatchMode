#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Brush.h"
#include "Engine/Selection.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "PatchBake.h"
#include "PatchEdMode.h"
#include "PatchOperations.h"
#include "PatchToolkit.h"
#include "PatchViewPlane.h"

using Cmd = EPatchCommand;

TArray<APatchSurface *> FPatchEdMode::SelectedPatches() const
{
    TArray<APatchSurface *> Patches;
    for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
        if (auto *P = Cast<APatchSurface>(*It))
            Patches.Add(P);
    return Patches;
}

APatchSurface *FPatchEdMode::Active() const
{
    return GEditor->GetSelectedActors()->GetTop<APatchSurface>();
}
void FPatchEdMode::SelectPatch(APatchSurface *P)
{
    GEditor->SelectNone(false, true, false);
    GEditor->SelectActor(P, true, true);
    SelectionOwner = P;
    Focus = 0;
    Points.Reset();
}
APatchSurface *FPatchEdMode::Spawn(const FPatchGrid &G, const FTransform &Transform, const FString &Name,
                                   UMaterialInterface *Mat)
{
    FActorSpawnParameters Args;
    Args.ObjectFlags |= RF_Transactional;
    Args.OverrideLevel = GetWorld()->GetCurrentLevel();
    APatchSurface *P = GetWorld()->SpawnActor<APatchSurface>(APatchSurface::StaticClass(), Transform, Args);
    if (P)
    {
        P->Modify();
        P->Grid = G;
        P->Material = Mat;
        P->SetActorLabel(Name);
        P->Changed();
    }
    return P;
}

TOptional<EPatchPrimitive> FPatchEdMode::Primitive(Cmd C)
{
    switch (C)
    {
    case Cmd::Terrain:
        return EPatchPrimitive::Terrain;
    case Cmd::Curve:
        return EPatchPrimitive::Curve;
    case Cmd::Bevel:
        return EPatchPrimitive::Bevel;
    case Cmd::SquareBevel:
        return EPatchPrimitive::SquareBevel;
    case Cmd::EndCap:
        return EPatchPrimitive::EndCap;
    case Cmd::SquareEndCap:
        return EPatchPrimitive::SquareEndCap;
    case Cmd::Cylinder:
        return EPatchPrimitive::Cylinder;
    case Cmd::DenseCylinder:
        return EPatchPrimitive::DenseCylinder;
    case Cmd::VeryDenseCylinder:
        return EPatchPrimitive::VeryDenseCylinder;
    case Cmd::SquareCylinder:
        return EPatchPrimitive::SquareCylinder;
    case Cmd::Cone:
        return EPatchPrimitive::Cone;
    case Cmd::Rope:
        return EPatchPrimitive::Rope;
    default:
        return {};
    }
}
bool FPatchEdMode::CanExecute(Cmd C) const
{
    if (!GEditor || GEditor->PlayWorld)
        return false;
    if (Primitive(C) || C == Cmd::Select || C == Cmd::Faces || C == Cmd::DrawTerrain)
        return true;
    const auto *P = Active();
    if (!P || !P->Grid.Valid())
        return false;
    switch (C)
    {
    case Cmd::WeldPoints:
        return Points.Num() >= 2;
    case Cmd::Split: {
        const bool Row = Options->Axis == EPatchWorkAxis::Rows;
        const int32 I = Row ? Focus / P->Grid.Width : Focus % P->Grid.Width;
        return I > 0 && I < (Row ? P->Grid.Height : P->Grid.Width) - 1 &&
               (P->Grid.Kind == EPatchKind::Terrain || I % 2 == 0);
    }
    case Cmd::ToTerrain:
    case Cmd::IncreaseSubdivision:
    case Cmd::DecreaseSubdivision:
        return P->Grid.Kind == EPatchKind::Curve;
    case Cmd::TurnEdge:
    case Cmd::AutoTurnEdges:
    case Cmd::Extrude:
        return P->Grid.Kind == EPatchKind::Terrain;
    default:
        return true;
    }
}

FText FPatchEdMode::CommandHint(Cmd C) const
{
    FString Hint;
    if (!CanExecute(C))
    {
        if (!GEditor || GEditor->PlayWorld)
            Hint = TEXT("Available while editing the level.");
        else if (!Active())
            Hint = TEXT("Select a patch in the viewport or Outliner.");
        else if (C == Cmd::WeldPoints)
            Hint = TEXT("Ctrl-click at least two control points.");
        else if (C == Cmd::Split)
            Hint = TEXT("Pick an interior row or column. Curves split at even-numbered anchors.");
        else if (C == Cmd::Extrude || C == Cmd::TurnEdge || C == Cmd::AutoTurnEdges)
            Hint = TEXT("Select a terrain patch.");
        else
            Hint = TEXT("Select a curved patch.");
    }
    else if (Primitive(C))
        Hint = C == Cmd::Rope ? TEXT("Click two anchors to place a rope.")
                              : TEXT("Place the base in the viewport, then size the preview.");
    else if (C == Cmd::Extrude)
        Hint = TEXT("Drag a terrain boundary. Type a length in cm while dragging. Release or press Enter to apply.");
    else if (C == Cmd::FeatherAlpha)
        Hint = TEXT("Fade vertex alpha from the chosen boundary over the feather width. Existing lower alpha is kept.");
    else if (C == Cmd::ConnectEdges)
        Hint = TEXT("Pick the source edge, then the destination edge. R reverses, Enter applies.");
    else
        for (const auto &Entry : PatchCommands)
            if (C == Entry.Id)
                Hint = Entry.Label;
    return FText::FromString(Hint);
}

void FPatchEdMode::ActivateTool(Cmd C)
{
    if (!CanExecute(C))
    {
        Status = CommandHint(C);
        return;
    }
    ResetInteraction(true);
    Tool = Cmd::Select;
    if (Primitive(C) || C == Cmd::Extrude || C == Cmd::ConnectEdges)
    {
        Tool = C;
        Status = CommandHint(C);
    }
    else
        Execute(C);
    GEditor->RedrawLevelEditingViewports();
}

static void SurfaceUV(FPatchGrid &G, Cmd C, const UPatchSettings &S, int32 Plane)
{
    const bool Light = C == Cmd::LightmapFit || S.UVLayer == EPatchUVLayer::Lightmap;
    for (int32 Y = 0; Y < G.Height; ++Y)
    {
        double Across = 0;
        for (int32 X = 0; X < G.Width; ++X)
        {
            FPatchPoint &P = G.At(X, Y);
            if (X)
                Across += FVector::Distance(P.Position, G.At(X - 1, Y).Position);
            if (P.bLocked)
                continue;
            FVector2D &UV = Light ? P.LightUV : (S.UVLayer == EPatchUVLayer::Smoothing ? P.SmoothingUV : P.UV);
            switch (C)
            {
            case Cmd::Fit:
            case Cmd::LightmapFit:
                UV = FVector2D(double(X) / (G.Width - 1), double(Y) / (G.Height - 1));
                break;
            case Cmd::Natural: {
                double Along = 0;
                for (int32 I = 1; I <= Y; ++I)
                    Along += FVector::Distance(G.At(X, I).Position, G.At(X, I - 1).Position);
                UV = FVector2D(Across, Along) / S.UnitsPerTile;
                break;
            }
            case Cmd::CapProjection:
            case Cmd::CycleProjection:
                UV = FVector2D(P.Position[Plane % 3], P.Position[(Plane + 1) % 3]) / S.UnitsPerTile;
                break;
            case Cmd::ShiftUV:
                UV += S.UVShift;
                break;
            case Cmd::ScaleUV:
                UV = (UV - FVector2D(.5, .5)) * S.UVScale + FVector2D(.5, .5);
                break;
            case Cmd::RotateUV:
                UV = (UV - FVector2D(.5, .5)).GetRotated(S.Rotation) + FVector2D(.5, .5);
                break;
            case Cmd::FlipU:
                UV.X = 1 - UV.X;
                break;
            case Cmd::FlipV:
                UV.Y = 1 - UV.Y;
                break;
            default:
                break;
            }
        }
    }
}

void FPatchEdMode::Execute(Cmd C)
{
    if (!CanExecute(C))
        return;
    const TCHAR *Label = TEXT("Patch");
    for (const auto &Entry : PatchCommands)
        if (Entry.Id == C)
        {
            Label = Entry.Label;
            break;
        }
    const FScopedTransaction Change(FText::FromString(FString(TEXT("Patch: ")) + Label));
    SetStatus(Label);
    UPatchSettings &S = *Options;
    if (auto Shape = Primitive(C))
    {
        FVector Size = S.Size.GetAbs().ComponentMax(FVector(1)), Location = GEditor->ClickLocation;
        FQuat Rotation = FQuat::Identity;
        TArray<AActor *> Selected;
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
            if (auto *A = Cast<AActor>(*It))
                Selected.Add(A);
        if (Selected.Num() == 1 && !Cast<APatchSurface>(Selected[0]))
        {
            auto Bounds = Selected[0]->GetComponentsBoundingBox(true);
            if (Bounds.IsValid && !Bounds.GetExtent().IsNearlyZero())
            {
                Size = Bounds.GetSize().ComponentMax(FVector(1));
                Location = Bounds.GetCenter();
                Location.Z = Bounds.Min.Z;
            }
        }
        if ((C == Cmd::Terrain || C == Cmd::Curve) && GEditor->GetActiveViewport())
        {
            for (auto *View : GEditor->GetLevelViewportClients())
                if (View && View->Viewport == GEditor->GetActiveViewport() && View->IsOrtho())
                {
                    Rotation = PatchPlaneRotation(*View);
                    break;
                }
        }
        if (C == Cmd::Rope)
        {
            Size.Y = 2 * S.RopeRadius;
            if (Selected.Num() == 2)
            {
                const FVector A = Selected[0]->GetActorLocation(), B = Selected[1]->GetActorLocation();
                if (A.Equals(B))
                {
                    SetStatus(TEXT("Choose two different rope endpoints."));
                    return;
                }
                Location = (A + B) * .5;
                Size.X = (B - A).Size();
                Rotation = (B - A).Rotation().Quaternion();
            }
        }
        const FVector Down = Rotation.UnrotateVector(-FVector::UpVector);
        auto G = FPatchGrid::Make(*Shape, S.Columns, S.Rows, Size, Size.X * S.RopeSagPercent / 100., Down);
        if (auto *New = Spawn(G, FTransform(Rotation, Location), Label))
        {
            SelectPatch(New);
            if (S.bReplaceSourceBrush && C != Cmd::Rope && Selected.Num() == 1)
            {
                auto *Brush = Cast<ABrush>(Selected[0]);
                if (Brush && !Brush->IsVolumeBrush() && Brush != GetWorld()->GetDefaultBrush())
                    GetWorld()->EditorDestroyActor(Brush, true);
            }
            if (S.bRemoveRopeTargetPoints && C == Cmd::Rope && Selected.Num() == 2)
                for (AActor *Anchor : Selected)
                    if (Anchor->IsA<ATargetPoint>())
                        GetWorld()->EditorDestroyActor(Anchor, true);
        }
        Tool = Cmd::Select;
        return;
    }
    if (C == Cmd::Select || C == Cmd::DrawTerrain || C == Cmd::Faces || C == Cmd::TurnEdge || C == Cmd::Sculpt ||
        C == Cmd::Paint)
    {
        Tool = C;
        if (C == Cmd::DrawTerrain)
            SetStatus(TEXT("Drag a rectangle in the viewport to create terrain."));
        if (C == Cmd::Faces)
            SetStatus(TEXT("Click a static mesh triangle or a brush face."));
        if (C == Cmd::Paint && Active() && !Active()->Material)
        {
            Active()->Modify();
            Active()->Display =
                S.Brush == EPatchBrush::PaintAlpha ? EPatchPreview::VertexAlpha : EPatchPreview::VertexRGB;
            Active()->Rebuild();
        }
        if (C == Cmd::Paint && S.Brush != EPatchBrush::PaintAlpha)
            S.Brush = EPatchBrush::PaintRGB;
        if (C == Cmd::Sculpt && (S.Brush == EPatchBrush::PaintRGB || S.Brush == EPatchBrush::PaintAlpha))
            S.Brush = EPatchBrush::Height;
        GEditor->RedrawLevelEditingViewports();
        return;
    }
    APatchSurface *P = Active();
    if (!P)
        return;
    if (SelectionOwner.Get() != P)
    {
        SelectionOwner = P;
        Focus = 0;
        Points.Reset();
    }
    FPatchGrid &G = P->Grid;
    Focus = FMath::Clamp(Focus, 0, G.Points.Num() - 1);
    const int32 X = Focus % G.Width, Y = Focus / G.Width;
    const EPatchAxis Axis = S.Axis == EPatchWorkAxis::Rows ? EPatchAxis::Rows : EPatchAxis::Columns;
    const int32 Index = Axis == EPatchAxis::Rows ? Y : X, N = Axis == EPatchAxis::Rows ? G.Height : G.Width;
    if (C == Cmd::ReadPoint)
    {
        ReadPoint();
        return;
    }
    if (C == Cmd::InspectUV)
    {
        if (Toolkit.IsValid())
            StaticCastSharedPtr<FPatchToolkit>(Toolkit)->OpenUV();
        return;
    }
    if (C == Cmd::ClearSelection)
    {
        Points.Reset();
        GEditor->RedrawLevelEditingViewports();
        return;
    }
    if (C == Cmd::SelectRow || C == Cmd::SelectColumn || C == Cmd::SelectAllPoints)
    {
        Points.Reset();
        for (int32 I = 0; I < G.Points.Num(); ++I)
            if (C == Cmd::SelectAllPoints || (C == Cmd::SelectRow ? I / G.Width == Y : I % G.Width == X))
                Points.Add(I);
        GEditor->RedrawLevelEditingViewports();
        return;
    }
    if (C == Cmd::CycleUVLayer)
    {
        S.UVLayer = EPatchUVLayer((int32(S.UVLayer) + 1) % 3);
        return;
    }
    if (C == Cmd::BakeStaticMesh)
    {
        for (auto *Patch : SelectedPatches())
        {
            FString Message;
            PatchBake::Bake(*Patch, Message);
            SetStatus(Message);
        }
        GEditor->RedrawLevelEditingViewports();
        return;
    }
    if (C >= Cmd::WeldPoints && C <= Cmd::SnaptoPrefab)
    {
        Join(C, *P);
        return;
    }
    P->Modify();
    switch (C)
    {
    case Cmd::FeatherAlpha: {
        const int32 Count = PatchOps::FeatherEdgeAlpha(G, int32(S.Edge), P->GetActorTransform(), S.FeatherWidth,
                                                       S.bOnlySelected ? &Points : nullptr);
        SetStatus(FString::Printf(TEXT("Feather Edge Alpha changed %d control points."), Count));
        if (!Count)
            return;
        S.Brush = EPatchBrush::PaintAlpha;
        if (!P->Material)
            P->Display = EPatchPreview::VertexAlpha;
        break;
    }
    case Cmd::ToTerrain: {
        const FPatchGrid Original = G;
        const int32 Steps = FMath::Clamp(G.Subdivisions, 1, 32);
        const int32 UCount = (G.Width - 1) / 2 * Steps, VCount = (G.Height - 1) / 2 * Steps;
        for (int32 V = 0; V < VCount; V += 15)
            for (int32 U = 0; U < UCount; U += 15)
            {
                auto Tile = FPatchGrid::Make(EPatchPrimitive::Terrain, FMath::Min(16, UCount - U + 1),
                                             FMath::Min(16, VCount - V + 1), FVector::OneVector, 0);
                Tile.bInverted = Original.bInverted;
                for (int32 Row = 0; Row < Tile.Height; ++Row)
                    for (int32 Col = 0; Col < Tile.Width; ++Col)
                        Tile.At(Col, Row) = Original.Sample(double(U + Col) / UCount, double(V + Row) / VCount);
                if (U == 0 && V == 0)
                    G = MoveTemp(Tile);
                else
                    Spawn(Tile, P->GetActorTransform(), TEXT("Terrain_Tile"), P->Material);
            }
        break;
    }
    case Cmd::Insert:
    case Cmd::Append:
    case Cmd::Prepend:
    case Cmd::Extrude:
    case Cmd::Remove:
    case Cmd::RemoveFirst:
    case Cmd::RemoveLast: {
        const bool Insert = C == Cmd::Insert || C == Cmd::Append || C == Cmd::Prepend || C == Cmd::Extrude;
        const int32 At =
            C == Cmd::Append || C == Cmd::RemoveLast ? N - 1 : (C == Cmd::Prepend || C == Cmd::RemoveFirst ? 0 : Index);
        if (C == Cmd::Extrude && At != 0 && At != N - 1)
        {
            SetStatus(TEXT("Select a boundary row or column to extrude."));
            return;
        }
        if (!PatchOps::Resize(G, Axis, At, Insert, C == Cmd::Append || C == Cmd::Prepend || C == Cmd::Extrude))
        {
            SetStatus(TEXT("Patch dimensions are at their limit."));
            return;
        }
        break;
    }
    case Cmd::Split: {
        FPatchGrid Other;
        if (!PatchOps::Split(G, Axis, Index, Other))
        {
            SetStatus(TEXT("Select an interior row/column; use an even anchor index on a curve."));
            return;
        }
        Spawn(Other, P->GetActorTransform(), P->GetActorLabel() + TEXT("_Split"), P->Material);
        break;
    }
    case Cmd::Invert:
        G.bInverted = !G.bInverted;
        break;
    case Cmd::Transpose:
        PatchOps::Transpose(G);
        break;
    case Cmd::RedistributeRows:
    case Cmd::RedistributeColumns:
    case Cmd::RedistributeAll:
        if (C != Cmd::RedistributeRows)
            PatchOps::Redistribute(G, EPatchAxis::Rows);
        if (C != Cmd::RedistributeColumns)
            PatchOps::Redistribute(G, EPatchAxis::Columns);
        break;
    case Cmd::IncreaseSubdivision:
    case Cmd::DecreaseSubdivision:
        SetConnectedSubdivisions(*P, FMath::Clamp(G.Subdivisions + (C == Cmd::IncreaseSubdivision ? 1 : -1), 1, 32));
        break;
    case Cmd::AutoTurnEdges:
        for (int32 J = 0; J < G.Height - 1; ++J)
            for (int32 I = 0; I < G.Width - 1; ++I)
                G.Turned[G.Cell(I, J)] = FVector::DistSquared(G.At(I + 1, J).Position, G.At(I, J + 1).Position) <
                                         FVector::DistSquared(G.At(I, J).Position, G.At(I + 1, J + 1).Position);
        break;
    case Cmd::Cap: {
        const bool Inside = S.CapShape != EPatchCap::OutsideBevel && S.CapShape != EPatchCap::OutsideEndCap;
        const bool End = S.CapShape == EPatchCap::OutsideEndCap || S.CapShape == EPatchCap::InsideEndCap;
        TArray<int32> Edges;
        if (S.CapShape == EPatchCap::Standard)
        {
            if (G.At(0, 0).Position.Equals(G.At(G.Width - 1, 0).Position, .001))
                Edges.Add(0);
            if (G.At(G.Width - 1, 0).Position.Equals(G.At(G.Width - 1, G.Height - 1).Position, .001))
                Edges.Add(1);
            if (G.At(0, G.Height - 1).Position.Equals(G.At(G.Width - 1, G.Height - 1).Position, .001))
                Edges.Add(2);
            if (G.At(0, 0).Position.Equals(G.At(0, G.Height - 1).Position, .001))
                Edges.Add(3);
        }
        if (Edges.IsEmpty())
            Edges.Add(int32(S.Edge));
        for (int32 Edge : Edges)
        {
            auto Cap = PatchOps::Cap(G, Edge, Inside, End);
            TArray<FPatchPoint> Verts;
            TArray<FIntVector> Tris;
            Cap.Tessellate(Verts, Tris);
            if (!Tris.IsEmpty())
                Spawn(Cap, P->GetActorTransform(), TEXT("Patch_Cap"), P->Material);
            else
                SetStatus(TEXT("The boundary is flat or collapsed; its cap has no area."));
        }
        return;
    }
    case Cmd::Thicken:
        for (auto &Part : PatchOps::Thicken(G, S.Thickness, S.bCreateSeams))
            Spawn(Part, P->GetActorTransform(), TEXT("Patch_Shell"), P->Material);
        return;
    case Cmd::SetUV:
    case Cmd::Lock:
    case Cmd::Unlock:
    case Cmd::SetPosition:
    case Cmd::SetRGBA:
    case Cmd::NoiseHeight:
    case Cmd::NoiseAlpha:
    case Cmd::SnaptoGrid: {
        FRandomStream R(S.NoiseSeed);
        const auto Indices = Points.IsEmpty() ? TSet<int32>{Focus} : Points;
        const FVector Delta = S.Position - G.Points[Focus].Position;
        for (int32 I : Indices)
        {
            if (!G.Points.IsValidIndex(I))
                continue;
            FPatchPoint &V = G.Points[I];
            if (C == Cmd::Lock || C == Cmd::Unlock)
            {
                V.bLocked = C == Cmd::Lock;
                continue;
            }
            if (V.bLocked)
                continue;
            if (C == Cmd::SetPosition)
                V.Position += Delta;
            if (C == Cmd::SetRGBA)
                V.Color = S.Color;
            if (C == Cmd::SetUV)
            {
                auto &UV = S.UVLayer == EPatchUVLayer::Texture
                               ? V.UV
                               : (S.UVLayer == EPatchUVLayer::Lightmap ? V.LightUV : V.SmoothingUV);
                UV = S.UVValue;
            }
            if (C == Cmd::NoiseHeight)
                V.Position.Z += R.FRandRange(-S.Height, S.Height);
            if (C == Cmd::NoiseAlpha)
                V.Color.A = FMath::Clamp(V.Color.A + R.FRandRange(-S.Strength, S.Strength), 0.f, 1.f);
            if (C == Cmd::SnaptoGrid)
                V.Position = P->GetActorTransform().InverseTransformPosition(
                    P->GetActorTransform().TransformPosition(V.Position).GridSnap(GEditor->GetGridSize()));
        }
        break;
    }
    default:
        if (C == Cmd::CycleProjection)
            Projection = (Projection + 1) % 3;
        SurfaceUV(G, C, S, Projection);
        break;
    }
    if (C == Cmd::ToTerrain || (C >= Cmd::Insert && C <= Cmd::Transpose) || C == Cmd::Split)
    {
        Points.Reset();
        Focus = 0;
    }
    P->Changed();
    GEditor->RedrawLevelEditingViewports();
}
