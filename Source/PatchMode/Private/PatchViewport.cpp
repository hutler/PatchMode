#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "PatchEdMode.h"
#include "PatchViewPlane.h"
#include "PatchToolkit.h"
#include "SceneManagement.h"
#include "Toolkits/ToolkitManager.h"
#include "UDynamicMesh.h"
#include "UnrealClient.h"

using Cmd = EPatchCommand;
const FName FPatchEdMode::Id(TEXT("EM_PatchMode"));

struct HPatchPoint : HHitProxy
{
    DECLARE_HIT_PROXY();
    TWeakObjectPtr<APatchSurface> Patch;
    int32 Index;
    HPatchPoint(APatchSurface *P, int32 I) : HHitProxy(HPP_Foreground), Patch(P), Index(I)
    {
    }
};
IMPLEMENT_HIT_PROXY(HPatchPoint, HHitProxy);

FPatchEdMode::FPatchEdMode() : Options(NewObject<UPatchSettings>(GetTransientPackage()))
{
    SetStatus(TEXT("Create a patch or select one in the level."));
}
void FPatchEdMode::Enter()
{
    FEdMode::Enter();
    if (!Owner->GetToolkitHost().IsValid())
    {
        SelectionChanged();
        return;
    }
    auto Panel = MakeShared<FPatchToolkit>(this);
    Toolkit = Panel;
    Panel->Build();
    Panel->Init(Owner->GetToolkitHost());
    SelectionChanged();
}
void FPatchEdMode::Exit()
{
    ResetInteraction(true);
    if (Toolkit.IsValid())
        FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
    Toolkit.Reset();
    FEdMode::Exit();
}
void FPatchEdMode::SelectionChanged()
{
    auto *P = Active();
    if (P != SelectionOwner.Get())
    {
        if (bEdgeDrag || DragAxis != INDEX_NONE)
            ResetInteraction(true);
        HoverPoint = INDEX_NONE;
        HoverPatch.Reset();
        SelectionOwner = P;
        Points.Reset();
        Focus = 0;
    }
}
void FPatchEdMode::PostUndo()
{
    SelectionChanged();
    if (auto *P = Active())
    {
        Focus = FMath::Clamp(Focus, 0, P->Grid.Points.Num() - 1);
        Points.Reset();
    }
    GEditor->RedrawLevelEditingViewports();
}

void FPatchEdMode::SelectPoint(APatchSurface *P, int32 I, bool Extend)
{
    if (!P || !P->Grid.Points.IsValidIndex(I))
        return;
    if (Active() != P)
        SelectPatch(P);
    if (!Extend)
        Points.Reset();
    Focus = I;
    const auto &G = P->Grid;
    const int32 X = I % G.Width, Y = I / G.Width;
    auto Add = [&](int32 J) {
        if (Extend && Points.Contains(J))
            Points.Remove(J);
        else
            Points.Add(J);
    };
    switch (Options->Pick)
    {
    case EPatchPick::Row:
        for (int32 J = 0; J < G.Width; ++J)
            Add(G.Index(J, Y));
        break;
    case EPatchPick::Column:
        for (int32 J = 0; J < G.Height; ++J)
            Add(G.Index(X, J));
        break;
    case EPatchPick::Cell: {
        int32 A = FMath::Min(X, G.Width - 2), B = FMath::Min(Y, G.Height - 2);
        for (int32 DY = 0; DY < 2; ++DY)
            for (int32 DX = 0; DX < 2; ++DX)
                Add(G.Index(A + DX, B + DY));
        break;
    }
    case EPatchPick::Surface:
        for (int32 J = 0; J < G.Points.Num(); ++J)
            Add(J);
        break;
    default:
        Add(I);
        break;
    }
    ReadPoint();
    GEditor->RedrawLevelEditingViewports();
}
void FPatchEdMode::ReadPoint()
{
    auto *P = Active();
    if (P && P->Grid.Points.IsValidIndex(Focus))
    {
        Options->Position = P->Grid.Points[Focus].Position;
        Options->Color = P->Grid.Points[Focus].Color;
        const auto &Pt = P->Grid.Points[Focus];
        Options->UVValue = Options->UVLayer == EPatchUVLayer::Texture
                               ? Pt.UV
                               : (Options->UVLayer == EPatchUVLayer::Lightmap ? Pt.LightUV : Pt.SmoothingUV);
    }
}
bool FPatchEdMode::BoxSelect(FBox &Box, bool Select)
{
    auto *P = Active();
    if (!P || Tool != Cmd::Select)
        return false;
    for (int32 I = 0; I < P->Grid.Points.Num(); ++I)
        if (Box.IsInsideOrOn(P->GetActorTransform().TransformPosition(P->Grid.Points[I].Position)))
        {
            if (Select)
            {
                Points.Add(I);
                Focus = I;
            }
            else
                Points.Remove(I);
        }
    return true;
}

void FPatchEdMode::Render(const FSceneView *View, FViewport *VP, FPrimitiveDrawInterface *PDI)
{
    FEdMode::Render(View, VP, PDI);
    const FLinearColor Cyan(.02f, .77f, .85f), Amber(1, .48f, .02f);
    for (auto *P : SelectedPatches())
    {
        const auto &G = P->Grid;
        if (!G.Valid())
            continue;
        const auto Transform = P->GetActorTransform();
        auto Pos = [&](int32 X, int32 Y) { return Transform.TransformPosition(G.At(X, Y).Position); };
        if (Options->bShowTriangles)
        {
            TArray<FPatchPoint> V;
            TArray<FIntVector> Tris;
            G.Tessellate(V, Tris);
            for (auto T : Tris)
                for (int32 K = 0; K < 3; ++K)
                    PDI->DrawLine(Transform.TransformPosition(V[T[K]].Position),
                                  Transform.TransformPosition(V[T[(K + 1) % 3]].Position), FLinearColor(.15f, .4f, .4f),
                                  SDPG_World);
        }
        if (!Options->bShowGrid)
            continue;
        for (int32 Y = 0; Y < G.Height; ++Y)
            for (int32 X = 0; X < G.Width; ++X)
            {
                const int32 I = G.Index(X, Y);
                const FVector V = Pos(X, Y);
                if (X + 1 < G.Width)
                    PDI->DrawLine(V, Pos(X + 1, Y), Cyan, SDPG_Foreground, .9f);
                if (Y + 1 < G.Height)
                    PDI->DrawLine(V, Pos(X, Y + 1), Cyan, SDPG_Foreground, .9f);
                if (P == Active() && I == Focus && G.Kind == EPatchKind::Terrain && X + 1 < G.Width && Y + 1 < G.Height)
                {
                    const bool Turn = G.Turned[G.Cell(X, Y)] != 0;
                    const FVector Corners[] = {V, Pos(X + 1, Y), Pos(X + 1, Y + 1), Pos(X, Y + 1)};
                    for (int32 J = 0; J < 4; ++J)
                        PDI->DrawLine(Corners[J], Corners[(J + 1) % 4], Amber, SDPG_Foreground, 1.5f);
                    PDI->DrawLine(Turn ? Pos(X + 1, Y) : V, Turn ? Pos(X, Y + 1) : Pos(X + 1, Y + 1), Amber,
                                  SDPG_Foreground, 2.5f);
                }
                if (PDI->IsHitTesting())
                    PDI->SetHitProxy(new HPatchPoint(P, I));
                const FLinearColor Col =
                    G.Points[I].bLocked ? FLinearColor::Gray : ((P == Active() && Points.Contains(I)) ? Amber : Cyan);
                const bool Hovered = Tool == Cmd::Select && HoverPatch.Get() == P && HoverPoint == I;
                PDI->DrawPoint(V, Hovered ? FLinearColor::White : Col, Hovered ? 14 : 9,
                               Options->bDrillDown ? SDPG_Foreground : SDPG_World);
                if (PDI->IsHitTesting())
                    PDI->SetHitProxy(nullptr);
            }
    }
    if (Options->bTolerantSnap && Active() && Active()->Grid.Points.IsValidIndex(Focus))
    {
        auto *Source = Active();
        const FVector From = Source->GetActorTransform().TransformPosition(Source->Grid.Points[Focus].Position);
        for (TActorIterator<APatchSurface> It(GetWorld()); It; ++It)
        {
            if (*It == Source)
                continue;
            for (const auto &Point : It->Grid.Points)
            {
                const FVector Candidate = It->GetActorTransform().TransformPosition(Point.Position);
                if (FVector::DistSquared(From, Candidate) <= FMath::Square(Options->WeldTolerance))
                    PDI->DrawLine(From, Candidate, FLinearColor::Green, SDPG_Foreground, 2);
            }
        }
    }
    RenderInteraction(View, PDI);
    if (Cursor.IsSet() && (Tool == Cmd::Paint || Tool == Cmd::Sculpt))
        RenderBrush(PDI);
    if (bDrawing && Cursor.IsSet())
    {
        const FVector Delta = *Cursor - DrawOrigin;
        const FVector Along = Delta.ProjectOnToNormal(DrawRotation.GetAxisX());
        const FVector Across = Delta - Along;
        const FVector Corners[] = {DrawOrigin, DrawOrigin + Along, *Cursor, DrawOrigin + Across};
        for (int32 I = 0; I < 4; ++I)
            PDI->DrawLine(Corners[I], Corners[(I + 1) % 4], Amber, SDPG_Foreground, 2);
    }
}

static bool HitSurface(APatchSurface &P, const FVector &Start, const FVector &Dir, FVector &World, int32 &CellX,
                       int32 &CellY)
{
    const auto Transform = P.GetActorTransform();
    const FVector A = Transform.InverseTransformPosition(Start),
                  B = Transform.InverseTransformPosition(Start + Dir * 1e8);
    const auto *Mesh = P.Preview->GetDynamicMesh()->GetMeshPtr();
    if (!Mesh)
        return false;
    double Best = TNumericLimits<double>::Max();
    const bool Curve = P.Grid.Kind == EPatchKind::Curve;
    const int32 Steps = Curve ? FMath::Clamp(P.Grid.Subdivisions, 1, 32) : 1;
    const int32 W = Curve ? (P.Grid.Width - 1) / 2 * Steps + 1 : P.Grid.Width;
    for (const int32 TriangleId : Mesh->TriangleIndicesItr())
    {
        const auto Face = Mesh->GetTriangle(TriangleId);
        const FIntVector Tri(Face.A, Face.B, Face.C);
        FVector Hit, Normal;
        if (!FMath::SegmentTriangleIntersection(A, B, Mesh->GetVertex(Face.A), Mesh->GetVertex(Face.B),
                                                Mesh->GetVertex(Face.C), Hit, Normal))
            continue;
        const double Dist = FVector::DistSquared(A, Hit);
        if (Dist >= Best)
            continue;
        Best = Dist;
        World = Transform.TransformPosition(Hit);
        const int32 X = FMath::Min3(Tri.X % W, Tri.Y % W, Tri.Z % W), Y = FMath::Min3(Tri.X / W, Tri.Y / W, Tri.Z / W);
        CellX = Curve ? X / Steps * 2 : X;
        CellY = Curve ? Y / Steps * 2 : Y;
    }
    return Best < TNumericLimits<double>::Max();
}

bool FPatchEdMode::HandleClick(FEditorViewportClient *Client, HHitProxy *Proxy, const FViewportClick &Click)
{
    if (Click.GetKey() != EKeys::LeftMouseButton || Click.IsAltDown())
        return false;
    if (bConsumeClick || Tool == Cmd::Extrude || Tool == Cmd::ConnectEdges || Primitive(Tool))
        return true;
    if (Tool == Cmd::Faces)
    {
        if (const auto Ray = PatchCursorRay(*Client, Click.GetClickPos()))
            FaceToTerrain(*Ray);
        return true;
    }
    if (Tool == Cmd::DrawTerrain || Tool == Cmd::Paint || Tool == Cmd::Sculpt)
        return true;
    if (Tool == Cmd::Select && Proxy && Proxy->IsA(HPatchPoint::StaticGetType()))
    {
        auto *Hit = static_cast<HPatchPoint *>(Proxy);
        SelectPoint(Hit->Patch.Get(), Hit->Index, Click.IsControlDown() || Click.IsShiftDown());
        return true;
    }
    if (Tool == Cmd::Select && Proxy && Proxy->IsA(HActor::StaticGetType()) && !Click.IsControlDown())
    {
        AActor *HitActor = static_cast<HActor *>(Proxy)->Actor;
        for (TActorIterator<APatchSurface> It(GetWorld()); It; ++It)
            if (It->BakedActor.Get() == HitActor && HitActor)
            {
                SelectPatch(*It);
                return true;
            }
    }
    auto *P = Active();
    if (P && Tool == Cmd::TurnEdge)
    {
        int32 X, Y;
        FVector World;
        const auto Ray = PatchCursorRay(*Client, Click.GetClickPos());
        if (Ray && P->Grid.Kind == EPatchKind::Terrain && HitSurface(*P, Ray->Origin, Ray->Direction, World, X, Y))
        {
            const FScopedTransaction Tx(FText::FromString(TEXT("Turn terrain edge")));
            P->Modify();
            P->Grid.Turned[P->Grid.Cell(X, Y)] ^= 1;
            Focus = P->Grid.Index(X, Y);
            P->Changed();
            Client->Invalidate();
            return true;
        }
    }
    return false;
}

bool FPatchEdMode::UsesTransformWidget() const
{
    return Tool == Cmd::Select && (Points.IsEmpty() || !Owner || Owner->GetWidgetMode() != UE::Widget::WM_Translate);
}
FVector FPatchEdMode::GetWidgetLocation() const
{
    auto *P = Active();
    FVector Sum = FVector::ZeroVector;
    int32 Count = 0;
    if (P)
        for (int32 I : Points)
            if (P->Grid.Points.IsValidIndex(I))
            {
                Sum += P->GetActorTransform().TransformPosition(P->Grid.Points[I].Position);
                ++Count;
            }
    return Count ? Sum / Count : FEdMode::GetWidgetLocation();
}
void FPatchEdMode::Touch(APatchSurface *P)
{
    if (!Touched.Contains(P))
    {
        Touched.Add(P, {P->Grid, P->bNeedsBake});
        P->Modify();
        P->bNeedsBake = true;
        P->Preview->SetDeferredCollisionUpdatesEnabled(true, false);
    }
}
void FPatchEdMode::FinishGesture(bool bCancel)
{
    for (auto &Entry : Touched)
        if (auto *P = Entry.Key.Get())
        {
            if (bCancel)
            {
                P->Grid = MoveTemp(Entry.Value.Key);
                P->bNeedsBake = Entry.Value.Value;
                P->Rebuild();
            }
            else
                P->Changed();
            P->Preview->SetDeferredCollisionUpdatesEnabled(false, true);
        }
    if (bCancel && Gesture)
        Gesture->Cancel();
    Touched.Reset();
    Gesture.Reset();
    bPainting = false;
    bDrawing = false;
}
bool FPatchEdMode::LostFocus(FEditorViewportClient *Client, FViewport *VP)
{
    if (DragAxis != INDEX_NONE || bEdgeDrag)
        ResetInteraction(true);
    else
        FinishGesture();
    HoverAxis = HoverPoint = INDEX_NONE;
    HoverEdge = {};
    HoverPatch.Reset();
    Cursor.Reset();
    return FEdMode::LostFocus(Client, VP);
}
bool FPatchEdMode::StartTracking(FEditorViewportClient *Client, FViewport *VP)
{
    if (bConsumeClick || bEdgeDrag || DragAxis != INDEX_NONE)
        return true;
    if (Tool != Cmd::Select || !Active() || Points.IsEmpty() || Client->GetCurrentWidgetAxis() == EAxisList::None)
        return false;
    if (!Gesture)
        Gesture = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Transform patch points")));
    return true;
}
bool FPatchEdMode::EndTracking(FEditorViewportClient *Client, FViewport *VP)
{
    if (DragAxis != INDEX_NONE || bEdgeDrag || bPointPressed)
        return true;
    if (Gesture && !bDrawing && !bPainting)
    {
        if (Options->bTolerantSnap && !Touched.IsEmpty() && Active())
            Join(Cmd::TolerantWeld, *Active());
        FinishGesture();
        return true;
    }
    return false;
}
bool FPatchEdMode::InputDelta(FEditorViewportClient *Client, FViewport *, FVector &Drag, FRotator &Rot, FVector &Scale)
{
    if (DragAxis != INDEX_NONE || bEdgeDrag || bPointPressed)
        return true;
    auto *P = Active();
    if (!P || !Gesture || Tool != Cmd::Select || Points.IsEmpty() || Client->GetCurrentWidgetAxis() == EAxisList::None)
        return false;
    TransformPoints(Drag, Rot, Scale);
    return true;
}
void FPatchEdMode::TransformPoints(const FVector &Drag, const FRotator &Rot, const FVector &Scale)
{
    auto *P = Active();
    if (!P)
        return;
    const FVector Pivot = GetWidgetLocation();
    TArray<FVector> From;
    for (int32 I : Points)
        if (P->Grid.Points.IsValidIndex(I) && !P->Grid.Points[I].bLocked)
            From.Add(P->GetActorTransform().TransformPosition(P->Grid.Points[I].Position));
    for (TActorIterator<APatchSurface> It(GetWorld()); It; ++It)
    {
        APatchSurface *Target = *It;
        if (Target != P && !Options->bMoveCoincident)
            continue;
        bool Dirty = false;
        for (int32 I = 0; I < Target->Grid.Points.Num(); ++I)
        {
            auto &Pt = Target->Grid.Points[I];
            if (Pt.bLocked)
                continue;
            const auto Xf = Target->GetActorTransform();
            const FVector World = Xf.TransformPosition(Pt.Position);
            double Weight = Target == P && Points.Contains(I) ? 1. : 0.;
            if (Weight == 0. && (Options->bMoveCoincident || (Options->bSoftSelection && Target == P)))
            {
                double D = TNumericLimits<double>::Max();
                for (const FVector &Q : From)
                    D = FMath::Min(D, FVector::Distance(Q, World));
                if (Options->bMoveCoincident && D <= .001)
                    Weight = 1.;
                else if (Options->bSoftSelection && D < Options->Radius)
                    Weight = FMath::Pow(1. - D / Options->Radius, Options->Falloff);
            }
            if (Weight <= 0.)
                continue;
            Touch(Target);
            Dirty = true;
            Pt.Position = Xf.InverseTransformPosition(FMath::Lerp(
                World, Pivot + Drag + Rot.RotateVector((World - Pivot) * (FVector::OneVector + Scale)), Weight));
        }
        if (Dirty)
            Target->Rebuild();
    }
}

bool FPatchEdMode::BrushAt(FEditorViewportClient *Client, FViewport *VP, double Drag)
{
    auto *P = Active();
    if (!P)
        return false;
    const auto Ray = PatchCursorRay(*Client, FIntPoint(VP->GetMouseX(), VP->GetMouseY()));
    return Ray && BrushRay(Ray->Origin, Ray->Direction, Drag);
}
void FPatchEdMode::BeginBrushStroke()
{
    if (!Gesture)
        Gesture = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Patch brush stroke")));
    bPainting = true;
}
void FPatchEdMode::EndBrushStroke()
{
    FinishGesture();
}
bool FPatchEdMode::BrushRay(FVector Origin, FVector Direction, double Drag)
{
    const auto Patches = SelectedPatches();
    FVector World = FVector::ZeroVector;
    APatchSurface *Nearest = nullptr;
    double Distance = TNumericLimits<double>::Max();
    for (auto *Target : Patches)
    {
        FVector Hit;
        int32 X, Y;
        if (!HitSurface(*Target, Origin, Direction, Hit, X, Y))
            continue;
        const double D = FVector::DistSquared(Origin, Hit);
        if (D < Distance)
        {
            Nearest = Target;
            World = Hit;
            Distance = D;
        }
    }
    if (!Nearest)
    {
        Cursor.Reset();
        return false;
    }
    Cursor = World;
    if (!bPainting)
        return true;
    if (Options->Brush == EPatchBrush::Grab)
        return ApplyStamp(Nearest, World, Drag);
    for (auto *Target : Patches)
        if (!Options->bOnlySelected || Target == Active())
            ApplyStamp(Target, World, Drag);
    return true;
}
bool FPatchEdMode::ApplyStamp(APatchSurface *P, FVector World, double Drag)
{
    auto &G = P->Grid;
    auto &S = *Options;
    const auto Xf = P->GetActorTransform();
    const auto Initial = G.Points;
    const FVector Local = Xf.InverseTransformPosition(World);
    if (S.Brush == EPatchBrush::Grab)
    {
        S.Height = Local.Z;
        int32 Closest = 0;
        double Best = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Initial.Num(); ++I)
        {
            const double D = FVector::DistSquared(Initial[I].Position, Local);
            if (D < Best)
            {
                Best = D;
                Closest = I;
            }
        }
        S.Color = Initial[Closest].Color;
        return true;
    }
    bool Dirty = false;
    for (int32 I = 0; I < Initial.Num(); ++I)
    {
        const auto &Old = Initial[I];
        auto &Pt = G.Points[I];
        if (Pt.bLocked || (S.bOnlySelected && !Points.Contains(I)))
            continue;
        const double D = FVector::Distance(Xf.TransformPosition(Old.Position), World);
        const double K = BrushWeight(D);
        if (K <= 0.)
            continue;
        Touch(P);
        Dirty = true;
        switch (S.Brush)
        {
        case EPatchBrush::Height:
            Pt.Position.Z += S.Height * K;
            break;
        case EPatchBrush::Flatten:
            Pt.Position.Z = FMath::Lerp(Old.Position.Z, S.Height, K);
            break;
        case EPatchBrush::Drag:
            Pt.Position.Z += Drag * K;
            break;
        case EPatchBrush::Noise:
            Pt.Position.Z +=
                S.Height * K * FMath::PerlinNoise3D(FVector(Old.Position * .035) + FVector(S.NoiseSeed * .071));
            break;
        case EPatchBrush::PaintAlpha:
            Pt.Color.A = FMath::Lerp(Old.Color.A, S.Color.A, float(K));
            break;
        case EPatchBrush::PaintRGB:
            if (S.bRed)
                Pt.Color.R = FMath::Lerp(Old.Color.R, S.Color.R, float(K));
            if (S.bGreen)
                Pt.Color.G = FMath::Lerp(Old.Color.G, S.Color.G, float(K));
            if (S.bBlue)
                Pt.Color.B = FMath::Lerp(Old.Color.B, S.Color.B, float(K));
            break;
        case EPatchBrush::Smooth: {
            const int32 Col = I % G.Width, Row = I / G.Width;
            double Sum = 0;
            int32 N = 0;
            for (FIntPoint Offset : {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)})
            {
                const int32 A = Col + Offset.X, B = Row + Offset.Y;
                if (A >= 0 && A < G.Width && B >= 0 && B < G.Height)
                {
                    Sum += Initial[G.Index(A, B)].Position.Z;
                    ++N;
                }
            }
            if (N)
                Pt.Position.Z = FMath::Lerp(Old.Position.Z, Sum / N, K);
            break;
        }
        default:
            break;
        }
    }
    if (Dirty)
        P->Rebuild();
    return true;
}

static FVector PlanePoint(FEditorViewportClient *Client, const FVector &Origin, const FVector &Normal)
{
    const auto Ray = Client->GetCursorWorldLocationFromMousePos();
    const double Dot = FVector::DotProduct(Ray.GetDirection(), Normal);
    if (FMath::Abs(Dot) < UE_SMALL_NUMBER)
        return Origin;
    return Ray.GetOrigin() + Ray.GetDirection() * (FVector::DotProduct(Origin - Ray.GetOrigin(), Normal) / Dot);
}
bool FPatchEdMode::MouseMove(FEditorViewportClient *Client, FViewport *VP, int32 X, int32 Y)
{
    if (DragAxis != INDEX_NONE || bEdgeDrag)
    {
        MoveHandle(Client, VP, X, Y);
        return true;
    }
    if (Tool == Cmd::Select || Tool == Cmd::Extrude || Tool == Cmd::ConnectEdges || Primitive(Tool))
    {
        const auto OldPoint = HoverPoint, OldAxis = HoverAxis, OldSide = HoverEdge.Side;
        const auto OldPatch = HoverPatch, OldEdgePatch = HoverEdge.Patch;
        UpdateHover(Client, VP, X, Y);
        if (Primitive(Tool) || OldPoint != HoverPoint || OldAxis != HoverAxis || OldSide != HoverEdge.Side ||
            OldPatch != HoverPatch || OldEdgePatch != HoverEdge.Patch)
            Client->Invalidate();
        return bPointPressed || Tool != Cmd::Select;
    }
    if (bDrawing)
    {
        Cursor = PlanePoint(Client, DrawOrigin, DrawRotation.GetUpVector());
        Client->Invalidate();
        return true;
    }
    if (Tool != Cmd::Sculpt && Tool != Cmd::Paint)
        return false;
    const bool Hit = BrushAt(Client, VP, LastMouseY - Y);
    LastMouseY = Y;
    Client->Invalidate();
    return Hit;
}
bool FPatchEdMode::CapturedMouseMove(FEditorViewportClient *C, FViewport *V, int32 X, int32 Y)
{
    return MouseMove(C, V, X, Y);
}

bool FPatchEdMode::InputKey(FEditorViewportClient *Client, FViewport *VP, FKey Key, EInputEvent Event)
{
    if (VP->KeyState(EKeys::LeftAlt) || VP->KeyState(EKeys::RightAlt))
    {
        if (Options->bRadiantKeys && Key == EKeys::O && Event == IE_Pressed)
        {
            ActivateTool(Cmd::Extrude);
            return true;
        }
        if (Event != IE_Released || (!bPainting && !bDrawing && DragAxis == INDEX_NONE && !bEdgeDrag && !bPointPressed))
            return false;
    }
    if (Key == EKeys::LeftMouseButton && PointerKey(Client, VP, Event))
        return true;
    if (Event == IE_Pressed && (Primitive(Tool) || Tool == Cmd::ConnectEdges || Tool == Cmd::Extrude))
    {
        if (Key == EKeys::Enter)
        {
            ConfirmPreview();
            Client->Invalidate();
            return true;
        }
        if (Key == EKeys::R && Tool == Cmd::ConnectEdges)
        {
            ReverseSeam();
            return true;
        }
        if (Primitive(Tool) && PlaceStage > 0 && Tool != Cmd::Rope)
        {
            FString Name = Key.GetFName().ToString();
            const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two,   EKeys::Three, EKeys::Four,
                                   EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
            for (int32 I = 0; I < 10; ++I)
                if (Key == Digits[I] || Name == FString::Printf(TEXT("NumPad%d"), I))
                    Name = FString::FromInt(I);
            if (Key == EKeys::Period || Key == EKeys::Decimal)
                Name = TEXT(".");
            if (Key == EKeys::BackSpace)
            {
                NumericInput.LeftChopInline(FMath::Min(1, NumericInput.Len()));
                return true;
            }
            if (Name.Len() == 1 && (FChar::IsDigit(Name[0]) || (Name[0] == '.' && !NumericInput.Contains(TEXT(".")))))
            {
                if (NumericInput.Len() < 8)
                    NumericInput += Name;
                Client->Invalidate();
                return true;
            }
        }
    }
    if (Key == EKeys::LeftMouseButton && (Tool == Cmd::Paint || Tool == Cmd::Sculpt))
    {
        if (Event == IE_Pressed)
        {
            if (!Active())
                return false;
            BeginBrushStroke();
            LastMouseY = VP->GetMouseY();
            BrushAt(Client, VP, 0);
            Client->Invalidate();
        }
        if (Event == IE_Released)
            EndBrushStroke();
        return true;
    }
    if (Key == EKeys::LeftMouseButton && Tool == Cmd::DrawTerrain)
    {
        if (Event == IE_Pressed)
        {
            DrawRotation = PatchPlaneRotation(*Client);
            DrawOrigin = PlanePoint(Client, GEditor->ClickLocation, DrawRotation.GetUpVector());
            Cursor = DrawOrigin;
            bDrawing = true;
        }
        if (Event == IE_Released && bDrawing)
        {
            const FVector End = PlanePoint(Client, DrawOrigin, DrawRotation.GetUpVector());
            const FVector Delta = DrawRotation.UnrotateVector(End - DrawOrigin);
            const double W = Delta.X, H = Delta.Y;
            if (FMath::Abs(W) > 1 && FMath::Abs(H) > 1)
            {
                const FScopedTransaction Tx(FText::FromString(TEXT("Draw terrain patch")));
                auto G = FPatchGrid::Make(EPatchPrimitive::Terrain, 2, 2, FVector(1), 0);
                for (auto &Pt : G.Points)
                    Pt.Position = FVector(Pt.UV.X * W, Pt.UV.Y * H, 0);
                G.bInverted = W * H < 0;
                if (auto *P = Spawn(G, FTransform(DrawRotation, DrawOrigin), TEXT("Terrain")))
                    SelectPatch(P);
            }
            bDrawing = false;
            Cursor.Reset();
            Client->Invalidate();
        }
        return true;
    }
    if (Event != IE_Pressed)
        return false;
    if (Key == EKeys::Escape)
    {
        ResetInteraction(true);
        Tool = Cmd::Select;
        SetStatus(TEXT("Select a control point, then drag a colored axis."));
        Client->Invalidate();
        return true;
    }
    if (!Options->bRadiantKeys)
        return false;
    const bool Shift = VP->KeyState(EKeys::LeftShift) || VP->KeyState(EKeys::RightShift);
    const bool Ctrl = VP->KeyState(EKeys::LeftControl) || VP->KeyState(EKeys::RightControl);
    if (Key == EKeys::W && !Ctrl)
    {
        if (Shift)
        {
            Options->bShowTriangles = !Options->bShowTriangles;
            Client->Invalidate();
        }
        else
            Execute(Cmd::WeldPoints);
        return true;
    }
    if (Key == EKeys::Tab && Active())
    {
        const int32 N = Active()->Grid.Points.Num();
        SelectPoint(Active(), (Focus + (Shift ? N - 1 : 1)) % N, false);
        return true;
    }
    if (Key == EKeys::Q && Shift)
    {
        Execute(Ctrl ? Cmd::Remove : Cmd::DrawTerrain);
        return true;
    }
    if (Key == EKeys::I && (Ctrl || Shift))
    {
        Execute(Ctrl ? (Shift ? Cmd::FlipU : Cmd::Invert) : Cmd::FlipV);
        return true;
    }
    if (Key == EKeys::X && Ctrl && Shift)
    {
        Execute(Cmd::Split);
        return true;
    }
    if (Key == EKeys::J && Ctrl && Shift)
    {
        Execute(Cmd::TolerantWeld);
        return true;
    }
    if (Key == EKeys::T && Ctrl && Shift)
    {
        Execute(Cmd::Thicken);
        return true;
    }
    if (Key == EKeys::N && Ctrl)
    {
        Execute(Shift ? Cmd::CycleProjection : Cmd::Natural);
        return true;
    }
    if (Key == EKeys::A && Ctrl && Shift)
    {
        Execute(Cmd::Insert);
        return true;
    }
    if (Key == EKeys::C && Shift)
    {
        Execute(Cmd::Cap);
        return true;
    }
    if (Key == EKeys::Period && Ctrl)
    {
        Execute(Shift ? Cmd::Unlock : Cmd::Lock);
        return true;
    }
    if (Key == EKeys::M && Ctrl && Shift)
    {
        Execute(Cmd::Transpose);
        return true;
    }
    if (Key == EKeys::L && Shift)
    {
        Execute(Cmd::CycleUVLayer);
        return true;
    }
    if (Key == EKeys::F && Shift)
    {
        Execute(Cmd::RedistributeAll);
        return true;
    }
    if (Key == EKeys::E && Shift)
    {
        Execute(Ctrl ? Cmd::RedistributeColumns : Cmd::RedistributeRows);
        return true;
    }
    if (Key == EKeys::S && !Ctrl && !Shift)
    {
        Execute(Cmd::InspectUV);
        return true;
    }
    if (Key == EKeys::P && Ctrl && Shift)
    {
        Execute(Cmd::CapProjection);
        return true;
    }
    if (Key == EKeys::G && !Ctrl && !Shift)
    {
        Execute(Cmd::ReadPoint);
        return true;
    }
    if (Key == EKeys::Y && !Ctrl && !Shift)
    {
        Execute(Cmd::Sculpt);
        return true;
    }
    if (Key == EKeys::LeftBracket && Shift)
    {
        Execute(Cmd::DecreaseSubdivision);
        return true;
    }
    if (Key == EKeys::RightBracket && Shift)
    {
        Execute(Cmd::IncreaseSubdivision);
        return true;
    }
    return false;
}
