#include "PatchEdMode.h"
#include "Math/Ray.h"
#include "EditorViewportClient.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Engine/StaticMesh.h"
#include "Model.h"
#include "MeshDescription.h"
#include "Components/StaticMeshComponent.h"

void FPatchEdMode::FaceToTerrain(const FRay &Ray)
{
    const FVector Start = Ray.Origin, End = Ray.PointAt(1.e8);
    double Nearest = TNumericLimits<double>::Max();
    TArray<FVector> Polygon;
    UMaterialInterface *Material = nullptr;
    auto Consider = [&](const TArray<FVector> &Vertices, UMaterialInterface *Mat) {
        for (int32 I = 2; I < Vertices.Num(); ++I)
        {
            FVector Hit, Normal;
            if (!FMath::SegmentTriangleIntersection(Start, End, Vertices[0], Vertices[I - 1], Vertices[I], Hit, Normal))
                continue;
            const double D = FVector::DistSquared(Hit, Start);
            if (D < Nearest)
            {
                Nearest = D;
                Polygon = Vertices;
                Material = Mat;
            }
        }
    };
    const FBox RayBox = FBox(Start.ComponentMin(End), Start.ComponentMax(End)).ExpandBy(.1);
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PatchFace), true);
    GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);
    auto *Comp = Cast<UStaticMeshComponent>(Hit.GetComponent());
    if (Comp && Comp->GetStaticMesh())
    {
        const auto *Mesh = Comp->GetStaticMesh()->GetMeshDescription(0);
        if (Mesh)
        {
            for (auto Poly : Mesh->Polygons().GetElementIDs())
            {
                TArray<FVector> V;
                for (const auto VertexId : Mesh->GetPolygonVertices(Poly))
                    V.Add(Comp->GetComponentTransform().TransformPosition(FVector(Mesh->GetVertexPosition(VertexId))));
                Consider(V, Comp->GetMaterial(Mesh->GetPolygonPolygonGroup(Poly).GetValue()));
            }
        }
    }
    for (TActorIterator<ABrush> It(GetWorld()); It; ++It)
    {
        if (It->IsVolumeBrush() || *It == GetWorld()->GetDefaultBrush() || !It->Brush || !It->Brush->Polys ||
            !RayBox.Intersect(It->GetComponentsBoundingBox(true)))
            continue;
        for (const FPoly &Face : It->Brush->Polys->Element)
        {
            TArray<FVector> V;
            for (auto Pos : Face.Vertices)
                V.Add(It->GetActorTransform().TransformPosition(FVector(Pos)));
            Consider(V, Face.Material);
        }
    }
    if (Polygon.Num() < 3)
    {
        SetStatus(TEXT("Click a visible static mesh or brush face."));
        return;
    }
    FScopedTransaction Change(FText::FromString(TEXT("Face to terrain")));
    const FVector Origin = Polygon[0];
    auto Make = [&](FVector A, FVector B, FVector C, FVector D) {
        auto G = FPatchGrid::Make(EPatchPrimitive::Terrain, 2, 2, FVector::OneVector, 0);
        G.At(0, 0).Position = A - Origin;
        G.At(1, 0).Position = B - Origin;
        G.At(1, 1).Position = C - Origin;
        G.At(0, 1).Position = D - Origin;
        return Spawn(G, FTransform(Origin), TEXT("Face_Terrain"), Material);
    };
    APatchSurface *P = nullptr;
    if (Polygon.Num() == 4)
        P = Make(Polygon[0], Polygon[1], Polygon[2], Polygon[3]);
    else
        for (int32 I = 2; I < Polygon.Num(); ++I)
            P = Make(Polygon[0], Polygon[I - 1], Polygon[I], Polygon[I]);
    if (P)
        SelectPatch(P);
    Tool = EPatchCommand::Select;
}
