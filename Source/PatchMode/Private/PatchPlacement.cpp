#include "Editor.h"
#include "EditorViewportClient.h"
#include "PatchEdMode.h"
#include "PatchOperations.h"
#include "SceneView.h"
#include "Settings/LevelEditorViewportSettings.h"

using Cmd = EPatchCommand;

void FPatchEdMode::UpdatePlacement(const FSceneView *View, FEditorViewportClient *Client, FIntPoint Pixel)
{
    const FViewportCursorLocation Ray(View, Client, Pixel.X, Pixel.Y);
    const FVector Origin = Ray.GetOrigin(), Dir = Ray.GetDirection();
    const bool Flat = Tool == Cmd::Terrain || Tool == Cmd::Curve;
    if (PlaceStage == 0 || Tool == Cmd::Rope)
    {
        FHitResult Hit;
        FVector At;
        if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Dir * 1.e7, ECC_Visibility))
            At = Hit.ImpactPoint;
        else
        {
            const FVector Normal = Client->IsOrtho() ? Dir : FVector::UpVector;
            const double Dot = Dir.Dot(Normal);
            const double T = FMath::Abs(Dot) > 1.e-5 ? (GEditor->ClickLocation - Origin).Dot(Normal) / Dot : -1.;
            At = Origin + Dir * (T >= 0 ? T : 1000.);
        }
        if (GetDefault<ULevelEditorViewportSettings>()->GridEnabled)
            At = At.GridSnap(GEditor->GetGridSize());
        PlaceEnd = At;
        bPlaceHit = true;
        if (PlaceStage == 0)
        {
            PlaceBase = At;
            PlaceRotation =
                Flat && Client->IsOrtho() ? FQuat::FindBetweenNormals(FVector::UpVector, -Dir) : FQuat::Identity;
        }
        return;
    }
    if (PlaceStage == 1)
    {
        const FVector N = PlaceRotation.GetUpVector();
        const double Dot = Dir.Dot(N);
        if (FMath::Abs(Dot) < 1.e-5)
            return;
        const FVector Local =
            PlaceRotation.UnrotateVector(Origin + Dir * ((PlaceBase - Origin).Dot(N) / Dot) - PlaceBase);
        if (Local.Size2D() < 1.)
            return;
        if (Flat)
        {
            Options->Size.X = FMath::Max(1., 2 * FMath::Abs(Local.X));
            Options->Size.Y = FMath::Max(1., 2 * FMath::Abs(Local.Y));
        }
        else
            Options->Size.X = Options->Size.Y = 2 * Local.Size2D();
    }
    else
    {
        const FVector Up = PlaceRotation.GetUpVector();
        const double Cross = Dir.Dot(Up), Denom = 1. - Cross * Cross;
        if (Denom > .001)
        {
            const FVector D = Origin - PlaceBase;
            Options->Size.Z = FMath::Max(1., (D.Dot(Up) - Cross * D.Dot(Dir)) / Denom);
        }
    }
}

bool FPatchEdMode::BuildPlacement(FPatchGrid &Grid, FTransform &Transform) const
{
    auto Shape = Primitive(Tool);
    if (!Shape || !bPlaceHit)
        return false;
    FVector Size = Options->Size.GetAbs().ComponentMax(FVector(1));
    FQuat Rotation = PlaceRotation;
    FVector Center = PlaceBase;
    if (Tool == Cmd::Rope)
    {
        const FVector End = PlaceStage == 0 ? PlaceBase + FVector(Size.X, 0, 0) : PlaceEnd;
        const FVector Span = End - PlaceBase;
        if (Span.Length() < 1.)
            return false;
        Size.X = Span.Length();
        Size.Y = 2 * Options->RopeRadius;
        Center = (End + PlaceBase) * .5;
        Rotation = Span.Rotation().Quaternion();
    }
    else if (!NumericInput.IsEmpty())
    {
        const double Value = FMath::Max(1., FCString::Atod(*NumericInput));
        if (PlaceStage == 1)
            Size.X = Size.Y = (Tool == Cmd::Terrain || Tool == Cmd::Curve) ? Value : 2 * Value;
        if (PlaceStage == 2)
            Size.Z = Value;
    }
    Transform = FTransform(Rotation, Center);
    Grid = FPatchGrid::Make(*Shape, Options->Columns, Options->Rows, Size, Size.X * Options->RopeSagPercent / 100.,
                            Rotation.UnrotateVector(-FVector::UpVector));
    return true;
}

void FPatchEdMode::PlacementClick()
{
    if (!bPlaceHit)
        return;
    if (PlaceStage == 0)
    {
        PlaceStage = 1;
        SetStatus(Tool == Cmd::Rope
                      ? TEXT("Click the second anchor. Adjust sag in the tool panel.")
                      : (Tool == Cmd::Terrain || Tool == Cmd::Curve
                             ? TEXT("Set the footprint with the mouse, or type a width. Click or Enter to place.")
                             : TEXT("Move to set radius, or type cm. Click or Enter to set the height.")));
        return;
    }
    if (PlaceStage == 1 && Tool != Cmd::Terrain && Tool != Cmd::Curve && Tool != Cmd::Rope)
    {
        if (!NumericInput.IsEmpty())
            Options->Size.X = Options->Size.Y = 2 * FMath::Max(1., FCString::Atod(*NumericInput));
        NumericInput.Reset();
        PlaceStage = 2;
        SetStatus(TEXT("Move to set height, or type cm. Click or Enter to place."));
        return;
    }
    FPatchGrid Grid;
    FTransform Transform;
    if (!BuildPlacement(Grid, Transform))
    {
        SetStatus(TEXT("Place the second anchor away from the first."));
        return;
    }
    const FScopedTransaction Tx(FText::FromString(TEXT("Place patch primitive")));
    const TCHAR *Name = TEXT("Patch");
    for (const auto &Entry : PatchCommands)
        if (Tool == Entry.Id)
            Name = Entry.Label;
    if (auto *P = Spawn(Grid, Transform, Name))
    {
        ResetInteraction(false);
        Tool = Cmd::Select;
        SelectPatch(P);
        SetStatus(TEXT("Select a control point to edit the new patch."));
    }
}

void FPatchEdMode::PickBoundary()
{
    if (!HoverEdge.Valid())
        return;
    if (!SeamFrom.Valid())
    {
        SeamFrom = HoverEdge;
        SetStatus(TEXT("Pick a matching boundary on a second patch."));
        return;
    }
    if (HoverEdge.Patch == SeamFrom.Patch)
        return;
    const auto &A = *SeamFrom.Patch, &B = *HoverEdge.Patch;
    auto From = PatchOps::EdgeIndices(A.Grid, SeamFrom.Side), To = PatchOps::EdgeIndices(B.Grid, HoverEdge.Side);
    if (A.Grid.Kind != B.Grid.Kind || From.Num() != To.Num())
    {
        SeamTo = {};
        SetStatus(TEXT("Pick a boundary of the same patch type and point count."));
        return;
    }
    double Forward = 0, Reverse = 0;
    for (int32 I = 0; I < From.Num(); ++I)
    {
        const FVector P = A.GetActorTransform().TransformPosition(A.Grid.Points[From[I]].Position);
        Forward += FVector::DistSquared(P, B.GetActorTransform().TransformPosition(B.Grid.Points[To[I]].Position));
        Reverse += FVector::DistSquared(P, B.GetActorTransform().TransformPosition(B.Grid.Points[To.Last(I)].Position));
    }
    SeamTo = HoverEdge;
    bReverseSeam = Reverse < Forward;
    SetStatus(
        TEXT("R reverses the pairs. Enter joins the edges. Esc cancels. Curve normals show the predicted seam angle."));
}
bool FPatchEdMode::HasSeamPair() const
{
    return Tool == Cmd::ConnectEdges && SeamFrom.Valid() && SeamTo.Valid();
}
void FPatchEdMode::ReverseSeam()
{
    if (HasSeamPair())
    {
        bReverseSeam = !bReverseSeam;
        GEditor->RedrawLevelEditingViewports();
    }
}
bool FPatchEdMode::CanConfirm() const
{
    return HasSeamPair() || (Primitive(Tool) && bPlaceHit && PlaceStage > 0) ||
           (Tool == Cmd::Extrude && Active() && !bEdgeDrag);
}
void FPatchEdMode::ConfirmPreview()
{
    if (!CanConfirm())
        return;
    if (Primitive(Tool))
        PlacementClick();
    else if (Tool == Cmd::Extrude)
    {
        auto *P = Active();
        FVector Center, Dir;
        GetExtrudeHandle(*P, int32(Options->Edge), Center, Dir);
        FPatchGrid Grid = P->Grid;
        if (!PatchOps::ExtrudeBoundary(Grid, int32(Options->Edge), P->GetActorTransform(),
                                       Dir * Options->ExtrudeLength))
        {
            SetStatus(TEXT("Choose a nonzero length and a boundary with room for another row."));
            return;
        }
        const FScopedTransaction Tx(FText::FromString(TEXT("Extrude patch boundary")));
        P->Modify();
        P->Grid = MoveTemp(Grid);
        P->Changed();
        Points.Reset();
        for (int32 I : PatchOps::EdgeIndices(P->Grid, int32(Options->Edge)))
            Points.Add(I);
        Focus = *Points.CreateConstIterator();
        ReadPoint();
    }
    else
    {
        auto From = PatchOps::EdgeIndices(SeamFrom.Patch->Grid, SeamFrom.Side);
        auto To = PatchOps::EdgeIndices(SeamTo.Patch->Grid, SeamTo.Side);
        if (bReverseSeam)
            Algo::Reverse(To);
        const FScopedTransaction Tx(FText::FromString(TEXT("Connect patch boundaries")));
        if (ApplySeam(*SeamFrom.Patch, From, *SeamTo.Patch, To))
        {
            auto *P = SeamFrom.Patch.Get();
            ResetInteraction(false);
            Tool = Cmd::Select;
            SelectPatch(P);
            SetStatus(TEXT("Edges connected."));
        }
    }
    GEditor->RedrawLevelEditingViewports();
}
