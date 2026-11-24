#include "CanvasItem.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "PatchEdMode.h"
#include "PatchOperations.h"
#include "PatchToolkit.h"
#include "PatchViewPlane.h"
#include "SceneManagement.h"
#include "Settings/LevelEditorViewportSettings.h"

using Cmd = EPatchCommand;

namespace
{
FVector WorldPoint(const APatchSurface &P, int32 I)
{
    return P.GetActorTransform().TransformPosition(P.Grid.Points[I].Position);
}

double PixelScale(const FSceneView &View, FVector At)
{
    const double Width = View.UnscaledViewRect.Width();
    return 2. * FMath::Abs(View.WorldToScreen(At).W) /
           FMath::Max(.001, Width * View.ViewMatrices.GetViewToClip().M[0][0]);
}

FVector PointAxis(FEditorViewportClient *Client, int32 Axis)
{
    return Client->GetWidgetCoordSystem().GetScaledAxis(EAxis::Type(Axis + 1)).GetSafeNormal();
}

void EdgeHandle(const APatchSurface &P, int32 Side, EPatchExtrudeDirection Mode, FVector &Center, FVector &Dir)
{
    const auto Edge = PatchOps::EdgeIndices(P.Grid, Side);
    const int32 Inside[] = {P.Grid.Width, -1, -P.Grid.Width, 1};
    Center = Dir = FVector::ZeroVector;
    for (int32 I : Edge)
    {
        Center += WorldPoint(P, I);
        Dir += WorldPoint(P, I) - WorldPoint(P, I + Inside[Side]);
    }
    Center /= Edge.Num();
    Dir = Dir.GetSafeNormal();
    if (Mode != EPatchExtrudeDirection::Outward)
    {
        Dir = FVector::ZeroVector;
        Dir[int32(Mode) - 1] = 1;
    }
}

void DrawArrow(FPrimitiveDrawInterface *PDI, const FSceneView &View, FVector Origin, FVector Direction,
               FLinearColor Color, bool Hot)
{
    const FVector End = Origin + Direction * (PixelScale(View, Origin) * 62.);
    PDI->DrawLine(Origin, End, Color, SDPG_Foreground, Hot ? 4.f : 2.f, 0, true);
    PDI->DrawPoint(End, Color, Hot ? 12 : 9, SDPG_Foreground);
}

bool ScreenAxis(const FSceneView &View, FVector Center, FVector Dir, FVector2D &Origin, FVector2D &Vector)
{
    FVector2D End;
    const double Scale = PixelScale(View, Center);
    if (!View.WorldToPixel(Center, Origin) || !View.WorldToPixel(Center + Dir * Scale * 62., End))
        return false;
    Vector = End - Origin;
    return Vector.Length() >= 8;
}
} // namespace

void FPatchEdMode::GetExtrudeHandle(const APatchSurface &P, int32 Side, FVector &Center, FVector &Dir) const
{
    EdgeHandle(P, Side, Options->ExtrudeDirection, Center, Dir);
}

void FPatchEdMode::UpdateHover(FEditorViewportClient *Client, FViewport *VP, int32 X, int32 Y)
{
    HoverPoint = HoverAxis = INDEX_NONE;
    HoverPatch.Reset();
    HoverEdge = {};
    if (!VP || !Client ||
        (Tool != Cmd::Select && Tool != Cmd::Extrude && Tool != Cmd::ConnectEdges && !Primitive(Tool)))
        return;
    FSceneViewFamilyContext Family(
        FSceneViewFamily::ConstructionValues(VP, Client->GetScene(), Client->EngineShowFlags));
    auto *View = Client->CalcSceneView(&Family);
    if (!View)
        return;
    const FVector2D Mouse(X, Y);
    if (Primitive(Tool))
    {
        UpdatePlacement(View, Client, FIntPoint(X, Y));
        return;
    }
    auto *P = Active();
    if (Tool == Cmd::Select && !P)
        return;
    double BestAxis = 8;
    auto ClosestAxis = [&](FVector Center, FVector Dir) {
        FVector2D Start, Projected;
        if (!ScreenAxis(*View, Center, Dir, Start, Projected))
            return false;
        const FVector2D Closest =
            FMath::ClosestPointOnSegment2D(Mouse, Start + Projected.GetSafeNormal() * 11., Start + Projected);
        const double Dist = FVector2D::Distance(Mouse, Closest);
        if (Dist >= BestAxis || (Mouse - Start).Length() < 9.)
            return false;
        BestAxis = Dist;
        HandleOrigin = Center;
        HandleDirection = Dir;
        DragScreenAxis = Projected / (62. * PixelScale(*View, Center));
        return true;
    };
    if (Tool == Cmd::Select)
    {
        if (!Points.IsEmpty() && Client->GetWidgetMode() == UE::Widget::WM_Translate)
            for (int32 I = 0; I < 3; ++I)
                if (ClosestAxis(GetWidgetLocation(), PointAxis(Client, I)))
                    HoverAxis = I;
        if (HoverAxis != INDEX_NONE || !Options->bShowGrid)
            return;
        double Best = 10., Depth = DBL_MAX;
        for (auto *Patch : SelectedPatches())
            for (int32 I = 0; I < Patch->Grid.Points.Num(); ++I)
            {
                const FVector At = WorldPoint(*Patch, I);
                FVector2D Pixel;
                if (!View->WorldToPixel(At, Pixel))
                    continue;
                const double Dist = (Mouse - Pixel).Length();
                const double Z = (At - View->ViewMatrices.GetViewOrigin()).Dot(View->GetViewDirection());
                if (Dist > 10 || (Dist > Best + .1 || (FMath::Abs(Best - Dist) < .1 && Z >= Depth)))
                    continue;
                if (!Options->bDrillDown)
                {
                    const FRay Ray = PatchCursorRay(*View, *Client, FIntPoint(FMath::RoundToInt(Pixel.X), FMath::RoundToInt(Pixel.Y)));
                    FHitResult Hit;
                    if (GetWorld()->LineTraceSingleByChannel(Hit, Ray.Origin, At, ECC_Visibility) &&
                        FVector::Distance(Hit.ImpactPoint, At) > FMath::Max(1., PixelScale(*View, At) * 3.))
                        continue;
                }
                Best = Dist;
                Depth = Z;
                HoverPoint = I;
                HoverPatch = Patch;
            }
        return;
    }
    double BestEdge = 13.;
    for (TActorIterator<APatchSurface> It(GetWorld()); It; ++It)
    {
        APatchSurface &Patch = **It;
        if (!Patch.Grid.Valid() || Patch.IsHiddenEd() || (Tool == Cmd::Extrude && &Patch != P))
            continue;
        if (Tool == Cmd::ConnectEdges && SeamFrom.Valid() && SeamFrom.Patch.Get() == &Patch)
            continue;
        for (int32 Side = 0; Side < 4; ++Side)
        {
            const auto Edge = PatchOps::EdgeIndices(Patch.Grid, Side);
            if (Tool == Cmd::Extrude)
            {
                FVector Center, Dir;
                EdgeHandle(Patch, Side, Options->ExtrudeDirection, Center, Dir);
                if (ClosestAxis(Center, Dir))
                {
                    HoverAxis = Side;
                    HoverEdge = {&Patch, Side};
                }
            }
            if (HoverAxis != INDEX_NONE)
                continue;
            for (int32 I = 1; I < Edge.Num(); ++I)
            {
                FVector2D A, B;
                if (!View->WorldToPixel(WorldPoint(Patch, Edge[I - 1]), A) ||
                    !View->WorldToPixel(WorldPoint(Patch, Edge[I]), B))
                    continue;
                const double Dist = (Mouse - FMath::ClosestPointOnSegment2D(Mouse, A, B)).Length();
                if (Dist < BestEdge)
                {
                    BestEdge = Dist;
                    HoverEdge = {&Patch, Side};
                }
            }
        }
    }
    if (Tool == Cmd::Extrude && HoverEdge.Valid() && HoverAxis == INDEX_NONE)
    {
        EdgeHandle(*HoverEdge.Patch, HoverEdge.Side, Options->ExtrudeDirection, HandleOrigin, HandleDirection);
        FVector2D Start, Projected;
        DragScreenAxis = ScreenAxis(*View, HandleOrigin, HandleDirection, Start, Projected)
                             ? Projected / (62. * PixelScale(*View, HandleOrigin))
                             : FVector2D(0, -1) / PixelScale(*View, HandleOrigin);
    }
}

bool FPatchEdMode::PointerKey(FEditorViewportClient *Client, FViewport *VP, EInputEvent Event)
{
    if (Event == IE_Released)
    {
        if (DragAxis != INDEX_NONE || bEdgeDrag)
        {
            MoveHandle(Client, VP, VP->GetMouseX(), VP->GetMouseY());
            if (bEdgeDrag && FMath::Abs(DragAmount) > .001 && ExtrudeEdge.Valid())
            {
                Points.Reset();
                for (int32 I : PatchOps::EdgeIndices(ExtrudeEdge.Patch->Grid, ExtrudeEdge.Side))
                    Points.Add(I);
                Focus = *Points.CreateConstIterator();
                Options->ExtrudeLength = DragAmount;
            }
            if (!bEdgeDrag && Options->bTolerantSnap && !Touched.IsEmpty() && Active())
                Join(Cmd::TolerantWeld, *Active());
            FinishGesture(FMath::Abs(DragAmount) < .001);
            if (bEdgeDrag)
                NumericInput.Reset();
            DragAxis = INDEX_NONE;
            bEdgeDrag = false;
            ReadPoint();
        }
        bPointPressed = false;
        return bConsumeClick;
    }
    if (Event != IE_Pressed && Event != IE_DoubleClick)
        return false;
    bConsumeClick = false;
    UpdateHover(Client, VP, VP->GetMouseX(), VP->GetMouseY());
    if (Primitive(Tool))
        PlacementClick();
    else if (Tool == Cmd::ConnectEdges)
        PickBoundary();
    else if ((Tool == Cmd::Select && HoverAxis != INDEX_NONE) || (Tool == Cmd::Extrude && HoverEdge.Valid()))
    {
        bEdgeDrag = Tool == Cmd::Extrude;
        if (bEdgeDrag)
        {
            ExtrudeEdge = HoverEdge;
            Options->Edge = EPatchBoundary(ExtrudeEdge.Side);
            const auto &G = ExtrudeEdge.Patch->Grid;
            if ((ExtrudeEdge.Side % 2 == 0 ? G.Height : G.Width) >= 16)
            {
                SetStatus(TEXT("This side has reached the 16-point terrain limit."));
                bEdgeDrag = false;
                bConsumeClick = true;
                return true;
            }
        }
        else
            DragAxis = HoverAxis;
        DragPixel = FVector2D(VP->GetMouseX(), VP->GetMouseY());
        DragAmount = 0;
        PreDragPoints = Points;
        PreDragFocus = Focus;
    }
    else if (Tool == Cmd::Select && HoverPatch.IsValid())
    {
        const bool Extend = VP->KeyState(EKeys::LeftControl) || VP->KeyState(EKeys::RightControl) ||
                            VP->KeyState(EKeys::LeftShift) || VP->KeyState(EKeys::RightShift);
        SelectPoint(HoverPatch.Get(), HoverPoint, Extend);
        bPointPressed = true;
    }
    else if (Tool != Cmd::Extrude)
        return false;
    bConsumeClick = true;
    Client->Invalidate();
    return true;
}

void FPatchEdMode::MoveHandle(FEditorViewportClient *Client, FViewport *, int32 X, int32 Y)
{
    auto *P = bEdgeDrag ? ExtrudeEdge.Patch.Get() : Active();
    if (!P || DragScreenAxis.IsNearlyZero())
        return;
    double Next = FVector2D::DotProduct(FVector2D(X, Y) - DragPixel, DragScreenAxis) / DragScreenAxis.SquaredLength();
    if (bEdgeDrag && !NumericInput.IsEmpty())
        Next = FCString::Atod(*NumericInput);
    else if (GetDefault<ULevelEditorViewportSettings>()->GridEnabled)
        Next = FMath::GridSnap(Next, double(GEditor->GetGridSize()));
    if (FMath::IsNearlyEqual(Next, DragAmount, .001))
        return;
    if (!Gesture)
        Gesture = MakeUnique<FScopedTransaction>(
            FText::FromString(bEdgeDrag ? TEXT("Extrude patch boundary") : TEXT("Move patch points")));
    if (bEdgeDrag)
    {
        Touch(P);
        P->Grid = Touched.FindChecked(P).Key;
        PatchOps::ExtrudeBoundary(P->Grid, ExtrudeEdge.Side, P->GetActorTransform(), HandleDirection * Next);
        P->Rebuild();
    }
    else
        TransformPoints(HandleDirection * (Next - DragAmount), FRotator::ZeroRotator, FVector::ZeroVector);
    DragAmount = Next;
    Client->Invalidate();
}

void FPatchEdMode::ResetInteraction(bool Cancel)
{
    if (Cancel && (bEdgeDrag || DragAxis != INDEX_NONE))
    {
        Points = PreDragPoints;
        Focus = PreDragFocus;
    }
    FinishGesture(Cancel);
    HoverEdge = {};
    ExtrudeEdge = {};
    SeamFrom = {};
    SeamTo = {};
    HoverPatch.Reset();
    HoverAxis = HoverPoint = DragAxis = INDEX_NONE;
    bPointPressed = bEdgeDrag = bConsumeClick = bReverseSeam = bPlaceHit = false;
    PlaceStage = 0;
    DragAmount = 0;
    NumericInput.Reset();
    Cursor.Reset();
}

void FPatchEdMode::RenderInteraction(const FSceneView *View, FPrimitiveDrawInterface *PDI)
{
    if (PDI->IsHitTesting())
        return;
    const FLinearColor Gold(1, .56f, .04f), Aqua(.03f, .86f, .86f);
    if (Tool == Cmd::Select && !Points.IsEmpty())
        for (auto *Client : GEditor->GetLevelViewportClients())
            if (Client && Client->Viewport == View->Family->RenderTarget &&
                Client->GetWidgetMode() == UE::Widget::WM_Translate)
            {
                const FLinearColor Colors[] = {FLinearColor(.98f, .07f, .07f), FLinearColor(.14f, .9f, .18f),
                                               FLinearColor(.1f, .42f, 1)};
                for (int32 I = 0; I < 3; ++I)
                {
                    const bool Hot = (DragAxis != INDEX_NONE ? DragAxis : HoverAxis) == I;
                    DrawArrow(PDI, *View, GetWidgetLocation(), PointAxis(Client, I), Hot ? Gold : Colors[I], Hot);
                }
            }
    if (Primitive(Tool))
    {
        FPatchGrid Grid;
        FTransform Xf;
        if (!BuildPlacement(Grid, Xf))
            return;
        // Sample the actual surface so round profiles, tapered shapes and sag are visible before placement.
        const int32 W = Grid.Kind == EPatchKind::Curve ? (Grid.Width - 1) * 2 : Grid.Width - 1;
        const int32 H = Grid.Kind == EPatchKind::Curve ? (Grid.Height - 1) * 2 : Grid.Height - 1;
        for (int32 Y = 0; Y <= H; ++Y)
            for (int32 X = 0; X <= W; ++X)
            {
                auto At = [&](int32 U, int32 V) {
                    return Xf.TransformPosition(Grid.Sample(double(U) / W, double(V) / H).Position);
                };
                if (X < W && (Y % 2 == 0 || Y == H || Grid.Kind == EPatchKind::Terrain))
                    PDI->DrawLine(At(X, Y), At(X + 1, Y), Aqua, SDPG_Foreground, 1, 0, true);
                if (Y < H && (X % 2 == 0 || X == W || Grid.Kind == EPatchKind::Terrain))
                    PDI->DrawLine(At(X, Y), At(X, Y + 1), Aqua, SDPG_Foreground, 1, 0, true);
            }
        PDI->DrawPoint(PlaceBase, Gold, 10, SDPG_Foreground);
        if (Tool == Cmd::Rope && PlaceStage)
            PDI->DrawPoint(PlaceEnd, Gold, 10, SDPG_Foreground);
        return;
    }
    if (Tool != Cmd::Extrude && Tool != Cmd::ConnectEdges)
        return;
    auto DrawEdge = [&](const FBoundary &Boundary, FLinearColor Color, float Width) {
        if (!Boundary.Valid())
            return;
        const auto &P = *Boundary.Patch;
        auto Edge = PatchOps::EdgeIndices(P.Grid, Boundary.Side);
        for (int32 I = 1; I < Edge.Num(); ++I)
            PDI->DrawLine(WorldPoint(P, Edge[I - 1]), WorldPoint(P, Edge[I]), Color, SDPG_Foreground, Width, 0, true);
    };
    if (Tool == Cmd::Extrude && Active() && Active()->Grid.Valid())
    {
        for (int32 Side = 0; Side < 4; ++Side)
        {
            const bool Hot = bEdgeDrag ? ExtrudeEdge.Side == Side : HoverEdge.Side == Side;
            FVector C, D;
            GetExtrudeHandle(*Active(), Side, C, D);
            DrawEdge({Active(), Side}, Hot ? Gold : Aqua, Hot ? 4 : 1);
            DrawArrow(PDI, *View, C, D, Hot ? Gold : Aqua, Hot);
        }
        return;
    }
    for (TActorIterator<APatchSurface> It(GetWorld()); It; ++It)
        if (!It->IsHiddenEd() && It->Grid.Valid())
            for (int32 I = 0; I < 4; ++I)
                DrawEdge({*It, I}, FLinearColor(.12f, .45f, .44f), 1);
    DrawEdge(HoverEdge, FLinearColor::White, 3);
    DrawEdge(SeamFrom, Gold, 3);
    DrawEdge(SeamTo, Aqua, 3);
    if (!HasSeamPair())
        return;
    auto &A = *SeamFrom.Patch, &B = *SeamTo.Patch;
    auto From = PatchOps::EdgeIndices(A.Grid, SeamFrom.Side), To = PatchOps::EdgeIndices(B.Grid, SeamTo.Side);
    if (From.Num() != To.Num())
        return;
    if (bReverseSeam)
        Algo::Reverse(To);
    FPatchGrid Predicted = A.Grid;
    for (int32 I = 0; I < From.Num(); ++I)
    {
        const FVector End = WorldPoint(B, To[I]);
        PDI->DrawLine(WorldPoint(A, From[I]), End, FLinearColor::White, SDPG_Foreground, 1, 0, true);
        Predicted.Points[From[I]].Position = A.GetActorTransform().InverseTransformPosition(End);
    }
    if (A.Grid.Kind != EPatchKind::Curve)
        return;
    auto SampleNormal = [](const FPatchGrid &Grid, const FTransform &Transform, int32 Side, double T, FVector &Pos) {
        const FVector2D UVs[] = {{T, 0}, {1, T}, {1 - T, 1}, {0, 1 - T}};
        const auto UV = UVs[Side];
        auto At = [&](double U, double V) { return Transform.TransformPosition(Grid.Sample(U, V).Position); };
        Pos = At(UV.X, UV.Y);
        const FVector DU = At(UV.X + .001, UV.Y) - At(UV.X - .001, UV.Y),
                      DV = At(UV.X, UV.Y + .001) - At(UV.X, UV.Y - .001);
        return DU.Cross(DV).GetSafeNormal() * (Grid.bInverted ? -1. : 1.);
    };
    for (int32 I = 0; I <= 8; ++I)
    {
        const double T = double(I) / 8;
        FVector Start, End;
        const auto N = SampleNormal(Predicted, A.GetActorTransform(), SeamFrom.Side, T, Start);
        const auto M = SampleNormal(B.Grid, B.GetActorTransform(), SeamTo.Side, bReverseSeam ? 1. - T : T, End);
        if (N.IsNearlyZero() || M.IsNearlyZero())
            continue;
        const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(N.Dot(M), -1., 1.)));
        const auto Heat = FMath::Lerp(FLinearColor::Green, FLinearColor::Red, float(FMath::Clamp(Angle / 90., 0., 1.)));
        const double Len = PixelScale(*View, End) * 30;
        PDI->DrawLine(Start, Start + N * Len, Heat, SDPG_Foreground, 3, 0, true);
        PDI->DrawLine(End, End + M * (Len * .8), Aqua, SDPG_Foreground, 1, 0, true);
    }
}

void FPatchEdMode::DrawHUD(FEditorViewportClient *Client, FViewport *VP, const FSceneView *View, FCanvas *Canvas)
{
    FEdMode::DrawHUD(Client, VP, View, Canvas);
    FString Label;
    FLinearColor LabelColor = FLinearColor::White;
    FVector At = FVector::ZeroVector;
    if (bEdgeDrag || DragAxis != INDEX_NONE)
    {
        At = HandleOrigin + HandleDirection * DragAmount;
        const TCHAR *Axes[] = {TEXT("X"), TEXT("Y"), TEXT("Z")};
        Label = FString::Printf(TEXT("%s  %+.1f cm"), bEdgeDrag ? TEXT("Extrude") : Axes[DragAxis], DragAmount);
        if (bEdgeDrag && !NumericInput.IsEmpty())
            Label = FString::Printf(TEXT("Extrude  %s cm  |  Enter to apply"), *NumericInput);
    }
    else if (Primitive(Tool) && bPlaceHit)
    {
        At = PlaceBase;
        if (Tool == Cmd::Rope && PlaceStage)
        {
            At = PlaceEnd;
            Label = FString::Printf(TEXT("Span %.1f cm   Sag %.0f%%"), FVector::Distance(PlaceBase, PlaceEnd),
                                    Options->RopeSagPercent);
        }
        else if (!PlaceStage)
            Label = TEXT("Click to anchor");
        else if (Tool == Cmd::Terrain || Tool == Cmd::Curve)
            Label = FString::Printf(TEXT("%.0f x %.0f cm"), Options->Size.X, Options->Size.Y);
        else
            Label = FString::Printf(TEXT("%s %.1f cm"), PlaceStage == 1 ? TEXT("Radius") : TEXT("Height"),
                                    PlaceStage == 1 ? Options->Size.X * .5 : Options->Size.Z);
        if (!NumericInput.IsEmpty())
            Label = NumericInput + TEXT(" cm");
    }
    else if (Tool == Cmd::ConnectEdges && (HoverEdge.Valid() || SeamFrom.Valid()))
    {
        const bool Paired = HasSeamPair();
        const auto &Edge = Paired ? SeamTo : (HoverEdge.Valid() ? HoverEdge : SeamFrom);
        const auto Indices = PatchOps::EdgeIndices(Edge.Patch->Grid, Edge.Side);
        At = WorldPoint(*Edge.Patch, Indices[Indices.Num() / 2]);
        if (!SeamFrom.Valid())
            Label = FString::Printf(TEXT("%d pts  |  Click source edge"), Indices.Num());
        else if (Edge.Patch == SeamFrom.Patch)
            Label = FString::Printf(TEXT("Source: %d pts  |  Pick destination"), Indices.Num());
        else
        {
            const auto Pair = InspectSeam(Edge, Paired ? TOptional<bool>(bReverseSeam) : TOptional<bool>());
            if (!Pair.Issue.IsEmpty())
            {
                Label = Pair.Issue;
                LabelColor = FLinearColor(1, .45f, .2f);
            }
            else
            {
                Label = FString::Printf(TEXT("%d pairs  |  %s"), Indices.Num(),
                                        Paired ? TEXT("R reverse  |  Enter join") : TEXT("Click destination edge"));
                if (Paired && Edge.Patch->Grid.Kind == EPatchKind::Curve)
                    Label += TEXT("\nSeam normals: green = aligned, red = 90 deg or more");
            }
        }
    }
    FVector2D Pixel;
    if (!Label.IsEmpty() && View->WorldToPixel(At, Pixel))
    {
        UFont *Font = GEngine->GetSmallFont();
        const double Dpi = Canvas->GetDPIScale();
        FVector2D Origin = (Pixel + FVector2D(14, 14)) / Dpi;
        int32 Height, Width;
        Font->GetStringHeightAndWidth(Label, Height, Width);
        const FVector2D Limit = FVector2D(VP->GetSizeXY()) / Dpi - FVector2D(Width + 4, Height + 4);
        Origin.X = FMath::Clamp(Origin.X, 4., FMath::Max(4., Limit.X));
        Origin.Y = FMath::Clamp(Origin.Y, 4., FMath::Max(4., Limit.Y));
        FCanvasTextItem Text(Origin, FText::FromString(Label), Font, LabelColor);
        Text.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Text);
    }
}
