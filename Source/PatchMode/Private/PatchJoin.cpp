#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "MeshDescription.h"
#include "Model.h"
#include "PatchEdMode.h"
#include "PatchOperations.h"

using Cmd = EPatchCommand;

using PatchOps::EdgeIndices;

TArray<int32> PatchOps::EdgeIndices(const FPatchGrid &G, int32 E)
{
    TArray<int32> Edge;
    const int32 N = (E % 2 == 0) ? G.Width : G.Height;
    for (int32 I = 0; I < N; ++I)
    {
        switch (E)
        {
        case 0:
            Edge.Add(G.Index(I, 0));
            break;
        case 1:
            Edge.Add(G.Index(G.Width - 1, I));
            break;
        case 2:
            Edge.Add(G.Index(G.Width - 1 - I, G.Height - 1));
            break;
        case 3:
            Edge.Add(G.Index(0, G.Height - 1 - I));
            break;
        }
    }
    return Edge;
}

// Geometric seams stay independent in UV/color space. Match complete boundaries in world space.
void FPatchEdMode::SetConnectedSubdivisions(APatchSurface &Start, int32 Steps)
{
    TArray<APatchSurface *> Pending{&Start};
    TSet<APatchSurface *> Seen{&Start};
    while (!Pending.IsEmpty())
    {
        APatchSurface *A = Pending.Pop();
        if (A->Grid.Subdivisions != Steps)
        {
            A->Modify();
            A->Grid.Subdivisions = Steps;
            A->Changed();
        }
        for (TActorIterator<APatchSurface> It(GetWorld()); It; ++It)
        {
            APatchSurface *B = *It;
            if (Seen.Contains(B) || B->Grid.Kind != EPatchKind::Curve || !B->Grid.Valid())
                continue;
            bool Joined = false;
            for (int32 E = 0; E < 4 && !Joined; ++E)
            {
                const auto From = EdgeIndices(A->Grid, E);
                for (int32 F = 0; F < 4 && !Joined; ++F)
                {
                    const auto To = EdgeIndices(B->Grid, F);
                    if (From.Num() != To.Num())
                        continue;
                    bool Forward = true, Reverse = true;
                    double Length = 0;
                    for (int32 I = 0; I < From.Num() && (Forward || Reverse); ++I)
                    {
                        const FVector V = A->GetActorTransform().TransformPosition(A->Grid.Points[From[I]].Position);
                        Forward &=
                            V.Equals(B->GetActorTransform().TransformPosition(B->Grid.Points[To[I]].Position), .001);
                        Reverse &= V.Equals(
                            B->GetActorTransform().TransformPosition(B->Grid.Points[To.Last(I)].Position), .001);
                        if (I)
                            Length += FVector::Distance(
                                V, A->GetActorTransform().TransformPosition(A->Grid.Points[From[I - 1]].Position));
                    }
                    Joined = (Forward || Reverse) && Length > .001;
                }
            }
            if (Joined)
            {
                Seen.Add(B);
                Pending.Add(B);
            }
        }
    }
}

static FString SeamIssue(const APatchSurface &P, const TArray<int32> &From, const APatchSurface &Other,
                         const TArray<int32> &To)
{
    if (&P == &Other)
        return TEXT("Choose an edge on another patch.");
    if (P.Grid.Kind != Other.Grid.Kind)
        return FString::Printf(TEXT("Source %d pts, target %d pts. Choose a %s patch."),
                               From.Num(), To.Num(), P.Grid.Kind == EPatchKind::Curve ? TEXT("Curve") : TEXT("Terrain"));
    if (From.Num() != To.Num() || From.IsEmpty())
        return FString::Printf(TEXT("Source %d pts, target %d pts. Edge counts must match."), From.Num(), To.Num());
    const auto Src = P.GetActorTransform(), Dst = Other.GetActorTransform();
    for (int32 I = 0; I < From.Num(); ++I)
        if (P.Grid.Points[From[I]].bLocked &&
            !Src.TransformPosition(P.Grid.Points[From[I]].Position)
                 .Equals(Dst.TransformPosition(Other.Grid.Points[To[I]].Position), .001))
            return FString::Printf(TEXT("%d pairs. Unlock source point %d to move it."), From.Num(), From[I]);
    return {};
}

FPatchEdMode::FSeamPair FPatchEdMode::InspectSeam(const FBoundary &Target, TOptional<bool> Reverse) const
{
    FSeamPair Pair;
    if (!SeamFrom.Valid() || !Target.Valid())
    {
        Pair.Issue = TEXT("Pick two patch boundaries.");
        return Pair;
    }
    const auto &A = *SeamFrom.Patch, &B = *Target.Patch;
    Pair.Source = EdgeIndices(A.Grid, SeamFrom.Side);
    Pair.Destination = EdgeIndices(B.Grid, Target.Side);
    if (A.Grid.Kind == B.Grid.Kind && Pair.Source.Num() == Pair.Destination.Num())
    {
        if (Reverse.IsSet())
            Pair.bReversed = Reverse.GetValue();
        else
        {
            double ForwardScore = 0, ReverseScore = 0;
            for (int32 I = 0; I < Pair.Source.Num(); ++I)
            {
                const FVector Pos = A.GetActorTransform().TransformPosition(A.Grid.Points[Pair.Source[I]].Position);
                auto Score = [&](int32 Index) {
                    return FVector::DistSquared(Pos, B.GetActorTransform().TransformPosition(B.Grid.Points[Index].Position));
                };
                ForwardScore += Score(Pair.Destination[I]);
                ReverseScore += Score(Pair.Destination.Last(I));
            }
            Pair.bReversed = ReverseScore < ForwardScore;
        }
        if (Pair.bReversed)
            Algo::Reverse(Pair.Destination);
    }
    Pair.Issue = SeamIssue(A, Pair.Source, B, Pair.Destination);
    return Pair;
}

bool FPatchEdMode::ApplySeam(APatchSurface &P, const TArray<int32> &From, APatchSurface &Other, const TArray<int32> &To)
{
    const FString Issue = SeamIssue(P, From, Other, To);
    if (!Issue.IsEmpty())
    {
        SetStatus(Issue);
        return false;
    }
    const auto Src = P.GetActorTransform(), Dst = Other.GetActorTransform();
    P.Modify();
    for (int32 I = 0; I < From.Num(); ++I)
        P.Grid.Points[From[I]].Position =
            Src.InverseTransformPosition(Dst.TransformPosition(Other.Grid.Points[To[I]].Position));
    if (P.Grid.Kind == EPatchKind::Curve)
        SetConnectedSubdivisions(P, FMath::Max(P.Grid.Subdivisions, Other.Grid.Subdivisions));
    P.Changed();
    GEditor->RedrawLevelEditingViewports();
    return true;
}

void FPatchEdMode::Join(Cmd C, APatchSurface &P)
{
    FPatchGrid &G = P.Grid;
    const auto Source = P.GetActorTransform();
    const TSet<int32> Selected = Points.IsEmpty() ? TSet<int32>{Focus} : Points;
    if (C == Cmd::ConnectEdges)
    {
        auto Group = SelectedPatches();
        Group.Remove(&P);
        if (Group.Num() != 1)
        {
            SetStatus(TEXT("Select two patch actors and choose the source boundary in Tool Settings."));
            return;
        }
        auto *Other = Group[0];
        auto From = EdgeIndices(G, int32(Options->Edge));
        TArray<int32> Best;
        double Score = TNumericLimits<double>::Max();
        const auto Destination = Other->GetActorTransform();
        for (int32 E = 0; E < 4; ++E)
        {
            auto To = EdgeIndices(Other->Grid, E);
            if (From.Num() != To.Num() || G.Kind != Other->Grid.Kind)
                continue;
            for (int32 Pass = 0; Pass < 2; ++Pass)
            {
                if (Pass)
                    Algo::Reverse(To);
                double Sum = 0;
                for (int32 J = 0; J < From.Num(); ++J)
                    Sum += FVector::DistSquared(Source.TransformPosition(G.Points[From[J]].Position),
                                                Destination.TransformPosition(Other->Grid.Points[To[J]].Position));
                if (Sum < Score)
                {
                    Best = To;
                    Score = Sum;
                }
            }
        }
        if (Best.IsEmpty())
        {
            SetStatus(TEXT("The two edges need matching point counts and surface types."));
            return;
        }
        ApplySeam(P, From, *Other, Best);
        return;
    }
    TArray<FVector> Targets;
    if (C == Cmd::WeldPoints)
    {
        if (Selected.Num() < 2)
        {
            SetStatus(TEXT("Ctrl-click at least two points on the patch."));
            return;
        }
        Targets.Add(Source.TransformPosition(G.Points[Focus].Position));
    }
    else if (C == Cmd::TolerantWeld)
    {
        for (TActorIterator<APatchSurface> It(GetWorld()); It; ++It)
            for (int32 I = 0; I < It->Grid.Points.Num(); ++I)
                if (*It != &P || !Selected.Contains(I))
                    Targets.Add(It->GetActorTransform().TransformPosition(It->Grid.Points[I].Position));
    }
    else
    {
        // Only inspect the reference actors that the user explicitly selected in the Outliner.
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
        {
            auto *A = Cast<AActor>(*It);
            if (!A || A == &P)
                continue;
            if (C == Cmd::SnaptoBrush)
            {
                const ABrush *Brush = Cast<ABrush>(A);
                if (!Brush || !Brush->Brush || !Brush->Brush->Polys)
                    continue;
                for (const FPoly &Poly : Brush->Brush->Polys->Element)
                    for (auto V : Poly.Vertices)
                        Targets.Add(A->GetActorTransform().TransformPosition(FVector(V)));
                continue;
            }
            TArray<AActor *> Actors{A};
            if (C == Cmd::SnaptoPrefab)
            {
                TArray<AActor *> Children;
                A->GetAttachedActors(Children, true, true);
                Actors.Append(Children);
            }
            for (auto *Actor : Actors)
            {
                TInlineComponentArray<UStaticMeshComponent *> Components(Actor);
                for (auto *Comp : Components)
                {
                    if (!Comp->GetStaticMesh())
                        continue;
                    const FMeshDescription *Mesh = Comp->GetStaticMesh()->GetMeshDescription(0);
                    if (!Mesh)
                        continue;
                    for (auto VertexId : Mesh->Vertices().GetElementIDs())
                        Targets.Add(Comp->GetComponentTransform().TransformPosition(
                            FVector(Mesh->GetVertexPosition(VertexId))));
                }
            }
        }
    }
    if (Targets.IsEmpty())
    {
        SetStatus(TEXT("No target vertices. Select the reference actor together with the patch."));
        return;
    }
    int32 Moved = 0;
    P.Modify();
    for (int32 I : Selected)
    {
        if (!G.Points.IsValidIndex(I) || G.Points[I].bLocked)
            continue;
        const FVector Pos = Source.TransformPosition(G.Points[I].Position);
        double Limit = C == Cmd::TolerantWeld ? FMath::Square(Options->WeldTolerance) : TNumericLimits<double>::Max();
        const FVector *Best = nullptr;
        for (const FVector &Target : Targets)
        {
            const double Dist = FVector::DistSquared(Pos, Target);
            if (Dist <= Limit)
            {
                Limit = Dist;
                Best = &Target;
            }
        }
        if (Best)
        {
            G.Points[I].Position = Source.InverseTransformPosition(*Best);
            ++Moved;
        }
    }
    if (Moved)
        P.Changed();
    SetStatus(FString::Printf(TEXT("Matched %d control points."), Moved));
    GEditor->RedrawLevelEditingViewports();
}
