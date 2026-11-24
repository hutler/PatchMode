#include "PatchSurface.h"
#include "PatchBake.h"
#include "ScopedTransaction.h"
#include "Engine/Engine.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/Material.h"

using namespace UE::Geometry;

APatchSurface::APatchSurface()
{
    Preview = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("PatchPreview"));
    SetRootComponent(Preview);
    Preview->SetMobility(EComponentMobility::Movable);
    Preview->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Preview->SetCollisionResponseToAllChannels(ECR_Ignore);
    Preview->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Preview->SetComplexAsSimpleCollisionEnabled(true, false);
    Preview->SetHiddenInGame(true);
    bIsEditorOnlyActor = true;
}

void APatchSurface::Rebuild()
{
    TArray<FPatchPoint> Vertices;
    TArray<FIntVector> Faces;
    Grid.Tessellate(Vertices, Faces);
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    auto *Attr = Mesh.Attributes();
    Attr->SetNumUVLayers(3);
    Attr->EnablePrimaryColors();
    for (const FPatchPoint &P : Vertices)
    {
        Mesh.AppendVertex(P.Position);
        Attr->GetUVLayer(0)->AppendElement(FVector2f(P.UV));
        Attr->GetUVLayer(1)->AppendElement(FVector2f(P.LightUV));
        Attr->GetUVLayer(2)->AppendElement(FVector2f(P.SmoothingUV));
        Attr->PrimaryColors()->AppendElement(FVector4f(P.Color.R, P.Color.G, P.Color.B, P.Color.A));
    }
    for (const FIntVector &Face : Faces)
    {
        const FIndex3i Tri(Face.X, Face.Y, Face.Z);
        const int32 ID = Mesh.AppendTriangle(Tri);
        if (ID < 0)
            continue;
        Attr->GetUVLayer(0)->SetTriangle(ID, Tri);
        Attr->GetUVLayer(1)->SetTriangle(ID, Tri);
        Attr->GetUVLayer(2)->SetTriangle(ID, Tri);
        Attr->PrimaryColors()->SetTriangle(ID, Tri);
    }
    FMeshNormals::InitializeOverlayToPerVertexNormals(Attr->PrimaryNormals(), false);
    Preview->SetMesh(MoveTemp(Mesh));
    UMaterialInterface *Shown = Material ? Material.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
    if (GEngine && Display == EPatchPreview::VertexRGB)
        Shown = GEngine->VertexColorViewModeMaterial_ColorOnly;
    if (GEngine && Display == EPatchPreview::VertexAlpha)
        Shown = GEngine->VertexColorViewModeMaterial_AlphaAsColor;
    Preview->SetMaterial(0, Shown);
    const bool PreviewVisible = bNeedsBake || !BakedActor.IsValid() || Display != EPatchPreview::Material;
    Preview->SetVisibility(PreviewVisible);
    Preview->SetCollisionEnabled(PreviewVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    if (auto *Baked = BakedActor.Get())
        Baked->SetIsTemporarilyHiddenInEditor(PreviewVisible);
}

void APatchSurface::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    if (bFinished)
        Changed();
}
void APatchSurface::BakeStaticMesh()
{
    const FScopedTransaction Tx(FText::FromString(TEXT("Bake patch to static mesh")));
    FString Message;
    PatchBake::Bake(*this, Message);
    UE_LOG(LogTemp, Display, TEXT("%s"), *Message);
}

void APatchSurface::Changed()
{
    bNeedsBake = true;
    Rebuild();
    MarkPackageDirty();
}

void APatchSurface::OnConstruction(const FTransform &Transform)
{
    Super::OnConstruction(Transform);
    if (!SourceId.IsValid())
        SourceId = FGuid::NewGuid();
    if (!Grid.Valid())
        Grid = FPatchGrid::Make(EPatchPrimitive::Terrain, 2, 2, FVector(400), 0);
    Rebuild();
}
void APatchSurface::PostEditUndo()
{
    Super::PostEditUndo();
    Rebuild();
}
void APatchSurface::PostEditChangeProperty(FPropertyChangedEvent &Event)
{
    Super::PostEditChangeProperty(Event);
    if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(APatchSurface, Display))
        Rebuild();
    else
        Changed();
}
void APatchSurface::PostEditImport()
{
    Super::PostEditImport();
    SourceId = FGuid::NewGuid();
    BakedActor.Reset();
    bNeedsBake = true;
}
void APatchSurface::PostDuplicate(EDuplicateMode::Type Mode)
{
    Super::PostDuplicate(Mode);
    if (Mode == EDuplicateMode::Normal)
    {
        SourceId = FGuid::NewGuid();
        BakedActor.Reset();
        bNeedsBake = true;
    }
}
