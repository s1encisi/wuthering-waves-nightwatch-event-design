#include "LNCampaignWidget.h"
#include "LNCampaignGameMode.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

void ULNCampaignWidget::SceneCheck(bool Passed,const FString& Name)
{
    ++SceneCheckCount;if(!Passed)++SceneCheckFailures;
    SceneCheckText+=FString::Printf(TEXT("%s %s\n"),Passed?TEXT("PASS"):TEXT("FAIL"),*Name);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);
    SceneCheckRows.Add(MakeShared<FJsonValueObject>(Row));
}

void ULNCampaignWidget::InspectScene(const FString& Label)
{
    auto* Game=GetGame();UWorld* World=GetWorld();
    SceneCheck(World&&World->HasBegunPlay(),Label+TEXT(".world_has_begun_play"));
    FString ConfigText;TSharedPtr<FJsonObject> Config;int32 ExpectedWalls=INDEX_NONE,ExpectedRoads=0;
    if(FFileHelper::LoadFileToString(ConfigText,*(FPaths::ProjectContentDir()/TEXT("Campaign/Data/Campaign.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ConfigText),Config)&&Game)
    {
        const auto& Stages=Config->GetArrayField(TEXT("stages"));
        if(Stages.IsValidIndex(Game->GetSelectedStage()))
        {
            auto Definition=Stages[Game->GetSelectedStage()]->AsObject();ExpectedWalls=Definition->GetArrayField(TEXT("obstacles_m")).Num();
            for(const auto& Entry:Definition->GetArrayField(TEXT("entries")))ExpectedRoads+=FMath::Max(0,Entry->AsObject()->GetArrayField(TEXT("route_m")).Num()-1);
        }
    }
    SceneCheck(ExpectedWalls>=0&&ExpectedRoads>0,Label+TEXT(".authored_geometry_loaded"));
    TArray<AStaticMeshActor*> Actors;
    for(TActorIterator<AStaticMeshActor> It(World);It;++It)if(!It->IsActorBeingDestroyed())Actors.Add(*It);
#if WITH_EDITOR
    TArray<UStaticMesh*> Meshes;for(auto* Actor:Actors)if(UStaticMesh* Mesh=Actor->GetStaticMeshComponent()->GetStaticMesh())Meshes.AddUnique(Mesh);
    FStaticMeshCompilingManager::Get().FinishCompilation(Meshes);
#endif
    if(auto* Physics=World->GetPhysicsScene()){Physics->ProcessDeferredCreatePhysicsState();Physics->ProcessAsyncPhysicsStateJobs(true);}
    int32 MeshCount=0,MaterialCount=0,ProxyCount=0,GroundCount=0,RoadCount=0,WallCount=0,WallHits=0;
    TArray<TSharedPtr<FJsonValue>> Geometry;
    for(auto* Actor:Actors)
    {
        auto* Component=Actor->GetStaticMeshComponent();UStaticMesh* Mesh=Component->GetStaticMesh();auto* Material=Component->GetMaterial(0);
        const bool HasProxy=Component->IsRenderStateCreated()&&Component->GetSceneProxy()!=nullptr;
        MeshCount+=Mesh!=nullptr;MaterialCount+=Material!=nullptr;ProxyCount+=HasProxy;
        SceneCheck(Mesh!=nullptr,Label+TEXT(".mesh.")+Actor->GetName());
        SceneCheck(Material!=nullptr,Label+TEXT(".material.")+Actor->GetName());
        SceneCheck(Component->IsRegistered()&&HasProxy&&Component->IsVisible()&&!Actor->IsHidden(),Label+TEXT(".render_ready.")+Actor->GetName());
        const FVector Bounds=Component->Bounds.BoxExtent*2,Position=Actor->GetActorLocation();
        const FString MaterialName=GetNameSafe(Material);
        if(MaterialName==TEXT("MI_Concrete"))
        {
            ++GroundCount;
            const FVector Expected=Position.Equals(FVector(5000,-12000,-40),.1)?FVector(200000,200000,60):FVector(11000,7600,60);
            SceneCheck(Mesh&&Bounds.Equals(Expected,2),Label+TEXT(".ground_actual_bounds.")+Actor->GetName());
        }
        if(MaterialName==TEXT("MI_Road"))
        {++RoadCount;SceneCheck(Mesh&&FMath::IsNearlyEqual(Component->GetComponentScale().Y,6.,.001),Label+TEXT(".road_width.")+Actor->GetName());}
        if(MaterialName==TEXT("MI_Warehouse"))
        {
            ++WallCount;FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(CampaignSceneWall),false);
            const FVector From=Position-FVector(Component->Bounds.BoxExtent.X+100,0,0),To=Position+FVector(Component->Bounds.BoxExtent.X+100,0,0);
            const bool HitWall=World->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility,Query)&&Hit.GetComponent()==Component;
            WallHits+=HitWall;
            SceneCheck(Mesh&&Component->IsPhysicsStateCreated()&&Component->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block&&HitWall,Label+TEXT(".wall_world_trace.")+Actor->GetName());
        }
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("actor"),Actor->GetName());Item->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh));
        Item->SetStringField(TEXT("material"),GetPathNameSafe(Material));Item->SetStringField(TEXT("position"),Position.ToString());
        Item->SetStringField(TEXT("scale"),Actor->GetActorScale3D().ToString());Item->SetStringField(TEXT("bounds_dimensions"),Bounds.ToString());
        Item->SetNumberField(TEXT("mobility"),int32(Component->Mobility));Item->SetBoolField(TEXT("render_proxy"),HasProxy);
        Geometry.Add(MakeShared<FJsonValueObject>(Item));
    }
    SceneCheck(Actors.Num()>0&&MeshCount==Actors.Num(),Label+TEXT(".all_scenery_have_meshes"));
    SceneCheck(GroundCount==2&&RoadCount==ExpectedRoads&&WallCount==ExpectedWalls&&WallHits==WallCount,Label+TEXT(".ground_roads_and_authored_walls_exist"));
    auto Snapshot=MakeShared<FJsonObject>();Snapshot->SetStringField(TEXT("label"),Label);Snapshot->SetNumberField(TEXT("stage_index"),Game?Game->GetSelectedStage():-1);
    Snapshot->SetBoolField(TEXT("world_has_begun_play"),World->HasBegunPlay());Snapshot->SetNumberField(TEXT("mesh_actors"),Actors.Num());
    Snapshot->SetNumberField(TEXT("valid_meshes"),MeshCount);Snapshot->SetNumberField(TEXT("valid_materials"),MaterialCount);Snapshot->SetNumberField(TEXT("render_proxies"),ProxyCount);
    Snapshot->SetNumberField(TEXT("grounds"),GroundCount);Snapshot->SetNumberField(TEXT("roads"),RoadCount);Snapshot->SetNumberField(TEXT("walls"),WallCount);Snapshot->SetNumberField(TEXT("wall_trace_hits"),WallHits);
    Snapshot->SetArrayField(TEXT("geometry"),Geometry);
    TArray<TSharedPtr<FJsonValue>> PortraitRows;
    for(int32 Index=0;Index<PortraitFallbacks.Num();++Index)
    {
        UTextBlock* Fallback=PortraitFallbacks[Index];const bool Visible=Fallback->GetVisibility()!=ESlateVisibility::Collapsed;
        auto Item=MakeShared<FJsonObject>();Item->SetBoolField(TEXT("fallback_visible"),Visible);Item->SetStringField(TEXT("text"),Fallback->GetText().ToString());
        Item->SetStringField(TEXT("cached_size"),Fallback->GetCachedGeometry().GetLocalSize().ToString());Item->SetStringField(TEXT("desired_size"),Fallback->GetDesiredSize().ToString());
        if(Visible)SceneCheck(Fallback->GetCachedGeometry().GetLocalSize().X>=27&&Fallback->GetDesiredSize().Y<=52,Label+FString::Printf(TEXT(".fallback_two_line_layout_%d"),Index));
        PortraitRows.Add(MakeShared<FJsonValueObject>(Item));
    }
    Snapshot->SetArrayField(TEXT("portraits"),PortraitRows);
    const FString Screenshot=SceneCheckDirectory/(Label+TEXT(".png"));Snapshot->SetStringField(TEXT("screenshot"),Screenshot);
    SceneCheckSnapshots.Add(MakeShared<FJsonValueObject>(Snapshot));
    FScreenshotRequest::RequestScreenshot(Screenshot,true,false,false,FIntRect(),true);
}

void ULNCampaignWidget::TickSceneChecks(float DeltaSeconds)
{
    if(bSceneChecksFinished)return;SceneCheckDelay+=DeltaSeconds;++SceneCheckFrames;
    if(SceneCheckDelay<1.f||SceneCheckFrames<3)return;SceneCheckDelay=0;SceneCheckFrames=0;
    auto* Game=GetGame();if(!Game)return;
    auto Click=[&](ULNCampaignActionButton* Target,const TCHAR* Name)
    {SceneCheck(Target&&Target->GetIsEnabled(),Name);if(Target&&Target->GetIsEnabled()){Target->OnClicked.Broadcast();RefreshFromGame();}};
    auto Assign=[&](FName Id)
    {const int32 Index=Game->GetAvailableRosterIds().IndexOfByKey(Id);SceneCheck(RosterButtons.IsValidIndex(Index),TEXT("portrait_switch_candidate_exists"));if(RosterButtons.IsValidIndex(Index))Click(RosterButtons[Index],TEXT("portrait_switch_button"));};
    switch(SceneCheckStep)
    {
    case 0:
        SceneCheckDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("SceneChecks"));IFileManager::Get().MakeDirectory(*SceneCheckDirectory,true);
        SceneCheckText=TEXT("Real RHI, delayed UMG delegate navigation; no OS input; isolated automation save.\n");
        SceneCheck(GetWorld()->HasBegunPlay()&&Game->IsInMenu(),TEXT("menu_waits_until_world_has_begun_play"));
        Click(NormalModeButton,TEXT("normal_mode"));Click(StageButtons[0],TEXT("normal_first_stage"));SceneCheckStep=1;break;
    case 1:if(Game->HasTutorial())Click(StartButton,TEXT("acknowledge_intro"));SceneCheckStep=2;break;
    case 2:InspectScene(TEXT("normal_main01"));SceneCheckStep=3;break;
    case 3:Click(HeaderMenuButton,TEXT("return_menu"));Click(ReviewModeButton,TEXT("review_mode"));SceneCheckStage=0;Click(StageButtons[0],TEXT("review_first_stage"));SceneCheckStep=4;break;
    case 4:InspectScene(FString::Printf(TEXT("review_stage_%02d"),SceneCheckStage+1));SceneCheckStep=SceneCheckStage<8?5:6;break;
    case 5:Click(HeaderMenuButton,TEXT("between_stage_menu"));++SceneCheckStage;Click(StageButtons[SceneCheckStage],TEXT("next_stage_card"));SceneCheckStep=4;break;
    case 6:Click(LampButtons[0],TEXT("portrait_lamp"));Click(Cast<ULNCampaignActionButton>(SeatFrames[0]->GetContent()),TEXT("portrait_seat"));Assign(TEXT("rover_spectro"));SceneCheckStep=7;break;
    case 7:SceneCheck(Portraits[0]->GetVisibility()!=ESlateVisibility::Collapsed&&PortraitFallbacks[0]->GetVisibility()==ESlateVisibility::Collapsed,TEXT("rover_uses_real_portrait"));InspectScene(TEXT("portrait_rover"));SceneCheckStep=8;break;
    case 8:Assign(TEXT("danjin"));SceneCheckStep=9;break;
    case 9:SceneCheck(Portraits[0]->GetVisibility()==ESlateVisibility::Collapsed&&PortraitFallbacks[0]->GetVisibility()!=ESlateVisibility::Collapsed,TEXT("danjin_uses_role_fallback"));InspectScene(TEXT("fallback_danjin"));SceneCheckStep=10;break;
    case 10:Assign(TEXT("rover_spectro"));SceneCheckStep=11;break;
    case 11:SceneCheck(Portraits[0]->GetVisibility()!=ESlateVisibility::Collapsed&&PortraitFallbacks[0]->GetVisibility()==ESlateVisibility::Collapsed,TEXT("portrait_recovers_after_fallback"));InspectScene(TEXT("portrait_rover_again"));SceneCheckStep=12;break;
    case 12:Click(HeaderMenuButton,TEXT("final_menu"));Click(NormalModeButton,TEXT("back_to_normal"));Click(StageButtons[0],TEXT("first_stage_again"));SceneCheckStep=13;break;
    case 13:if(Game->HasTutorial())Click(StartButton,TEXT("acknowledge_return_intro"));SceneCheckStep=14;break;
    case 14:InspectScene(TEXT("normal_main01_return"));SceneCheckStep=15;break;
    default:FinishSceneChecks();break;
    }
}

void ULNCampaignWidget::FinishSceneChecks()
{
    bSceneChecksFinished=true;
    for(const auto& Value:SceneCheckSnapshots)
    {const FString Path=Value->AsObject()->GetStringField(TEXT("screenshot"));SceneCheck(IFileManager::Get().FileSize(*Path)>1000,TEXT("screenshot_written.")+FPaths::GetCleanFilename(Path));}
    auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("scope"),TEXT("real RHI; UI delegates after World.HasBegunPlay; no desktop input; isolated profile"));
    Report->SetStringField(TEXT("status"),SceneCheckFailures?TEXT("fail"):TEXT("pass"));Report->SetNumberField(TEXT("checks"),SceneCheckCount);Report->SetNumberField(TEXT("failures"),SceneCheckFailures);
    Report->SetArrayField(TEXT("results"),SceneCheckRows);Report->SetArrayField(TEXT("snapshots"),SceneCheckSnapshots);
    const FIntPoint Viewport=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    Report->SetNumberField(TEXT("width"),Viewport.X);Report->SetNumberField(TEXT("height"),Viewport.Y);
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    SceneCheckText+=FString::Printf(TEXT("CHECKS=%d\nFAILURES=%d\nSCREENSHOTS=%d\n"),SceneCheckCount,SceneCheckFailures,SceneCheckSnapshots.Num());
    const bool Saved=FFileHelper::SaveStringToFile(Json,*(SceneCheckDirectory/TEXT("scene-checks.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        &&FFileHelper::SaveStringToFile(SceneCheckText,*(SceneCheckDirectory/TEXT("scene-checks.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SCENE_CHECKS checks=%d failures=%d screenshots=%d saved=%d path=%s"),SceneCheckCount,SceneCheckFailures,SceneCheckSnapshots.Num(),Saved,*SceneCheckDirectory);
    FPlatformMisc::RequestExitWithStatus(false,SceneCheckFailures||!Saved?1:0);
}
