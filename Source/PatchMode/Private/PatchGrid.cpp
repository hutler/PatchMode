#include "PatchSurface.h"

bool FPatchGrid::Valid() const
{
    return Width >= 2 && Height >= 2 && Width <= 16 && Height <= 16 && Points.Num() == Width * Height &&
           Turned.Num() == (Width - 1) * (Height - 1) && (Kind == EPatchKind::Terrain || ((Width & 1) && (Height & 1)));
}

FPatchGrid FPatchGrid::Make(EPatchPrimitive Shape, int32 W, int32 H, FVector Size, double Sag, FVector SagDirection)
{
    FPatchGrid G;
    G.Kind = Shape == EPatchPrimitive::Terrain ? EPatchKind::Terrain : EPatchKind::Curve;
    const bool bCurve = G.Kind == EPatchKind::Curve;
    G.Width = bCurve ? FMath::Clamp(W | 1, 3, 15) : FMath::Clamp(W, 2, 16);
    G.Height = bCurve ? FMath::Clamp(H | 1, 3, 15) : FMath::Clamp(H, 2, 16);
    double Sweep = 2 * PI;
    bool bSquare = false;
    switch (Shape)
    {
    case EPatchPrimitive::Bevel:
        G.Width = 3;
        Sweep = PI / 2;
        break;
    case EPatchPrimitive::SquareBevel:
        G.Width = 5;
        break;
    case EPatchPrimitive::EndCap:
        G.Width = 5;
        Sweep = PI;
        break;
    case EPatchPrimitive::SquareEndCap:
        G.Width = 7;
        break;
    case EPatchPrimitive::DenseCylinder:
        G.Width = 9;
        G.Height = 5;
        break;
    case EPatchPrimitive::VeryDenseCylinder:
        G.Width = 9;
        G.Height = 9;
        break;
    case EPatchPrimitive::Cylinder:
    case EPatchPrimitive::SquareCylinder:
    case EPatchPrimitive::Cone:
        G.Width = 9;
        break;
    case EPatchPrimitive::Rope:
        G.Width = 9;
        G.Height = 5;
        break;
    default:
        break;
    }
    bSquare = Shape == EPatchPrimitive::SquareBevel || Shape == EPatchPrimitive::SquareEndCap ||
              Shape == EPatchPrimitive::SquareCylinder;
    G.Points.SetNum(G.Width * G.Height);
    G.Turned.SetNumZeroed((G.Width - 1) * (G.Height - 1));
    for (int32 Y = 0; Y < G.Height; ++Y)
    {
        const double V = double(Y) / (G.Height - 1);
        for (int32 X = 0; X < G.Width; ++X)
        {
            const double U = double(X) / (G.Width - 1);
            FPatchPoint &P = G.At(X, Y);
            P.UV = P.LightUV = P.SmoothingUV = FVector2D(U, V);
            if (Shape == EPatchPrimitive::Terrain || Shape == EPatchPrimitive::Curve)
            {
                P.Position = FVector((U - .5) * Size.X, (V - .5) * Size.Y, 0);
                continue;
            }
            const double Angle = Sweep * U;
            double C = FMath::Cos(Angle), S = FMath::Sin(Angle);
            const double Radius = (X & 1) ? 1.0 / FMath::Cos(Sweep / (G.Width - 1)) : 1.0;
            C *= Radius;
            S *= Radius;
            if (bSquare)
            {
                const FVector2D Quarter[] = {{1, 0}, {1, 1}, {0, 1}};
                const FVector2D Half[] = {{1, 0}, {1, 1}, {-1, 1}, {-1, 0}};
                const FVector2D Box[] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}, {1, 1}};
                const FVector2D *Anchors = Shape == EPatchPrimitive::SquareBevel
                                               ? Quarter
                                               : (Shape == EPatchPrimitive::SquareEndCap ? Half : Box);
                const FVector2D Profile = (X & 1) ? (Anchors[X / 2] + Anchors[X / 2 + 1]) * .5 : Anchors[X / 2];
                C = Profile.X;
                S = Profile.Y;
            }
            const double Taper = Shape == EPatchPrimitive::Cone ? 1.0 - V : 1.0;
            P.Position = FVector(C * Size.X * .5 * Taper, S * Size.Y * .5 * Taper, V * Size.Z);
            if (Shape == EPatchPrimitive::Rope)
            {
                const double Weight = (Y & 1) ? (4 * V * (1 - V) + 4.0 / FMath::Square(G.Height - 1)) : 4 * V * (1 - V);
                P.Position =
                    FVector((V - .5) * Size.X, C * Size.Y * .5, S * Size.Y * .5) + SagDirection * (Sag * Weight);
            }
        }
    }
    return G;
}

FPatchPoint FPatchGrid::Sample(double U, double V) const
{
    U = FMath::Clamp(U, 0., 1.);
    V = FMath::Clamp(V, 0., 1.);
    const int32 Step = Kind == EPatchKind::Curve ? 2 : 1;
    const double SX = U * (Width - 1) / Step, SY = V * (Height - 1) / Step;
    const int32 X = FMath::Min(FMath::FloorToInt(SX), (Width - 1) / Step - 1);
    const int32 Y = FMath::Min(FMath::FloorToInt(SY), (Height - 1) / Step - 1);
    const double A = SX - X, B = SY - Y;
    double WX[3] = {1 - A, A, 0}, WY[3] = {1 - B, B, 0};
    if (Step == 2)
    {
        WX[0] = (1 - A) * (1 - A);
        WX[1] = 2 * A * (1 - A);
        WX[2] = A * A;
        WY[0] = (1 - B) * (1 - B);
        WY[1] = 2 * B * (1 - B);
        WY[2] = B * B;
    }
    FPatchPoint P;
    P.Color = FLinearColor::Transparent;
    for (int32 J = 0; J <= Step; ++J)
        for (int32 I = 0; I <= Step; ++I)
        {
            const FPatchPoint &Q = At(X * Step + I, Y * Step + J);
            const double K = WX[I] * WY[J];
            P.Position += Q.Position * K;
            P.UV += Q.UV * K;
            P.LightUV += Q.LightUV * K;
            P.SmoothingUV += Q.SmoothingUV * K;
            P.Color += Q.Color * float(K);
        }
    return P;
}

void FPatchGrid::Tessellate(TArray<FPatchPoint> &Verts, TArray<FIntVector> &Tris) const
{
    Verts.Reset();
    Tris.Reset();
    if (!Valid())
        return;
    const bool Curve = Kind == EPatchKind::Curve;
    const int32 Steps = FMath::Clamp(Subdivisions, 1, 32);
    const int32 W = Curve ? (Width - 1) / 2 * Steps + 1 : Width, H = Curve ? (Height - 1) / 2 * Steps + 1 : Height;
    Verts.Reserve(W * H);
    Tris.Reserve((W - 1) * (H - 1) * 2);
    for (int32 Y = 0; Y < H; ++Y)
        for (int32 X = 0; X < W; ++X)
            Verts.Add(Curve ? Sample(double(X) / (W - 1), double(Y) / (H - 1)) : At(X, Y));
    auto Add = [&](int32 A, int32 B, int32 C) {
        if (FVector::CrossProduct(Verts[B].Position - Verts[A].Position, Verts[C].Position - Verts[A].Position)
                .SizeSquared() > UE_SMALL_NUMBER)
            Tris.Add(bInverted ? FIntVector(A, C, B) : FIntVector(A, B, C));
    };
    for (int32 Y = 0; Y < H - 1; ++Y)
        for (int32 X = 0; X < W - 1; ++X)
        {
            const int32 A = Y * W + X, B = A + 1, C = A + W, D = C + 1;
            if (!Curve && Turned[Cell(X, Y)])
            {
                Add(A, C, B);
                Add(B, C, D);
            }
            else
            {
                Add(A, D, B);
                Add(A, C, D);
            }
        }
}
