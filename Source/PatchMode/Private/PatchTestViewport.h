#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "LevelEditorViewport.h"

inline FLevelEditorViewportClient *PatchTestViewport()
{
    FLevelEditorViewportClient *Largest = nullptr;
    for (auto *Client : GEditor->GetLevelViewportClients())
    {
        if (!Client || !Client->Viewport || Client->Viewport->GetSizeXY().GetMin() <= 1)
            continue;
        if (Client->Viewport == GEditor->GetActiveViewport())
            return Client;
        if (!Largest || Client->Viewport->GetSizeXY().SizeSquared() > Largest->Viewport->GetSizeXY().SizeSquared())
            Largest = Client;
    }
    return Largest;
}

struct FPatchTestCamera
{
    FLevelEditorViewportClient &Client;
    FViewportCameraTransform Saved;

    FPatchTestCamera(FLevelEditorViewportClient &InClient, FVector Eye, FVector Target)
        : Client(InClient), Saved(Client.GetViewTransform())
    {
        if (Client.IsOrtho())
        {
            Client.SetViewLocation(Target - Client.GetForwardVector() * 1000.);
            const double Span = Client.GetOrthoUnitsPerPixel(Client.Viewport) * Client.Viewport->GetSizeXY().GetMin();
            Client.SetOrthoZoom(Client.GetOrthoZoom() * 700. / Span);
        }
        else
        {
            Client.SetViewLocation(Eye);
            Client.SetViewRotation((Target - Eye).Rotation());
        }
    }

    ~FPatchTestCamera()
    {
        Client.GetViewTransform() = Saved;
        Client.Invalidate();
    }
};
#endif
