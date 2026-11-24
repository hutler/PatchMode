#pragma once
#include "EdMode.h"
#include "PatchCommands.h"
#include "PatchSettings.h"
#include "PatchSurface.h"
#include "ScopedTransaction.h"

class FPatchEdMode : public FEdMode
{
  public:
    static const FName Id;
    TStrongObjectPtr<UPatchSettings> Options;
    FText Status;
    EPatchCommand Tool = EPatchCommand::Select;
    TSet<int32> Points;
    TWeakObjectPtr<APatchSurface> SelectionOwner;
    int32 Focus = 0;
    int32 Projection = 0;

    FPatchEdMode();
    virtual void Enter() override;
    virtual void Exit() override;
    virtual bool UsesToolkits() const override
    {
        return true;
    }
    virtual void Render(const FSceneView *, FViewport *, FPrimitiveDrawInterface *) override;
    virtual bool HandleClick(FEditorViewportClient *, HHitProxy *, const FViewportClick &) override;
    virtual bool InputKey(FEditorViewportClient *, FViewport *, FKey, EInputEvent) override;
    virtual bool MouseMove(FEditorViewportClient *, FViewport *, int32, int32) override;
    virtual bool LostFocus(FEditorViewportClient *, FViewport *) override;
    virtual bool CapturedMouseMove(FEditorViewportClient *, FViewport *, int32, int32) override;
    virtual bool StartTracking(FEditorViewportClient *, FViewport *) override;
    virtual bool EndTracking(FEditorViewportClient *, FViewport *) override;
    virtual bool InputDelta(FEditorViewportClient *, FViewport *, FVector &, FRotator &, FVector &) override;
    virtual FVector GetWidgetLocation() const override;
    virtual bool UsesTransformWidget() const override;
    virtual void PostUndo() override;
    virtual void SelectionChanged() override;
    virtual bool BoxSelect(FBox &, bool) override;
    APatchSurface *Active() const;
    TArray<APatchSurface *> SelectedPatches() const;
    void SelectPatch(APatchSurface *);
    APatchSurface *Spawn(const FPatchGrid &, const FTransform &, const FString &, UMaterialInterface * = nullptr);
    void Execute(EPatchCommand);
    void ActivateTool(EPatchCommand);
    FText CommandHint(EPatchCommand) const;
    void ConfirmPreview();
    void ReverseSeam();
    bool CanConfirm() const;
    bool HasSeamPair() const;
    static TOptional<EPatchPrimitive> Primitive(EPatchCommand);
    virtual void DrawHUD(FEditorViewportClient *, FViewport *, const FSceneView *, FCanvas *) override;
    void BeginBrushStroke();
    bool BrushRay(FVector Origin, FVector Direction, double Drag);
    void EndBrushStroke();
    bool CanExecute(EPatchCommand) const;
    void SelectPoint(APatchSurface *, int32, bool);
    void SetStatus(const FString &S)
    {
        Status = FText::FromString(S);
    }

  private:
    struct FBoundary
    {
        TWeakObjectPtr<APatchSurface> Patch;
        int32 Side = INDEX_NONE;
        bool Valid() const
        {
            return Patch.IsValid() && Side >= 0 && Side < 4 && Patch->Grid.Valid();
        }
    };
    FBoundary HoverEdge, ExtrudeEdge, SeamFrom, SeamTo;
    TWeakObjectPtr<APatchSurface> HoverPatch;
    int32 HoverPoint = INDEX_NONE, HoverAxis = INDEX_NONE, DragAxis = INDEX_NONE;
    bool bConsumeClick = false, bPointPressed = false, bEdgeDrag = false, bReverseSeam = false;
    double DragAmount = 0;
    FVector HandleOrigin = FVector::ZeroVector, HandleDirection = FVector::ZeroVector;
    FVector2D DragPixel = FVector2D::ZeroVector, DragScreenAxis = FVector2D::ZeroVector;
    TSet<int32> PreDragPoints;
    int32 PreDragFocus = 0;
    int32 PlaceStage = 0;
    FVector PlaceBase = FVector::ZeroVector, PlaceEnd = FVector::ZeroVector;
    FQuat PlaceRotation = FQuat::Identity;
    bool bPlaceHit = false;
    FString NumericInput;
    void ResetInteraction(bool bCancel);
    void UpdateHover(FEditorViewportClient *, FViewport *, int32, int32);
    bool PointerKey(FEditorViewportClient *, FViewport *, EInputEvent);
    void MoveHandle(FEditorViewportClient *, FViewport *, int32, int32);
    void RenderInteraction(const FSceneView *, FPrimitiveDrawInterface *);
    void GetExtrudeHandle(const APatchSurface &, int32, FVector &, FVector &) const;
    void PickBoundary();
    void PlacementClick();
    void UpdatePlacement(const FSceneView *, FEditorViewportClient *, FIntPoint);
    bool BuildPlacement(FPatchGrid &, FTransform &) const;
    void RenderBrush(FPrimitiveDrawInterface *) const;
    double BrushWeight(double Distance) const;
    void TransformPoints(const FVector &, const FRotator &, const FVector &);
    bool ApplySeam(APatchSurface &, const TArray<int32> &, APatchSurface &, const TArray<int32> &);
    TUniquePtr<FScopedTransaction> Gesture;
    bool bPainting = false, bDrawing = false;
    FVector DrawOrigin = FVector::ZeroVector, DrawNormal = FVector::UpVector;
    TOptional<FVector> Cursor;
    int32 LastMouseY = 0;
    TMap<TWeakObjectPtr<APatchSurface>, TPair<FPatchGrid, bool>> Touched;
    void Touch(APatchSurface *);
    void FinishGesture(bool bCancel = false);
    bool BrushAt(FEditorViewportClient *, FViewport *, double);
    bool ApplyStamp(APatchSurface *, FVector, double);
    void Join(EPatchCommand, APatchSurface &);
    void SetConnectedSubdivisions(APatchSurface &, int32);
    void FaceToTerrain(FEditorViewportClient *, const FViewportClick &);
    void ReadPoint();
};
