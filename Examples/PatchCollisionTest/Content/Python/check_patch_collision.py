"""Open /Game/Tests/PatchCollisionTest, then run 'py check_patch_collision.py' in Unreal."""

import unreal


def check_patch_collision():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name() != "/Game/Tests/PatchCollisionTest.PatchCollisionTest":
        raise RuntimeError("Open /Game/Tests/PatchCollisionTest before running the collision check.")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()

    def named(label):
        matches = [actor for actor in actors if actor.get_actor_label() == label]
        if len(matches) != 1:
            raise RuntimeError(f"Expected one {label} actor, found {len(matches)}.")
        return matches[0]

    terrain, curve, baked = (named(label) for label in ("Terrain", "Curve", "Curve_Baked"))
    if not isinstance(terrain, unreal.PatchSurface) or not isinstance(curve, unreal.PatchSurface):
        raise RuntimeError("Terrain and Curve must be editable patch actors.")
    if curve.get_editor_property("baked_actor") != baked or not isinstance(baked, unreal.StaticMeshActor):
        raise RuntimeError("Curve has lost its reference to Curve_Baked.")
    mesh = baked.get_component_by_class(unreal.StaticMeshComponent)
    if not mesh or not mesh.get_editor_property("static_mesh"):
        raise RuntimeError("Curve_Baked has no static mesh.")

    def trace(start, end):
        return unreal.SystemLibrary.line_trace_single(
            world, unreal.Vector(*start), unreal.Vector(*end), unreal.TraceTypeQuery.ECC_VISIBILITY,
            True, [curve], unreal.DrawDebugTrace.FOR_DURATION, False, draw_time=5.0,
        )

    # Reuse the world coordinates of the original live measurement, in centimetres.
    hit = trace((-340, 211, -17), (-500, 211, -17))
    if not hit:
        raise RuntimeError("The ray through the surface missed Curve_Baked.")
    blocking, _, _, distance, _, impact, _, _, _, actor, component, *_ = hit.to_tuple()
    if not blocking or actor != baked or component != mesh or abs(distance - 80.0) > 0.5:
        raise RuntimeError(f"Unexpected hit on {actor} / {component} at {distance:.2f} cm.")
    if trace((-400, 211, -17), (-340, 211, -17)):
        raise RuntimeError("The control ray pointing away from the surface was blocked.")
    unreal.log(f"PatchCollisionTest PASS | Curve_Baked.{mesh.get_name()} | {distance:.2f} cm "
               f"| impact ({impact.x:.1f}, {impact.y:.1f}, {impact.z:.1f}) | control ray clear")


if __name__ == "__main__":
    check_patch_collision()
