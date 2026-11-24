#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PatchEdMode.h"
#include "SceneManagement.h"
#include "UDynamicMesh.h"

double FPatchEdMode::BrushWeight(double D) const
{
    const double Radius = Options->Radius, Inner = FMath::Clamp(Options->InnerRadius, 0., Radius);
    if (D > Radius || Radius <= 0.)
        return 0.;
    const double Weight = D <= Inner ? 1. : (Radius - D) / FMath::Max(.001, Radius - Inner);
    return Options->Strength * FMath::Pow(Weight, Options->Falloff);
}

namespace
{
// Intersect the sphere with the face plane, then clip its circle to the three half-planes.
void DrawContour(FPrimitiveDrawInterface *PDI, const FVector (&V)[3], FVector Center, double Radius, FLinearColor Color)
{
    if (Radius <= 0.)
        return;
    const FVector N = FVector::CrossProduct(V[1] - V[0], V[2] - V[0]).GetSafeNormal();
    const double Depth = N.Dot(Center - V[0]);
    if (N.IsNearlyZero() || FMath::Abs(Depth) >= Radius)
        return;
    const double R2 = Radius * Radius;
    if (FVector::DistSquared(Center, FMath::ClosestPointOnTriangleToPoint(Center, V[0], V[1], V[2])) > R2)
        return;
    if (FMath::Max3(FVector::DistSquared(Center, V[0]), FVector::DistSquared(Center, V[1]),
                    FVector::DistSquared(Center, V[2])) < R2)
        return;
    const FVector O = Center - Depth * N, U = (V[1] - V[0]).GetSafeNormal(), W = N.Cross(U);
    const double R = FMath::Sqrt(R2 - Depth * Depth);
    TArray<double, TInlineAllocator<8>> Angles{0., 2. * UE_DOUBLE_PI};
    for (int32 I = 0; I < 3; ++I)
    {
        const FVector Start = V[I] - O, D = V[(I + 1) % 3] - V[I];
        const double A = D.SquaredLength(), B = Start.Dot(D),
                     Discriminant = B * B - A * (Start.SquaredLength() - R * R);
        if (A < UE_SMALL_NUMBER || Discriminant < 0)
            continue;
        const double Root = FMath::Sqrt(Discriminant);
        for (double T : {(-B - Root) / A, (-B + Root) / A})
        {
            if (T < 0 || T > 1)
                continue;
            const FVector At = Start + D * T;
            double Theta = FMath::Atan2(At.Dot(W), At.Dot(U));
            if (Theta < 0)
                Theta += 2. * UE_DOUBLE_PI;
            Angles.Add(Theta);
        }
    }
    Angles.Sort();
    auto At = [&](double T) { return O + R * (U * FMath::Cos(T) + W * FMath::Sin(T)); };
    for (int32 I = 1; I < Angles.Num(); ++I)
    {
        const double A = Angles[I - 1], B = Angles[I];
        if (B - A < 1.e-6)
            continue;
        const FVector Mid = At((A + B) * .5);
        bool Inside = true;
        for (int32 E = 0; E < 3; ++E)
            Inside &= (V[(E + 1) % 3] - V[E]).Cross(Mid - V[E]).Dot(N) >= -1.e-5;
        if (!Inside)
            continue;
        const int32 Steps = FMath::Max(1, FMath::CeilToInt((B - A) * 10));
        FVector Last = At(A);
        for (int32 J = 1; J <= Steps; ++J)
        {
            const FVector Next = At(FMath::Lerp(A, B, double(J) / Steps));
            PDI->DrawLine(Last, Next, Color, SDPG_Foreground, 1.7f, 0, true);
            Last = Next;
        }
    }
}
} // namespace

void FPatchEdMode::RenderBrush(FPrimitiveDrawInterface *PDI) const
{
    if (!Cursor || PDI->IsHitTesting())
        return;
    PDI->DrawPoint(*Cursor, FLinearColor::White, 5, SDPG_Foreground);
    for (auto *Patch : SelectedPatches())
    {
        if (Options->bOnlySelected && Patch != Active())
            continue;
        const auto Transform = Patch->GetActorTransform();
        const auto *Mesh = Patch->Preview->GetDynamicMesh()->GetMeshPtr();
        if (!Mesh)
            continue;
        for (int32 T : Mesh->TriangleIndicesItr())
        {
            const auto Tri = Mesh->GetTriangle(T);
            const FVector V[] = {Transform.TransformPosition(Mesh->GetVertex(Tri.A)),
                                 Transform.TransformPosition(Mesh->GetVertex(Tri.B)),
                                 Transform.TransformPosition(Mesh->GetVertex(Tri.C))};
            DrawContour(PDI, V, *Cursor, Options->Radius, FLinearColor(1, .66f, .08f));
            if (Options->InnerRadius < Options->Radius)
                DrawContour(PDI, V, *Cursor, Options->InnerRadius, FLinearColor::White);
        }
        for (int32 I = 0; I < Patch->Grid.Points.Num(); ++I)
        {
            const auto &Point = Patch->Grid.Points[I];
            const FVector At = Transform.TransformPosition(Point.Position);
            const double D = FVector::Distance(*Cursor, At);
            if (D > Options->Radius)
                continue;
            const bool Protected = Point.bLocked || (Options->bOnlySelected && !Points.Contains(I));
            const float W = BrushWeight(D);
            const auto Color = FMath::Lerp(FLinearColor(.04f, .45f, .65f), FLinearColor(1, .43f, .02f), W);
            PDI->DrawPoint(At, Protected ? FLinearColor::Gray : Color, Protected ? 7 : 9 + 6 * W, SDPG_Foreground);
        }
    }
}
