# Copyright (c) 2026 jesus luque.
# Demo levels of the plugin, /Game/Demo/<Name>, from assets the test scripts
# imported (setup_files.py, setup_sequence.py, setup_backdrop.py): each with
# its clouds, a sky light, a key light, a floor where a shadow is to be seen,
# and a player camera (BenchCamera) framed on the clouds, for -game and for
# render_demo.py.
#   py make_demo.py NAME [--quit]
# NAME: explosions, pawn, bicycle, sparrow, mix.
import unreal, sys

name = [a for a in sys.argv[1:] if not a.startswith("--")][0]
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level = "/Game/Demo/" + name.capitalize()
if unreal.EditorAssetLibrary.does_asset_exist(level):
    les.load_level(level)
    for a in eas.get_all_level_actors():
        eas.destroy_actor(a)
else:
    les.new_level(level)

clouds = []


def cloud(asset, location=unreal.Vector(0, 0, 0), scale=1.0):
    a = eas.spawn_actor_from_class(unreal.GaussianSplatActor, location)
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    comp = a.get_editor_property("gaussian_splat_component")
    comp.set_editor_property("gaussian_splat_asset", unreal.load_asset(asset))
    # Splats cast into UE's shadow maps (r.Gaussians.Shadows).
    comp.set_editor_property("cast_shadow", True)
    clouds.append(a)
    return a


def sequence(folder, location=unreal.Vector(0, 0, 0), scale=1.0, fps=24.0):
    frames = [unreal.load_asset(p.split(".")[0]) for p in sorted(unreal.EditorAssetLibrary.list_assets(folder, recursive=False))]
    frames = [f for f in frames if isinstance(f, unreal.GSplatAsset)]
    a = cloud(frames[-1].get_path_name().split(".")[0], location, scale)
    comp = a.get_editor_property("gaussian_splat_component")
    comp.set_editor_property("sequence_frames", frames)
    comp.set_editor_property("sequence_frames_per_second", fps)
    return a


def bounds(actors):
    lo, hi = None, None
    for s in actors:
        o, x, _ = unreal.SystemLibrary.get_component_bounds(s.get_editor_property("gaussian_splat_component"))
        a, b = o - x, o + x
        lo = a if lo is None else unreal.Vector(min(lo.x, a.x), min(lo.y, a.y), min(lo.z, a.z))
        hi = b if hi is None else unreal.Vector(max(hi.x, b.x), max(hi.y, b.y), max(hi.z, b.z))
    return lo, hi


def sky(cube, intensity=1.0):
    light = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 0))
    lc = light.get_editor_property("light_component")
    lc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    lc.set_editor_property("cubemap", unreal.load_asset(cube))
    lc.set_editor_property("intensity", intensity)
    lc.set_editor_property("lower_hemisphere_is_black", False)
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)


def key(lux=3.0, pitch=-35.0, yaw=-115.0):
    k = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 0))
    k.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw), False)
    kc = k.get_editor_property("light_component")
    kc.set_editor_property("intensity", lux)
    kc.set_editor_property("light_color", unreal.Color(r=255, g=242, b=224, a=255))
    kc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)


def floor_material():
    # A mid grey (0.18, the photographer's card), unlit by nothing but the
    # level's lights: the engine's basic shape material is near white and
    # burns under a studio sky.
    path = "/Game/Demo/M_Floor"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset("M_Floor", "/Game/Demo", unreal.Material, unreal.MaterialFactoryNew())
    grey = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    grey.set_editor_property("constant", unreal.LinearColor(0.18, 0.18, 0.18, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(grey, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 200)
    rough.set_editor_property("r", 0.8)
    unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return mat


def floor(z, half):
    p = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Plane"), unreal.Vector(0, 0, z))
    p.set_actor_label("Floor")
    s = max(half / 50.0, 1.0)
    p.set_actor_scale3d(unreal.Vector(s, s, 1.0))
    p.get_editor_property("static_mesh_component").set_material(0, floor_material())
    return p


def mesh(shape, location, scale):
    m = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/" + shape), location)
    m.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return m


def backdrop(hdr_name, yaw=0.0):
    mat = unreal.load_asset("/Game/Env/M_Backdrop_" + hdr_name)
    sphere = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Sphere"), unreal.Vector(0, 0, 0))
    sphere.set_actor_label("Backdrop")
    sphere.set_actor_scale3d(unreal.Vector(5000, 5000, 5000))
    sphere.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False)
    smc = sphere.get_editor_property("static_mesh_component")
    smc.set_material(0, mat)
    smc.set_editor_property("cast_shadow", False)


def camera(distance=2.6, height=0.45, fov=50.0, yaw=0.0, framed=None):
    lo, hi = bounds(framed if framed else clouds)
    origin, extent = (lo + hi) * 0.5, (hi - lo) * 0.5
    r = max(extent.x, extent.y, extent.z)
    a = unreal.MathLibrary.degrees_to_radians(yaw)
    eye = origin + unreal.Vector(-distance * r * unreal.MathLibrary.cos(a), -distance * r * unreal.MathLibrary.sin(a), height * r)
    cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
    cam.set_actor_label("BenchCamera")
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, origin), False)
    cam.camera_component.set_editor_property("field_of_view", fov)
    cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
    cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    return lo, hi


if name == "explosions":
    # The EmberGen mid-air explosion and the Blender fireball side by side over
    # the San Giuseppe bridge, each its sequence playing, lit by the same sky.
    a = sequence("/Game/Seq/midair")
    lo, hi = bounds([a])
    b = sequence("/Game/Seq/fireball", unreal.Vector(0, (hi.y - lo.y) * 0.9, 0), 4.0)
    backdrop("san_giuseppe_bridge")
    sky("/Game/Env/san_giuseppe_bridge")
    key(3.0)
    camera(2.2, 0.3, 55.0)
elif name == "pawn":
    # athenea's pawn web scene (three clouds: the body in two files, the glass
    # top), relit by the studio sky, at a metre tall, on a floor.
    for asset in ("/Game/Files/athenea_pawn_athz", "/Game/Files/athenea_pawn_athz_car_M_Pawn_Body_W_1",
                  "/Game/Files/athenea_pawn_athz_car_M_Pawn_Top_W"):
        cloud(asset, scale=12.0)
    lo, hi = bounds(clouds)
    floor(lo.z, (hi.x - lo.x) * 3.0)
    sky("/Game/Tx/studio")
    key(2.0)
    camera(6.0, 0.3, 40.0)
elif name == "bicycle":
    # The Mip-NeRF 360 bicycle, a real capture, framed as its dataset cameras
    # see it: from four units out, a little above.
    cloud("/Game/Files/bicycle_splat")
    sky("/Game/Tx/studio")
    eye = unreal.Vector(-400, 0, 150)
    cam = eas.spawn_actor_from_class(unreal.CameraActor, eye)
    cam.set_actor_label("BenchCamera")
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, unreal.Vector(0, 0, 0)), False)
    cam.camera_component.set_editor_property("field_of_view", 60.0)
    cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
    cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
elif name == "sparrow":
    # The sparrow (the 60k USD cloud, its stage saying which way is up), under
    # the studio sky, on a floor.
    # Its stage says no unit, which USD reads as centimetres: its metres are
    # given back by the actor.
    cloud("/Game/Files/sparrow_gs60_usdc", scale=100.0)
    lo, hi = bounds(clouds)
    floor(lo.z, (hi.x - lo.x) * 3.0)
    sky("/Game/Tx/studio")
    key(2.0)
    camera(3.6, 0.3, 45.0)
elif name == "mix":
    # Gaussians among UE's own meshes: the pawn on a floor with a cube and a
    # sphere, the mid-air explosion behind them, one key light for all.
    for asset in ("/Game/Files/athenea_pawn_athz", "/Game/Files/athenea_pawn_athz_car_M_Pawn_Body_W_1",
                  "/Game/Files/athenea_pawn_athz_car_M_Pawn_Top_W"):
        cloud(asset, scale=12.0)
    lo, hi = bounds(clouds)
    size = hi.z - lo.z
    pawn = list(clouds)
    floor(lo.z, size * 12.0)
    mesh("Cube", unreal.Vector((lo.x + hi.x) * 0.5, hi.y + size * 0.5, lo.z + size * 0.2), size / 250.0)
    mesh("Sphere", unreal.Vector((lo.x + hi.x) * 0.5, lo.y - size * 0.5, lo.z + size * 0.15), size / 330.0)
    # The explosion behind the pawn as the camera sees it, a few pawns tall.
    blast = sequence("/Game/Seq/midair", unreal.Vector(hi.x + size * 5.0, 0, lo.z), size / 500.0)
    blo, bhi = bounds([blast])
    blast.set_actor_location(blast.get_actor_location() + unreal.Vector(0, 0, lo.z + size * 0.5 - blo.z), False, False)
    sky("/Game/Tx/studio", 0.6)
    key(4.0, -40.0, -130.0)
    camera(4.5, 0.3, 50.0, framed=pawn)
les.save_current_level()
unreal.log_warning("athenea: demo %s saved with %d clouds" % (level, len(clouds)))
if "--quit" in sys.argv:
    unreal.SystemLibrary.quit_editor()
