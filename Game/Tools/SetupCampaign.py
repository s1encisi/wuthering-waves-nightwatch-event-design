"""UE full-editor Python: create the persistent campaign map and sync config.

Stage-specific floors, routes, blockers and task props are owned by the runtime
campaign controller. This script only replaces its LN_CAMPAIGN_ENV actors.
"""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import shutil
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
SOURCE=ROOT/'Data/Campaign/Campaign.json'
if not SOURCE.is_file():raise RuntimeError('Run ExtractCampaignDesign.py before SetupCampaign')
data=json.loads(SOURCE.read_text(encoding='utf-8'))
if data['counts']!={'stages':9,'waves':29,'enemies':610,'persons':25,'configurations':26,'skills':52,'synergies':52,'reward_conditions':10}:
    raise RuntimeError('Campaign configuration counts differ from the authored nine-stage scope')
MODE='/Script/LongNight.LNCampaignGameMode'
mode=unreal.load_class(None,MODE)
if mode is None:raise RuntimeError('Compile LNCampaignGameMode before generating its map')
MAP='/Game/Maps/L_Campaign'
TAG='LN_CAMPAIGN_ENV'
assets=unreal.EditorAssetLibrary
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if assets.does_asset_exist(MAP):
    if not levels.load_level(MAP):raise RuntimeError('Could not load persistent campaign map')
elif not levels.new_level(MAP):raise RuntimeError('Could not create persistent campaign map')
for actor in actors.get_all_level_actors():
    if actor.actor_has_tag(TAG):actors.destroy_actor(actor)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',mode)
created=[]

def spawn(cls,name,location=(0,0,0),rotation=None):
    obj=actors.spawn_actor_from_class(cls,unreal.Vector(*location),rotation or unreal.Rotator(pitch=0,yaw=0,roll=0))
    if obj is None:raise RuntimeError('Could not create '+name)
    obj.set_actor_label(name);obj.set_folder_path('CampaignEnvironment')
    obj.set_editor_property('tags',[unreal.Name(TAG)])
    created.append(obj)
    return obj

moon=spawn(unreal.DirectionalLight,'CampaignMoon',(5000,-3000,1800),unreal.Rotator(pitch=-52,yaw=-38,roll=0))
moon.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
moon.light_component.set_intensity(2.4)
moon.light_component.set_light_color(unreal.LinearColor(0.43,0.62,0.8,1))
moon.light_component.set_editor_property('atmosphere_sun_light',True)
moon.light_component.set_editor_property('forward_shading_priority',10)
fill=spawn(unreal.DirectionalLight,'CampaignFill',(5000,-3000,1500),unreal.Rotator(pitch=-65,yaw=145,roll=0))
fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
fill.light_component.set_intensity(1.5)
fill.light_component.set_light_color(unreal.LinearColor(.46,.62,.75,1))
fill.light_component.set_editor_property('atmosphere_sun_light',False)
fill.light_component.set_editor_property('forward_shading_priority',0)
fill.light_component.set_cast_shadows(False)
spawn(unreal.SkyAtmosphere,'CampaignAtmosphere')
sky=spawn(unreal.SkyLight,'CampaignSky')
sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_intensity(.7)
sky.light_component.set_editor_property('real_time_capture',True)
post=spawn(unreal.PostProcessVolume,'CampaignExposure')
post.set_editor_property('unbound',True)
settings=post.get_editor_property('settings')
for key,value in {
    'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
    'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
    'override_auto_exposure_bias':True,'auto_exposure_bias':.35,'override_bloom_intensity':True,'bloom_intensity':.12,
    'override_vignette_intensity':True,'vignette_intensity':.15,'override_motion_blur_amount':True,'motion_blur_amount':0.
}.items():settings.set_editor_property(key,value)
post.set_editor_property('settings',settings)
spawn(unreal.PlayerStart,'CampaignStart',(5000,-3000,100))
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    unreal.Vector(5000,2500,4200),unreal.Rotator(pitch=-35,yaw=-90,roll=0))
destination=ROOT/'Content/Campaign/Data'
destination.mkdir(parents=True,exist_ok=True)
for source in (ROOT/'Data/Campaign').glob('*.json'):shutil.copy2(source,destination/source.name)
if not levels.save_current_level():raise RuntimeError('Campaign map save failed')
report={'status':'assets_saved','map':MAP,'game_mode':MODE,'engine':unreal.SystemLibrary.get_engine_version(),
        'environment_actor_count':len(created),'tag':TAG,'stage_geometry_owner':'LNCampaignGameMode runtime',
        'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'counts':data['counts'],
        'old_maps_modified':False,'runtime_verified':False,'saved_at':datetime.now().isoformat()}
output=ROOT/'Saved/Validation/Campaign-environment.json';output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('CAMPAIGN_ENVIRONMENT_OK '+json.dumps(report,ensure_ascii=False))
