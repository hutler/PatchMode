#include "PatchToolkit.h"
#include "IDetailsView.h"
#include "ISinglePropertyView.h"
#include "PatchEdMode.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

FEdMode *FPatchToolkit::GetEditorMode() const
{
    return Mode;
}

// FModeToolkit wraps the inline panel in a scroll box when it attaches it to the tab.
static void RevealSection(const TSharedPtr<SExpandableArea> &Section)
{
    if (!Section)
        return;
    Section->SetExpanded(true);
    // SScrollBox inherits SCompoundWidget's StaticWidgetClass in UE 5.8.
    static const FName ScrollBoxType(TEXT("SScrollBox"));
    for (auto Parent = Section->GetParentWidget(); Parent; Parent = Parent->GetParentWidget())
    {
        if (Parent->GetType() == ScrollBoxType)
        {
            const auto ScrollBox = StaticCastSharedPtr<SScrollBox>(Parent);
            ScrollBox->EndInertialScrolling();
            ScrollBox->ScrollDescendantIntoView(Section, false, EDescendantScrollDestination::TopOrLeft);
            break;
        }
    }
}

void FPatchToolkit::OpenUV()
{
    RevealSection(UVArea);
}
void FPatchToolkit::ChooseTool(EPatchCommand Command)
{
    Mode->ActivateTool(Command);
    using C = EPatchCommand;
    if (Mode->Primitive(Command) || Command == C::ConnectEdges || Command == C::Extrude || Command == C::Sculpt ||
        Command == C::Paint)
        RevealSection(ContextArea);
}

void FPatchToolkit::Build()
{
    auto List = SNew(SVerticalBox);
    auto AddGroup = [&](const FString &Group, const TSharedRef<SWidget> &Body, bool Open) {
        TSharedPtr<SExpandableArea> Area;
        List->AddSlot().AutoHeight().Padding(1, 2)[SAssignNew(Area, SExpandableArea)
                                                       .InitiallyCollapsed(!Open)
                                                       .HeaderContent()[SNew(STextBlock).Text(FText::FromString(Group))]
                                                       .BodyContent()[Body]];
        if (Group == TEXT("Active Tool"))
            ContextArea = Area;
        if (Group == TEXT("UV"))
            UVArea = Area;
    };
    using C = EPatchCommand;
    auto Context = SNew(SVerticalBox);
    auto &Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    auto AddProperty = [&](FName Name, TFunction<bool()> Visible) {
        auto Row = Properties.CreateSingleProperty(Mode->Options.Get(), Name, FSinglePropertyParams());
        if (Row)
            Context->AddSlot().AutoHeight().Padding(3, 1)[SNew(SBox).Visibility_Lambda(
                [Visible] { return Visible() ? EVisibility::Visible : EVisibility::Collapsed; })[Row.ToSharedRef()]];
    };
    auto Brush = [this] { return Mode->Tool == C::Sculpt || Mode->Tool == C::Paint; };
    auto Extrude = [this] { return Mode->Tool == C::Extrude; };
    auto Place = [this] { return FPatchEdMode::Primitive(Mode->Tool).IsSet(); };
    for (FName Property : {FName("Brush"), FName("Radius"), FName("InnerRadius"), FName("Strength"), FName("Falloff"),
                           FName("bOnlySelected")})
        AddProperty(Property, Brush);
    AddProperty("Height", [this] { return Mode->Tool == C::Sculpt; });
    AddProperty("Color", [this] { return Mode->Tool == C::Paint; });
    AddProperty("Pick", [this] { return Mode->Tool == C::Select; });
    AddProperty("Edge", [this] { return Mode->Tool == C::Extrude || Mode->Tool == C::Paint; });
    for (FName Property : {FName("ExtrudeLength"), FName("ExtrudeDirection")})
        AddProperty(Property, Extrude);
    AddProperty("FeatherWidth", [this] { return Mode->Tool == C::Paint; });
    Context->AddSlot().AutoHeight()[SNew(SButton)
        .Text(FText::FromString(TEXT("Feather Edge Alpha")))
        .Visibility_Lambda([this] { return Mode->Tool == C::Paint ? EVisibility::Visible : EVisibility::Collapsed; })
        .IsEnabled_Lambda([this] { return Mode->CanExecute(C::FeatherAlpha); })
        .ToolTipText_Lambda([this] { return Mode->CommandHint(C::FeatherAlpha); })
        .OnClicked_Lambda([this] { Mode->Execute(C::FeatherAlpha); return FReply::Handled(); })];
    for (FName Property : {FName("Size"), FName("Rows"), FName("Columns")})
        AddProperty(Property, Place);
    for (FName Property : {FName("RopeRadius"), FName("RopeSagPercent")})
        AddProperty(Property, [this] { return Mode->Tool == C::Rope; });
    Context->AddSlot().AutoHeight()[SNew(SButton)
                                        .Text(FText::FromString(TEXT("Create from selection / settings")))
                                        .Visibility_Lambda(
                                            [Place] { return Place() ? EVisibility::Visible : EVisibility::Collapsed; })
                                        .ToolTipText(FText::FromString(TEXT(
                                            "Use selected bounds or rope target actors and the creation settings.")))
                                        .OnClicked_Lambda([this] {
                                            Mode->Execute(Mode->Tool);
                                            return FReply::Handled();
                                        })];
    Context->AddSlot().AutoHeight()[SNew(SButton)
                                        .Text(FText::FromString(TEXT("Reverse pairing (R)")))
                                        .Visibility_Lambda([this] {
                                            return Mode->HasSeamPair() ? EVisibility::Visible : EVisibility::Collapsed;
                                        })
                                        .OnClicked_Lambda([this] {
                                            Mode->ReverseSeam();
                                            return FReply::Handled();
                                        })];
    Context->AddSlot().AutoHeight()[SNew(SButton)
                                        .Text_Lambda([this] {
                                            return FText::FromString(
                                                Mode->Tool == C::Extrude        ? TEXT("Extrude by length")
                                                : Mode->Tool == C::ConnectEdges ? TEXT("Connect edges (Enter)")
                                                                                : TEXT("Confirm step (Enter)"));
                                        })
                                        .Visibility_Lambda([this, Place, Extrude] {
                                            return (Place() || Extrude() || Mode->Tool == C::ConnectEdges)
                                                       ? EVisibility::Visible
                                                       : EVisibility::Collapsed;
                                        })
                                        .IsEnabled_Lambda([this] { return Mode->CanConfirm(); })
                                        .OnClicked_Lambda([this] {
                                            Mode->ConfirmPreview();
                                            return FReply::Handled();
                                        })];
    AddGroup(TEXT("Active Tool"), Context, true);
    TSet<C> Shortcuts;
    Shortcuts.Add(C::FeatherAlpha);
    auto QuickGroup = [&](const TCHAR *Title, std::initializer_list<TPair<C, const TCHAR *>> Tiles) {
        auto Grid = SNew(SGridPanel);
        Grid->SetColumnFill(0, 1);
        Grid->SetColumnFill(1, 1);
        int32 Index = 0;
        for (const auto &Item : Tiles)
        {
            const C Command = Item.Key;
            Shortcuts.Add(Command);
            const TCHAR *Label = TEXT("");
            for (const auto &Entry : PatchCommands)
                if (Entry.Id == Command)
                    Label = Entry.Label;
            if (Command == C::WeldPoints)
                Label = TEXT("Weld");
            Grid->AddSlot(Index % 2, Index / 2)
                .Padding(1)[SNew(SButton)
                                .HAlign(HAlign_Fill)
                                .ContentPadding(FMargin(3, 4))
                                .ToolTipText_Lambda([this, Command] { return Mode->CommandHint(Command); })
                                .IsEnabled_Lambda([this, Command] { return Mode->CanExecute(Command); })
                                .ButtonColorAndOpacity_Lambda([this, Command] {
                                    return Mode->Tool == Command ? FLinearColor(.04f, .32f, .65f) : FLinearColor::White;
                                })
                                .OnClicked_Lambda([this, Command] {
                                    ChooseTool(Command);
                                    return FReply::Handled();
                                })[SNew(SVerticalBox) +
                                   SVerticalBox::Slot().AutoHeight().HAlign(
                                       HAlign_Center)[SNew(SImage)
                                                          .DesiredSizeOverride(FVector2D(18, 18))
                                                          .Image(FAppStyle::GetBrush(Item.Value))] +
                                   SVerticalBox::Slot().AutoHeight().Padding(1, 2).HAlign(
                                       HAlign_Fill)[SNew(SScaleBox)
                                                        .Stretch(EStretch::ScaleToFitX)
                                                        .StretchDirection(EStretchDirection::DownOnly)
                                                            [SNew(STextBlock)
                                                                 .Justification(ETextJustify::Center)
                                                                 .Text(FText::FromString(Label))]]]];
            ++Index;
        }
        AddGroup(Title, Grid, true);
    };
    QuickGroup(TEXT("Create"),
               {{C::Terrain, TEXT("LevelEditor.Tabs.Modes")}, {C::Curve, TEXT("CurveAssetEditor.Tabs.Properties")}});
    QuickGroup(TEXT("Edit"), {{C::Select, TEXT("MeshPaint.SelectVertex")},
                              {C::Extrude, TEXT("EditorViewport.TranslateMode")},
                              {C::WeldPoints, TEXT("Icons.EditHierarchy")},
                              {C::Split, TEXT("Icons.SelectInViewport")},
                              {C::TurnEdge, TEXT("EditorViewport.RotateMode")}});
    QuickGroup(TEXT("Surface"), {{C::Sculpt, TEXT("LandscapeEditor.SculptTool")},
                                 {C::Paint, TEXT("LevelEditor.MeshPaintMode")},
                                 {C::InspectUV, TEXT("LevelEditor.MeshPaintMode.TexturePaint")}});
    for (const TCHAR *Group :
         {TEXT("Create"), TEXT("Primitives"), TEXT("Edit"), TEXT("Join"), TEXT("Surface"), TEXT("UV"), TEXT("Output")})
    {
        auto Grid = SNew(SGridPanel);
        int32 I = 0;
        for (const auto &Entry : PatchCommands)
        {
            if (FCString::Strcmp(Group, Entry.Group) || Shortcuts.Contains(Entry.Id))
                continue;
            const auto Id = Entry.Id;
            Grid->SetColumnFill(0, 1);
            Grid->SetColumnFill(1, 1);
            Grid->AddSlot(I % 2, I / 2)
                .Padding(1)[SNew(SButton)
                                .Text(FText::FromString(Entry.Label))
                                .HAlign(HAlign_Center)
                                .ButtonColorAndOpacity_Lambda([this, Id] {
                                    return Mode->Tool == Id ? FLinearColor(.04f, .32f, .65f) : FLinearColor::White;
                                })
                                .ToolTipText_Lambda([this, Id] { return Mode->CommandHint(Id); })
                                .IsEnabled_Lambda([this, Id] { return Mode->CanExecute(Id); })
                                .OnClicked_Lambda([this, Id] {
                                    ChooseTool(Id);
                                    return FReply::Handled();
                                })];
            ++I;
        }
        const FString Heading = FString(Group) == TEXT("Create")    ? TEXT("Create from Geometry")
                                : FString(Group) == TEXT("Edit")    ? TEXT("Topology & Selection")
                                : FString(Group) == TEXT("Surface") ? TEXT("Vertex Inspector")
                                                                    : Group;
        AddGroup(Heading, Grid, false);
    }
    FDetailsViewArgs Args;
    Args.bHideSelectionTip = true;
    Args.bAllowSearch = true;
    Args.bUpdatesFromSelection = false;
    Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    auto Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(Args);
    Details->SetObject(Mode->Options.Get());
    AddGroup(TEXT("All Settings"), Details, false);
    List->AddSlot().AutoHeight().Padding(
        6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return Mode->Status; })];
    Panel = List;
}
