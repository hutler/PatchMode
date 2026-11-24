#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PatchSettings.generated.h"

UENUM()
enum class EPatchPick : uint8
{
    Point,
    Row,
    Column,
    Cell,
    Surface
};
UENUM()
enum class EPatchWorkAxis : uint8
{
    Rows,
    Columns
};
UENUM()
enum class EPatchBrush : uint8
{
    Height,
    Flatten,
    Smooth,
    Noise,
    Drag,
    Grab,
    PaintRGB,
    PaintAlpha
};
UENUM()
enum class EPatchUVLayer : uint8
{
    Texture,
    Lightmap,
    Smoothing
};
UENUM()
enum class EPatchBoundary : uint8
{
    FirstRow,
    LastColumn,
    LastRow,
    FirstColumn
};
UENUM()
enum class EPatchExtrudeDirection : uint8
{
    Outward,
    WorldX,
    WorldY,
    WorldZ
};
UENUM()
enum class EPatchCap : uint8
{
    Standard,
    InsideBevel,
    OutsideBevel,
    InsideEndCap,
    OutsideEndCap
};

UCLASS()
class UPatchSettings : public UObject
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, Category = "Create", meta = (ClampMin = 2, ClampMax = 16))
    int32 Columns = 3;
    UPROPERTY(EditAnywhere, Category = "Create", meta = (ClampMin = 2, ClampMax = 16))
    int32 Rows = 3;
    UPROPERTY(EditAnywhere, Category = "Create")
    FVector Size = FVector(400, 400, 240);
    UPROPERTY(EditAnywhere, Category = "Create", meta = (ClampMin = 1))
    double RopeRadius = 8;
    UPROPERTY(EditAnywhere, Category = "Create", meta = (ClampMin = 0))
    double RopeSagPercent = 10;
    UPROPERTY(EditAnywhere, Category = "Create")
    bool bReplaceSourceBrush = false;
    UPROPERTY(EditAnywhere, Category = "Create")
    bool bRemoveRopeTargetPoints = false;
    UPROPERTY(EditAnywhere, Category = "Selection")
    EPatchPick Pick = EPatchPick::Point;
    UPROPERTY(EditAnywhere, Category = "Selection")
    bool bDrillDown = false;
    UPROPERTY(EditAnywhere, Category = "Selection")
    bool bSoftSelection = false;
    UPROPERTY(EditAnywhere, Category = "Selection")
    bool bMoveCoincident = false;
    UPROPERTY(EditAnywhere, Category = "Selection")
    bool bShowGrid = true;
    UPROPERTY(EditAnywhere, Category = "Selection")
    bool bShowTriangles = false;
    UPROPERTY(EditAnywhere, Category = "Selection")
    bool bRadiantKeys = false;
    UPROPERTY(EditAnywhere, Category = "Topology")
    EPatchWorkAxis Axis = EPatchWorkAxis::Rows;
    UPROPERTY(EditAnywhere, Category = "Topology")
    EPatchBoundary Edge = EPatchBoundary::FirstRow;
    UPROPERTY(EditAnywhere, Category = "Topology", meta = (Units = "cm"))
    double ExtrudeLength = 100;
    UPROPERTY(EditAnywhere, Category = "Topology")
    EPatchExtrudeDirection ExtrudeDirection = EPatchExtrudeDirection::Outward;
    UPROPERTY(EditAnywhere, Category = "Topology", meta = (ClampMin = 0.001))
    double WeldTolerance = 2;
    UPROPERTY(EditAnywhere, Category = "Topology")
    bool bTolerantSnap = false;
    UPROPERTY(EditAnywhere, Category = "Topology")
    double Thickness = 16;
    UPROPERTY(EditAnywhere, Category = "Topology")
    bool bCreateSeams = true;
    UPROPERTY(EditAnywhere, Category = "Topology")
    EPatchCap CapShape = EPatchCap::Standard;
    UPROPERTY(EditAnywhere, Category = "Point")
    FVector Position = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, Category = "Point")
    FLinearColor Color = FLinearColor::White;
    UPROPERTY(EditAnywhere, Category = "Brush")
    EPatchBrush Brush = EPatchBrush::Height;
    UPROPERTY(EditAnywhere, Category = "Brush", meta = (ClampMin = 0.1))
    double Radius = 175;
    UPROPERTY(EditAnywhere, Category = "Brush", meta = (ClampMin = 0, ClampMax = 1))
    double Strength = .35;
    UPROPERTY(EditAnywhere, Category = "Brush", meta = (ClampMin = 0.1))
    double Falloff = 1;
    UPROPERTY(EditAnywhere, Category = "Brush")
    double Height = 20;
    UPROPERTY(EditAnywhere, Category = "Brush", meta = (ClampMin = 0))
    double InnerRadius = 0;
    UPROPERTY(EditAnywhere, Category = "Brush")
    bool bRed = true;
    UPROPERTY(EditAnywhere, Category = "Brush")
    bool bGreen = true;
    UPROPERTY(EditAnywhere, Category = "Brush")
    bool bBlue = true;
    UPROPERTY(EditAnywhere, Category = "Brush")
    bool bOnlySelected = false;
    UPROPERTY(EditAnywhere, Category = "Brush")
    int32 NoiseSeed = 137;
    UPROPERTY(EditAnywhere, Category = "UV")
    EPatchUVLayer UVLayer = EPatchUVLayer::Texture;
    UPROPERTY(EditAnywhere, Category = "UV")
    FVector2D UVValue = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, Category = "UV")
    FVector2D UVShift = FVector2D(.125, 0);
    UPROPERTY(EditAnywhere, Category = "UV")
    FVector2D UVScale = FVector2D(1, 1);
    UPROPERTY(EditAnywhere, Category = "UV")
    double Rotation = 90;
    UPROPERTY(EditAnywhere, Category = "UV", meta = (ClampMin = 0.01))
    double UnitsPerTile = 100;
};
