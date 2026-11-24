#pragma once

#include "EditorViewportClient.h"
#include "Math/Ray.h"
#include "SceneView.h"

// Flat patches use local +Z as their front face and sculpt direction.
inline FQuat PatchPlaneRotation(const FEditorViewportClient &Client)
{
    return Client.IsOrtho() ? FQuat::FindBetweenNormals(FVector::UpVector, -Client.GetForwardVector())
                            : FQuat::Identity;
}

inline FRay PatchCursorRay(const FSceneView &View, FEditorViewportClient &Client, FIntPoint Pixel)
{
    if (Client.IsOrtho())
    {
        // FViewportCursorLocation starts halfway through the ortho depth range. A visible patch
        // can be on either side of that point. Reversed-Z depth 1 is the visible near plane.
        const FVector4 Near = View.ScreenToWorld(View.CursorToScreen(Pixel.X, Pixel.Y, 1.f));
        return FRay(FVector(Near) / Near.W, View.GetViewDirection(), true);
    }
    const FViewportCursorLocation Cursor(&View, &Client, Pixel.X, Pixel.Y);
    return FRay(Cursor.GetOrigin(), Cursor.GetDirection(), true);
}

inline TOptional<FRay> PatchCursorRay(FEditorViewportClient &Client, FIntPoint Pixel)
{
    FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Client.Viewport, Client.GetScene(), Client.EngineShowFlags)
                                       .SetRealtimeUpdate(Client.IsRealtime()));
    if (const FSceneView *View = Client.CalcSceneView(&Family))
        return PatchCursorRay(*View, Client, Pixel);
    return {};
}
