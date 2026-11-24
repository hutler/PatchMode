#pragma once
#include "PatchSurface.h"

enum class EPatchAxis
{
    Columns,
    Rows
};
namespace PatchOps
{
TArray<int32> EdgeIndices(const FPatchGrid &, int32 Side);
bool ExtrudeBoundary(FPatchGrid &, int32 Side, const FTransform &, FVector WorldOffset);
int32 FeatherEdgeAlpha(FPatchGrid &Grid, int32 Side, const FTransform &Transform, double Width,
                       const TSet<int32> *Selection);
FPatchPoint Blend(const FPatchPoint &A, const FPatchPoint &B, double T);
FPatchGrid Section(const FPatchGrid &Grid, int32 X, int32 Y, int32 Width, int32 Height);
void Transpose(FPatchGrid &Grid);
bool Resize(FPatchGrid &Grid, EPatchAxis Axis, int32 Index, bool bInsert, bool bExtrude);
void Redistribute(FPatchGrid &Grid, EPatchAxis Axis);
bool Split(FPatchGrid &Grid, EPatchAxis Axis, int32 Index, FPatchGrid &Other);
FPatchGrid ToTerrain(const FPatchGrid &Grid, int32 Width, int32 Height);
TArray<FPatchGrid> Thicken(const FPatchGrid &Grid, double Distance, bool bSeams);
FPatchGrid Cap(const FPatchGrid &Grid, int32 Side, bool bInside, bool bEndCap);
FVector PointNormal(const FPatchGrid &Grid, int32 X, int32 Y);
} // namespace PatchOps
