#include "PatchOperations.h"

int32 PatchOps::FeatherEdgeAlpha(FPatchGrid &Grid, int32 Side, const FTransform &Transform, double Width,
                                 const TSet<int32> *Selection)
{
    if (!Grid.Valid() || Side < 0 || Side > 3 || !FMath::IsFinite(Width) || Width <= 0.)
        return 0;
    const int32 Inward[] = {Grid.Width, -1, -Grid.Width, 1};
    const int32 Depth = Side % 2 == 0 ? Grid.Height : Grid.Width;
    int32 Changed = 0;
    for (int32 Index : EdgeIndices(Grid, Side))
    {
        double Distance = 0.;
        for (int32 Step = 0; Step < Depth; ++Step, Index += Inward[Side])
        {
            auto &Point = Grid.Points[Index];
            if (Step)
                Distance += Transform.TransformVector(Point.Position - Grid.Points[Index - Inward[Side]].Position).Length();
            if (Distance >= Width)
                break;
            const float Alpha = float(FMath::SmoothStep(0., Width, Distance));
            if (!Point.bLocked && (!Selection || Selection->Contains(Index)) && Point.Color.A > Alpha)
            {
                Point.Color.A = Alpha;
                ++Changed;
            }
        }
    }
    return Changed;
}

FPatchPoint PatchOps::Blend(const FPatchPoint &A, const FPatchPoint &B, double T)
{
    FPatchPoint P;
    P.Position = FMath::Lerp(A.Position, B.Position, T);
    P.UV = FMath::Lerp(A.UV, B.UV, T);
    P.LightUV = FMath::Lerp(A.LightUV, B.LightUV, T);
    P.SmoothingUV = FMath::Lerp(A.SmoothingUV, B.SmoothingUV, T);
    P.Color = FMath::Lerp(A.Color, B.Color, float(T));
    P.bLocked = A.bLocked && B.bLocked;
    return P;
}

FPatchGrid PatchOps::Section(const FPatchGrid &G, int32 X, int32 Y, int32 W, int32 H)
{
    FPatchGrid Out = G;
    Out.Width = W;
    Out.Height = H;
    Out.Points.SetNum(W * H);
    Out.Turned.SetNumZeroed((W - 1) * (H - 1));
    for (int32 J = 0; J < H; ++J)
        for (int32 I = 0; I < W; ++I)
        {
            Out.At(I, J) = G.At(X + I, Y + J);
            if (I + 1 < W && J + 1 < H)
                Out.Turned[Out.Cell(I, J)] = G.Turned[G.Cell(X + I, Y + J)];
        }
    return Out;
}

void PatchOps::Transpose(FPatchGrid &G)
{
    FPatchGrid Out = G;
    Swap(Out.Width, Out.Height);
    for (int32 Y = 0; Y < G.Height; ++Y)
        for (int32 X = 0; X < G.Width; ++X)
        {
            Out.At(Y, X) = G.At(X, Y);
            if (X + 1 < G.Width && Y + 1 < G.Height)
                Out.Turned[Out.Cell(Y, X)] = G.Turned[G.Cell(X, Y)];
        }
    Out.bInverted = !Out.bInverted;
    G = MoveTemp(Out);
}

bool PatchOps::Resize(FPatchGrid &Grid, EPatchAxis Axis, int32 Index, bool bInsert, bool bExtrude)
{
    FPatchGrid G = Grid;
    if (Axis == EPatchAxis::Rows)
        Transpose(G);
    const bool Curve = G.Kind == EPatchKind::Curve;
    const int32 Step = Curve ? 2 : 1;
    const int32 W = G.Width + (bInsert ? Step : -Step);
    if (W < (Curve ? 3 : 2) || W > (Curve ? 15 : 16))
        return false;
    const int32 K = FMath::Clamp(Index, 0, G.Width - 1);
    const int32 Start = bInsert ? FMath::Min(K / Step * Step, G.Width - Step - 1) : FMath::Min(K, G.Width - Step);
    const bool First = bExtrude && K == 0;
    FPatchGrid Out = G;
    Out.Width = W;
    Out.Points.SetNum(W * G.Height);
    Out.Turned.Init(0, (W - 1) * (G.Height - 1));
    for (int32 Y = 0; Y < G.Height; ++Y)
    {
        for (int32 X = 0; X < W; ++X)
        {
            FPatchPoint &P = Out.At(X, Y);
            if (!bInsert)
                P = G.At(X < Start ? X : X + Step, Y);
            else if (bExtrude)
            {
                const int32 I = First ? X - Step : X;
                if (I < 0)
                    P = Blend(G.At(0, Y), G.At(1, Y), double(I));
                else if (I >= G.Width)
                    P = Blend(G.At(G.Width - 2, Y), G.At(G.Width - 1, Y), double(I - G.Width + 2));
                else
                    P = G.At(I, Y);
            }
            else if (X <= Start)
                P = G.At(X, Y);
            else if (X >= Start + Step * 2)
                P = G.At(X - Step, Y);
            else if (!Curve)
                P = Blend(G.At(Start, Y), G.At(Start + 1, Y), .5);
            else
            {
                const FPatchPoint L = Blend(G.At(Start, Y), G.At(Start + 1, Y), .5);
                const FPatchPoint R = Blend(G.At(Start + 1, Y), G.At(Start + 2, Y), .5);
                P = X == Start + 1 ? L : (X == Start + 2 ? Blend(L, R, .5) : R);
            }
            if (X + 1 < W && Y + 1 < G.Height)
            {
                int32 OldX = X;
                if (!bInsert && X >= Start)
                    OldX += Step;
                if (bInsert && ((bExtrude && First) || (!bExtrude && X > Start)))
                    OldX -= Step;
                Out.Turned[Out.Cell(X, Y)] = G.Turned[G.Cell(FMath::Clamp(OldX, 0, G.Width - 2), Y)];
            }
        }
    }
    if (Axis == EPatchAxis::Rows)
        Transpose(Out);
    Grid = MoveTemp(Out);
    return true;
}

bool PatchOps::ExtrudeBoundary(FPatchGrid &Grid, int32 Side, const FTransform &Transform, FVector WorldOffset)
{
    if (!Grid.Valid() || Grid.Kind != EPatchKind::Terrain || Side < 0 || Side > 3 || WorldOffset.IsNearlyZero())
        return false;
    const bool Rows = Side % 2 == 0;
    const int32 Last = (Rows ? Grid.Height : Grid.Width) - 1;
    const int32 Boundary = Side == 0 || Side == 3 ? 0 : Last;
    const int32 Inward = (Boundary == 0 ? 1 : -1) * (Rows ? Grid.Width : 1);
    const auto Source = EdgeIndices(Grid, Side);
    FPatchGrid Result = Grid;
    if (!Resize(Result, Rows ? EPatchAxis::Rows : EPatchAxis::Columns, Boundary, true, true))
        return false;
    const auto Dest = EdgeIndices(Result, Side);
    const FVector LocalOffset = Transform.InverseTransformVector(WorldOffset);
    for (int32 I = 0; I < Source.Num(); ++I)
    {
        const auto &Outer = Grid.Points[Source[I]];
        const auto &Inner = Grid.Points[Source[I] + Inward];
        const double Span = Transform.TransformVector(Outer.Position - Inner.Position).Length();
        const double Ratio = Span > UE_SMALL_NUMBER ? WorldOffset.Length() / Span : 0.;
        FPatchPoint &Point = Result.Points[Dest[I]];
        Point = Outer;
        Point.Position += LocalOffset;
        Point.UV += (Outer.UV - Inner.UV) * Ratio;
        Point.LightUV += (Outer.LightUV - Inner.LightUV) * Ratio;
        Point.SmoothingUV += (Outer.SmoothingUV - Inner.SmoothingUV) * Ratio;
    }
    Grid = MoveTemp(Result);
    return true;
}

void PatchOps::Redistribute(FPatchGrid &G, EPatchAxis Axis)
{
    if (Axis == EPatchAxis::Rows)
        Transpose(G);
    for (int32 Y = 0; Y < G.Height; ++Y)
    {
        const FPatchGrid &Src = G;
        TArray<FPatchPoint> Line;
        TArray<double> Distances;
        double Length = 0;
        for (int32 X = 0; X < G.Width; ++X)
        {
            Line.Add(Src.At(X, Y));
            if (X)
                Length += FVector::Distance(Line[X].Position, Line[X - 1].Position);
            Distances.Add(Length);
        }
        if (Length < UE_SMALL_NUMBER)
            continue;
        int32 I = 1;
        for (int32 X = 1; X < G.Width - 1; ++X)
        {
            const double S = Length * X / (G.Width - 1);
            while (I < G.Width - 1 && Distances[I] < S)
                ++I;
            if (!G.At(X, Y).bLocked)
                G.At(X, Y) =
                    Blend(Line[I - 1], Line[I],
                          (S - Distances[I - 1]) / FMath::Max(UE_SMALL_NUMBER, Distances[I] - Distances[I - 1]));
        }
    }
    if (Axis == EPatchAxis::Rows)
        Transpose(G);
}

bool PatchOps::Split(FPatchGrid &G, EPatchAxis Axis, int32 I, FPatchGrid &Other)
{
    const bool Rows = Axis == EPatchAxis::Rows;
    const int32 N = Rows ? G.Height : G.Width;
    if (I <= 0 || I >= N - 1 || (G.Kind == EPatchKind::Curve && (I & 1)))
        return false;
    Other = Rows ? Section(G, 0, I, G.Width, N - I) : Section(G, I, 0, N - I, G.Height);
    G = Rows ? Section(G, 0, 0, G.Width, I + 1) : Section(G, 0, 0, I + 1, G.Height);
    return true;
}

FPatchGrid PatchOps::ToTerrain(const FPatchGrid &G, int32 W, int32 H)
{
    FPatchGrid Out = FPatchGrid::Make(EPatchPrimitive::Terrain, W, H, FVector::OneVector, 0);
    Out.bInverted = G.bInverted;
    for (int32 Y = 0; Y < Out.Height; ++Y)
        for (int32 X = 0; X < Out.Width; ++X)
            Out.At(X, Y) = G.Sample(double(X) / (Out.Width - 1), double(Y) / (Out.Height - 1));
    return Out;
}

FVector PatchOps::PointNormal(const FPatchGrid &G, int32 X, int32 Y)
{
    const FVector DX = G.At(FMath::Min(X + 1, G.Width - 1), Y).Position - G.At(FMath::Max(X - 1, 0), Y).Position;
    const FVector DY = G.At(X, FMath::Min(Y + 1, G.Height - 1)).Position - G.At(X, FMath::Max(Y - 1, 0)).Position;
    return FVector::CrossProduct(DX, DY).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector) * (G.bInverted ? -1 : 1);
}

static FPatchPoint EdgePoint(const FPatchGrid &G, int32 Side, int32 I)
{
    switch (Side)
    {
    case 0:
        return G.At(I, 0);
    case 1:
        return G.At(G.Width - 1, I);
    case 2:
        return G.At(G.Width - 1 - I, G.Height - 1);
    default:
        return G.At(0, G.Height - 1 - I);
    }
}

static FPatchGrid Strip(const FPatchGrid &G, int32 Side)
{
    const int32 Count = (Side % 2 == 0) ? G.Width : G.Height;
    FPatchGrid Out = FPatchGrid::Make(G.Kind == EPatchKind::Curve ? EPatchPrimitive::Curve : EPatchPrimitive::Terrain,
                                      Count, G.Kind == EPatchKind::Curve ? 3 : 2, FVector::OneVector, 0);
    Out.Subdivisions = G.Subdivisions;
    Out.bInverted = G.bInverted;
    return Out;
}

TArray<FPatchGrid> PatchOps::Thicken(const FPatchGrid &G, double Distance, bool bSeams)
{
    FPatchGrid Back = G;
    for (int32 Y = 0; Y < G.Height; ++Y)
        for (int32 X = 0; X < G.Width; ++X)
            Back.At(X, Y).Position -= Distance * PointNormal(G, X, Y);
    Back.bInverted = !G.bInverted;
    TArray<FPatchGrid> Result{Back};
    if (!bSeams)
        return Result;
    for (int32 S = 0; S < 4; ++S)
    {
        const int32 Length = S % 2 == 0 ? G.Width : G.Height;
        bool ClosedSeam = true;
        bool Collapsed = true;
        for (int32 I = 0; I < Length; ++I)
        {
            const auto Point = EdgePoint(G, S, I).Position;
            ClosedSeam &= Point.Equals(EdgePoint(G, (S + 2) % 4, Length - 1 - I).Position, .001);
            Collapsed &= Point.Equals(EdgePoint(G, S, 0).Position, .001);
        }
        if (ClosedSeam || Collapsed)
            continue;
        FPatchGrid Wall = Strip(G, S);
        Wall.bInverted = !G.bInverted;
        for (int32 X = 0; X < Wall.Width; ++X)
            for (int32 Y = 0; Y < Wall.Height; ++Y)
            {
                const double T = double(Y) / (Wall.Height - 1);
                auto &P = Wall.At(X, Y);
                P = Blend(EdgePoint(G, S, X), EdgePoint(Back, S, X), T);
                P.UV = P.LightUV = P.SmoothingUV = FVector2D(double(X) / (Wall.Width - 1), T);
            }
        Result.Add(MoveTemp(Wall));
    }
    return Result;
}

FPatchGrid PatchOps::Cap(const FPatchGrid &G, int32 Side, bool bInside, bool bEndCap)
{
    FPatchGrid Out = Strip(G, Side);
    FVector Center = FVector::ZeroVector;
    for (int32 X = 0; X < Out.Width; ++X)
        Center += EdgePoint(G, Side, X).Position;
    const FVector Start = EdgePoint(G, Side, 0).Position, End = EdgePoint(G, Side, Out.Width - 1).Position;
    Center = Start.Equals(End, .01) ? Center / (Out.Width - 1) - Start / (Out.Width - 1) : (Start + End) * .5;
    for (int32 X = 0; X < Out.Width; ++X)
    {
        const double U = double(X) / (Out.Width - 1);
        FPatchPoint Rim = EdgePoint(G, Side, X), Target = Rim;
        Target.Position = bEndCap && !Start.Equals(End, .01) ? FMath::Lerp(Start, End, U) : Center;
        if (!Start.Equals(End, .01))
        {
            const FVector Middle = EdgePoint(G, Side, Out.Width / 2).Position;
            if (!bEndCap)
                Target.Position = bInside ? Start + End - Middle : Middle;
            else if (!bInside)
            {
                const FVector Offset = Middle - (Start + End) * .5;
                const double Weight = 1.0 - FMath::Abs(2 * U - 1);
                Target.Position += Offset * (2 * Weight);
            }
        }
        for (int32 Y = 0; Y < Out.Height; ++Y)
        {
            const double V = double(Y) / (Out.Height - 1);
            Out.At(X, Y) = Blend(Rim, Target, V);
            Out.At(X, Y).UV = Out.At(X, Y).LightUV = Out.At(X, Y).SmoothingUV = FVector2D(U, V);
        }
    }
    Out.bInverted = !G.bInverted;
    return Out;
}
