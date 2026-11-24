#include "PatchBake.h"
#include "PatchSurface.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "Components/DynamicMeshComponent.h"

bool PatchBake::Bake(APatchSurface &Source, FString &Message)
{
    TArray<FPatchPoint> V;
    TArray<FIntVector> T;
    Source.Grid.Tessellate(V, T);
    if (T.IsEmpty())
    {
        Message = TEXT("The surface contains no non-degenerate triangles.");
        return false;
    }
    FMeshDescription Desc;
    FStaticMeshAttributes Attr(Desc);
    Attr.Register();
    Attr.GetVertexInstanceUVs().SetNumChannels(3);
    TArray<FVertexID> Ids;
    for (const FPatchPoint &P : V)
    {
        FVertexID Id = Desc.CreateVertex();
        Attr.GetVertexPositions()[Id] = FVector3f(P.Position);
        Ids.Add(Id);
    }
    const FPolygonGroupID Group = Desc.CreatePolygonGroup();
    Attr.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Patch");
    for (const FIntVector &Tri : T)
    {
        TArray<FVertexInstanceID, TInlineAllocator<3>> Corners;
        for (int32 I : {Tri.X, Tri.Y, Tri.Z})
        {
            const auto Id = Desc.CreateVertexInstance(Ids[I]);
            Attr.GetVertexInstanceUVs().Set(Id, 0, FVector2f(V[I].UV));
            Attr.GetVertexInstanceUVs().Set(Id, 1, FVector2f(V[I].LightUV));
            Attr.GetVertexInstanceUVs().Set(Id, 2, FVector2f(V[I].SmoothingUV));
            const auto &C = V[I].Color;
            Attr.GetVertexInstanceColors()[Id] = FVector4f(C.R, C.G, C.B, C.A);
            Corners.Add(Id);
        }
        Desc.CreateTriangle(Group, Corners);
    }
    if (!Source.SourceId.IsValid())
        Source.SourceId = FGuid::NewGuid();
    const FString Name = TEXT("SM_Patch_") + Source.SourceId.ToString(EGuidFormats::Digits);
    const FString Path = TEXT("/Game/PatchMeshes/") + Name;
    UStaticMesh *Mesh = LoadObject<UStaticMesh>(nullptr, *(Path + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
    const bool bNew = !Mesh;
    if (Mesh && Mesh->GetPackage()->GetMetaData().GetValue(Mesh, TEXT("PatchSource")) != Source.SourceId.ToString())
    {
        Message = TEXT("An unrelated asset already uses this bake path.");
        return false;
    }
    if (!Mesh)
        Mesh = NewObject<UStaticMesh>(CreatePackage(*Path), *Name, RF_Public | RF_Standalone | RF_Transactional);
    Mesh->Modify();
    Mesh->GetPackage()->GetMetaData().SetValue(Mesh, TEXT("PatchSource"), *Source.SourceId.ToString());
    if (!Mesh->GetNumSourceModels())
        Mesh->AddSourceModel();
    auto &Settings = Mesh->GetSourceModel(0).BuildSettings;
    Settings.bRecomputeNormals = true;
    Settings.bRecomputeTangents = true;
    Settings.bGenerateLightmapUVs = false;
    Settings.bUseFullPrecisionUVs = true;
    Mesh->GetStaticMaterials().Reset();
    Mesh->GetStaticMaterials().Add(
        FStaticMaterial(Source.Material ? Source.Material.Get() : UMaterial::GetDefaultMaterial(MD_Surface),
                        TEXT("Patch"), TEXT("Patch")));
    Mesh->SetLightMapCoordinateIndex(1);
    Mesh->CreateMeshDescription(0, MoveTemp(Desc));
    Mesh->CommitMeshDescription(0);
    Mesh->CreateBodySetup();
    UBodySetup *Body = Mesh->GetBodySetup();
    Body->RemoveSimpleCollision();
    Body->CollisionTraceFlag =
        Source.Collision == EPatchCollision::SurfaceTriangles ? CTF_UseComplexAsSimple : CTF_UseDefault;
    if (Source.Collision == EPatchCollision::ConvexHull)
    {
        // Use the small control cage, including some depth for otherwise planar hulls.
        FKConvexElem Hull;
        const FVector N =
            FVector::CrossProduct(V[T[0].Y].Position - V[T[0].X].Position, V[T[0].Z].Position - V[T[0].X].Position)
                .GetSafeNormal();
        for (const auto &Point : Source.Grid.Points)
        {
            Hull.VertexData.Add(Point.Position);
            Hull.VertexData.Add(Point.Position + N);
        }
        Hull.UpdateElemBox();
        Body->AggGeom.ConvexElems.Add(MoveTemp(Hull));
    }
    Mesh->GetBodySetup()->InvalidatePhysicsData();
    Mesh->Build(false);
    if (bNew)
        FAssetRegistryModule::AssetCreated(Mesh);
    Mesh->MarkPackageDirty();
    AStaticMeshActor *Actor = Source.BakedActor.Get();
    if (!Actor)
    {
        FActorSpawnParameters Params;
        Params.OverrideLevel = Source.GetLevel();
        Params.ObjectFlags |= RF_Transactional;
        Actor = Source.GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
                                                                Source.GetActorTransform(), Params);
    }
    if (!Actor)
    {
        Message = TEXT("Could not place baked mesh in the source level.");
        return false;
    }
    Source.Modify();
    Actor->Modify();
    Actor->SetActorLabel(Source.GetActorLabel() + TEXT("_Baked"));
    auto *Comp = Actor->GetStaticMeshComponent();
    Comp->Modify();
    Comp->SetMobility(EComponentMobility::Movable);
    Actor->SetActorTransform(Source.GetActorTransform());
    Comp->SetStaticMesh(Mesh);
    Comp->SetMobility(EComponentMobility::Static);
    Comp->SetCollisionProfileName(Source.Collision == EPatchCollision::None ? TEXT("NoCollision") : TEXT("BlockAll"));
    Actor->SetIsTemporarilyHiddenInEditor(false);
    Source.BakedActor = Actor;
    Source.bNeedsBake = false;
    Source.Preview->SetVisibility(false);
    Source.Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Source.MarkPackageDirty();
    Actor->MarkPackageDirty();
    Message = FString::Printf(TEXT("Baked %d triangles. Save All to persist the mesh and level."), T.Num());
    return true;
}
