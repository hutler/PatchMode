#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PatchSurface.generated.h"

class UDynamicMeshComponent;
UENUM()
enum class EPatchPreview : uint8
{
    Material,
    VertexRGB,
    VertexAlpha
};
UENUM()
enum class EPatchCollision : uint8
{
    SurfaceTriangles,
    ConvexHull,
    None
};
class UMaterialInterface;
class AStaticMeshActor;

UENUM()
enum class EPatchKind : uint8
{
    Terrain,
    Curve
};

UENUM()
enum class EPatchPrimitive : uint8
{
    Terrain,
    Curve,
    Bevel,
    SquareBevel,
    EndCap,
    SquareEndCap,
    Cylinder,
    DenseCylinder,
    VeryDenseCylinder,
    SquareCylinder,
    Cone,
    Rope
};

USTRUCT()
struct FPatchPoint
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category = "Point")
    FVector Position = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, Category = "Point")
    FVector2D UV = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, Category = "Point")
    FVector2D LightUV = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, Category = "Point")
    FVector2D SmoothingUV = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, Category = "Point")
    FLinearColor Color = FLinearColor::White;
    UPROPERTY(EditAnywhere, Category = "Point")
    bool bLocked = false;
};

USTRUCT()
struct FPatchGrid
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category = "Surface")
    EPatchKind Kind = EPatchKind::Terrain;
    UPROPERTY(VisibleAnywhere, Category = "Surface")
    int32 Width = 0;
    UPROPERTY(VisibleAnywhere, Category = "Surface")
    int32 Height = 0;
    UPROPERTY(EditAnywhere, Category = "Surface", meta = (ClampMin = 1, ClampMax = 32))
    int32 Subdivisions = 4;
    UPROPERTY()
    TArray<FPatchPoint> Points;
    UPROPERTY()
    TArray<uint8> Turned;
    UPROPERTY()
    bool bInverted = false;

    int32 Index(int32 X, int32 Y) const
    {
        return Y * Width + X;
    }
    int32 Cell(int32 X, int32 Y) const
    {
        return Y * (Width - 1) + X;
    }
    bool Valid() const;
    FPatchPoint &At(int32 X, int32 Y)
    {
        return Points[Index(X, Y)];
    }
    const FPatchPoint &At(int32 X, int32 Y) const
    {
        return Points[Index(X, Y)];
    }
    static FPatchGrid Make(EPatchPrimitive Shape, int32 W, int32 H, FVector Size, double Sag,
                           FVector SagDirection = -FVector::UpVector);
    FPatchPoint Sample(double U, double V) const;
    void Tessellate(TArray<FPatchPoint> &Vertices, TArray<FIntVector> &Triangles) const;
};

UCLASS(NotBlueprintable)
class PATCHMODE_API APatchSurface : public AActor
{
    GENERATED_BODY()
  public:
    APatchSurface();
    UPROPERTY(VisibleAnywhere, Category = "Patch")
    FPatchGrid Grid;
    UPROPERTY(EditAnywhere, Category = "Patch")
    TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(VisibleAnywhere, Category = "Patch")
    TObjectPtr<UDynamicMeshComponent> Preview;
    UPROPERTY(EditAnywhere, Category = "Patch")
    EPatchPreview Display = EPatchPreview::Material;
    UPROPERTY(EditAnywhere, Category = "Bake")
    EPatchCollision Collision = EPatchCollision::SurfaceTriangles;
    UFUNCTION(CallInEditor, Category = "Bake")
    void BakeStaticMesh();
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditImport() override;
    UPROPERTY(VisibleInstanceOnly, Category = "Bake")
    TSoftObjectPtr<AStaticMeshActor> BakedActor;
    UPROPERTY(VisibleInstanceOnly, Category = "Bake")
    bool bNeedsBake = true;
    UPROPERTY()
    FGuid SourceId;

    void Rebuild();
    void Changed();
    virtual void OnConstruction(const FTransform &Transform) override;
    virtual void PostEditUndo() override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent &Event) override;
    virtual void PostDuplicate(EDuplicateMode::Type Mode) override;
    virtual bool IsEditorOnly() const override
    {
        return true;
    }
};
