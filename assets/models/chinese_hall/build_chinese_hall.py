#!/usr/bin/env python3
"""Procedurally build a Chinese classical hall for Blender 5.x.

The model is arranged around a five-bay timber hall on a stone platform:
stone balustrade, cinnabar columns, painted architraves, dougong bracket
sets, lattice doors and windows, a curved hip roof, ridge ornaments,
lanterns, and a small garden setting.

Run:
    blender -b --python assets/models/chinese_hall/build_chinese_hall.py

Environment:
    HALL_PREVIEW=1       low-resolution preview render
    HALL_RENDER_ALL=1    render hero, elevation, and entrance detail shots
"""

from __future__ import annotations

import math
import os
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Vector


ASSET_DIR = Path(__file__).resolve().parent
RENDER_DIR = ASSET_DIR / "renders"
BLEND_PATH = ASSET_DIR / "chinese_hall.blend"
FONT_PATH = ASSET_DIR.parents[2] / "assets/fonts/NotoSansCJKsc-Regular.otf"

PREVIEW = os.environ.get("HALL_PREVIEW", "0") == "1"
RENDER_ALL = os.environ.get("HALL_RENDER_ALL", "0") == "1"

PLATFORM_TOP = 1.10
COLUMN_TOP = 5.88
ROOF_EAVE_Z = 7.06
ROOF_RIDGE_Z = 12.18
ROOF_HALF_X = 9.75
ROOF_HALF_Y = 7.15
ROOF_RIDGE_HALF_X = 2.65
ROOF_CORNER_LIFT = 0.78


# ---------------------------------------------------------------------------
# Scene, collections, and utilities


def clear_scene() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.length_unit = "METERS"
    scene.unit_settings.scale_length = 1.0


def make_collection(name: str, parent: bpy.types.Collection | None = None) -> bpy.types.Collection:
    collection = bpy.data.collections.new(name)
    (parent or bpy.context.scene.collection).children.link(collection)
    return collection


def move_to_collection(obj: bpy.types.Object, collection: bpy.types.Collection) -> None:
    for old_collection in list(obj.users_collection):
        old_collection.objects.unlink(obj)
    collection.objects.link(obj)


def srgb_to_linear(value: float) -> float:
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


def hex_rgba(value: str, alpha: float = 1.0) -> tuple[float, float, float, float]:
    value = value.lstrip("#")
    return (
        srgb_to_linear(int(value[0:2], 16) / 255.0),
        srgb_to_linear(int(value[2:4], 16) / 255.0),
        srgb_to_linear(int(value[4:6], 16) / 255.0),
        alpha,
    )


def set_input(node: bpy.types.Node, name: str, value) -> None:
    socket = node.inputs.get(name)
    if socket is not None:
        socket.default_value = value


def new_principled(
    name: str,
    color: str,
    roughness: float = 0.5,
    metallic: float = 0.0,
    coat: float = 0.0,
    emission: str | None = None,
    emission_strength: float = 0.0,
) -> bpy.types.Material:
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (500, 0)
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.location = (180, 0)
    set_input(shader, "Base Color", hex_rgba(color))
    set_input(shader, "Roughness", roughness)
    set_input(shader, "Metallic", metallic)
    set_input(shader, "Coat Weight", coat)
    if emission:
        set_input(shader, "Emission Color", hex_rgba(emission))
        set_input(shader, "Emission Strength", emission_strength)
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])

    material.diffuse_color = hex_rgba(color)
    return material


def new_noise_material(
    name: str,
    color_a: str,
    color_b: str,
    noise_scale: float,
    roughness: float = 0.6,
    bump_strength: float = 0.12,
    metallic: float = 0.0,
    noise_detail: float = 5.0,
) -> bpy.types.Material:
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (720, 0)
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.location = (470, 0)
    texcoord = nodes.new("ShaderNodeTexCoord")
    texcoord.location = (-750, 0)
    noise = nodes.new("ShaderNodeTexNoise")
    noise.location = (-520, 0)
    set_input(noise, "Scale", noise_scale)
    set_input(noise, "Detail", noise_detail)
    set_input(noise, "Roughness", 0.62)
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.location = (-200, 140)
    ramp.color_ramp.elements[0].position = 0.24
    ramp.color_ramp.elements[0].color = hex_rgba(color_a)
    ramp.color_ramp.elements[1].position = 0.78
    ramp.color_ramp.elements[1].color = hex_rgba(color_b)
    bump = nodes.new("ShaderNodeBump")
    bump.location = (220, -170)
    set_input(bump, "Strength", bump_strength)
    set_input(bump, "Distance", 0.045)

    links.new(texcoord.outputs["Object"], noise.inputs["Vector"])
    links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], shader.inputs["Base Color"])
    links.new(noise.outputs["Fac"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    set_input(shader, "Roughness", roughness)
    set_input(shader, "Metallic", metallic)
    material.diffuse_color = hex_rgba(color_a)
    return material


def new_roof_material() -> bpy.types.Material:
    material = bpy.data.materials.new("Roof_Glazed_Tiles")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (850, 0)
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.location = (590, 0)
    texcoord = nodes.new("ShaderNodeTexCoord")
    texcoord.location = (-900, 60)
    uv = texcoord.outputs["UV"]

    wave = nodes.new("ShaderNodeTexWave")
    wave.location = (-640, 190)
    wave.wave_type = "BANDS"
    wave.bands_direction = "X"
    wave.wave_profile = "SIN"
    set_input(wave, "Scale", 52.0)
    set_input(wave, "Distortion", 0.0)
    set_input(wave, "Detail", 0.0)
    set_input(wave, "Detail Scale", 1.0)
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.location = (-390, 200)
    ramp.color_ramp.elements[0].position = 0.20
    ramp.color_ramp.elements[0].color = hex_rgba("#1c2b2e")
    ramp.color_ramp.elements[1].position = 0.80
    ramp.color_ramp.elements[1].color = hex_rgba("#536165")

    noise = nodes.new("ShaderNodeTexNoise")
    noise.location = (-630, -180)
    set_input(noise, "Scale", 7.0)
    set_input(noise, "Detail", 5.0)
    set_input(noise, "Roughness", 0.6)
    glaze_ramp = nodes.new("ShaderNodeValToRGB")
    glaze_ramp.location = (-360, -170)
    glaze_ramp.color_ramp.elements[0].position = 0.26
    glaze_ramp.color_ramp.elements[0].color = (0.20, 0.24, 0.25, 1.0)
    glaze_ramp.color_ramp.elements[1].position = 0.76
    glaze_ramp.color_ramp.elements[1].color = (0.62, 0.67, 0.66, 1.0)
    mix = nodes.new("ShaderNodeMixRGB")
    mix.location = (-80, 60)
    mix.blend_type = "MULTIPLY"
    set_input(mix, "Fac", 0.62)

    bump_wave = nodes.new("ShaderNodeBump")
    bump_wave.location = (260, -170)
    set_input(bump_wave, "Strength", 0.28)
    set_input(bump_wave, "Distance", 0.035)
    bump_noise = nodes.new("ShaderNodeBump")
    bump_noise.location = (420, -300)
    set_input(bump_noise, "Strength", 0.018)
    set_input(bump_noise, "Distance", 0.015)

    links.new(uv, wave.inputs["Vector"])
    links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    links.new(texcoord.outputs["Object"], noise.inputs["Vector"])
    links.new(noise.outputs["Fac"], glaze_ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], mix.inputs[1])
    links.new(glaze_ramp.outputs["Color"], mix.inputs[2])
    links.new(mix.outputs["Color"], shader.inputs["Base Color"])
    links.new(wave.outputs["Fac"], bump_wave.inputs["Height"])
    links.new(bump_wave.outputs["Normal"], bump_noise.inputs["Normal"])
    links.new(noise.outputs["Fac"], bump_noise.inputs["Height"])
    links.new(bump_noise.outputs["Normal"], shader.inputs["Normal"])
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    set_input(shader, "Roughness", 0.46)
    set_input(shader, "Metallic", 0.0)
    set_input(shader, "Coat Weight", 0.08)
    material.diffuse_color = hex_rgba("#334145")
    return material


def new_wood_material(name: str, color_a: str, color_b: str) -> bpy.types.Material:
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (650, 0)
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.location = (400, 0)
    texcoord = nodes.new("ShaderNodeTexCoord")
    texcoord.location = (-780, 0)
    mapping = nodes.new("ShaderNodeMapping")
    mapping.location = (-590, 0)
    mapping.inputs["Scale"].default_value = (1.2, 1.2, 7.0)
    noise = nodes.new("ShaderNodeTexNoise")
    noise.location = (-380, 0)
    set_input(noise, "Scale", 2.3)
    set_input(noise, "Detail", 6.0)
    set_input(noise, "Roughness", 0.7)
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.location = (-140, 100)
    ramp.color_ramp.elements[0].position = 0.22
    ramp.color_ramp.elements[0].color = hex_rgba(color_a)
    ramp.color_ramp.elements[1].position = 0.8
    ramp.color_ramp.elements[1].color = hex_rgba(color_b)
    bump = nodes.new("ShaderNodeBump")
    bump.location = (170, -150)
    set_input(bump, "Strength", 0.09)
    set_input(bump, "Distance", 0.02)

    links.new(texcoord.outputs["Object"], mapping.inputs["Vector"])
    links.new(mapping.outputs["Vector"], noise.inputs["Vector"])
    links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], shader.inputs["Base Color"])
    links.new(noise.outputs["Fac"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    set_input(shader, "Roughness", 0.34)
    set_input(shader, "Coat Weight", 0.24)
    material.diffuse_color = hex_rgba(color_a)
    return material


def new_paving_material() -> bpy.types.Material:
    material = bpy.data.materials.new("Courtyard_Stone_Paving")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (720, 0)
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.location = (460, 0)
    texcoord = nodes.new("ShaderNodeTexCoord")
    texcoord.location = (-760, 0)
    brick = nodes.new("ShaderNodeTexBrick")
    brick.location = (-500, 40)
    brick.offset = 0.5
    brick.offset_frequency = 1
    set_input(brick, "Color1", hex_rgba("#68665f"))
    set_input(brick, "Color2", hex_rgba("#817e73"))
    set_input(brick, "Mortar", hex_rgba("#3d3c37"))
    set_input(brick, "Scale", 0.72)
    set_input(brick, "Mortar Size", 0.018)
    set_input(brick, "Mortar Smooth", 0.5)
    set_input(brick, "Bias", 0.0)
    set_input(brick, "Brick Width", 1.15)
    set_input(brick, "Row Height", 0.58)
    noise = nodes.new("ShaderNodeTexNoise")
    noise.location = (-510, -260)
    set_input(noise, "Scale", 38.0)
    set_input(noise, "Detail", 4.0)
    bump = nodes.new("ShaderNodeBump")
    bump.location = (170, -190)
    set_input(bump, "Strength", 0.18)
    set_input(bump, "Distance", 0.02)

    links.new(texcoord.outputs["Object"], brick.inputs["Vector"])
    links.new(brick.outputs["Color"], shader.inputs["Base Color"])
    links.new(texcoord.outputs["Object"], noise.inputs["Vector"])
    links.new(noise.outputs["Fac"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    set_input(shader, "Roughness", 0.82)
    material.diffuse_color = hex_rgba("#747169")
    return material


def assign_material(obj: bpy.types.Object, material: bpy.types.Material) -> None:
    if obj.data and hasattr(obj.data, "materials"):
        obj.data.materials.append(material)


def add_bevel(obj: bpy.types.Object, width: float = 0.02, segments: int = 2) -> None:
    if width <= 0:
        return
    modifier = obj.modifiers.new("Soft carved edges", "BEVEL")
    modifier.width = width
    modifier.segments = segments
    modifier.limit_method = "ANGLE"


def mesh_object(
    name: str,
    verts,
    faces,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    smooth: bool = False,
    bevel: float = 0.0,
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update(calc_edges=True)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    assign_material(obj, material)
    if smooth:
        for polygon in mesh.polygons:
            polygon.use_smooth = True
    add_bevel(obj, bevel)
    return obj


def add_box(
    name: str,
    center,
    size,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    bevel: float = 0.015,
    rotation=(0.0, 0.0, 0.0),
) -> bpy.types.Object:
    sx, sy, sz = (dimension * 0.5 for dimension in size)
    verts = [
        (-sx, -sy, -sz),
        (sx, -sy, -sz),
        (sx, sy, -sz),
        (-sx, sy, -sz),
        (-sx, -sy, sz),
        (sx, -sy, sz),
        (sx, sy, sz),
        (-sx, sy, sz),
    ]
    faces = [
        (0, 3, 2, 1),
        (4, 5, 6, 7),
        (0, 1, 5, 4),
        (1, 2, 6, 5),
        (2, 3, 7, 6),
        (3, 0, 4, 7),
    ]
    obj = mesh_object(name, verts, faces, material, collection, bevel=bevel)
    obj.location = center
    obj.rotation_euler = Euler(rotation, "XYZ")
    return obj


def cylinder_data(radius_bottom: float, radius_top: float, depth: float, segments: int = 24):
    verts = []
    for z, radius in ((-depth * 0.5, radius_bottom), (depth * 0.5, radius_top)):
        for i in range(segments):
            angle = math.tau * i / segments
            verts.append((radius * math.cos(angle), radius * math.sin(angle), z))
    faces = [tuple(reversed(range(segments))), tuple(range(segments, 2 * segments))]
    for i in range(segments):
        j = (i + 1) % segments
        faces.append((i, j, segments + j, segments + i))
    return verts, faces


def add_cylinder(
    name: str,
    center,
    radius: float,
    depth: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    radius_top: float | None = None,
    segments: int = 24,
    rotation=(0.0, 0.0, 0.0),
    bevel: float = 0.008,
    smooth: bool = True,
) -> bpy.types.Object:
    verts, faces = cylinder_data(radius, radius if radius_top is None else radius_top, depth, segments)
    obj = mesh_object(name, verts, faces, material, collection, smooth=smooth, bevel=bevel)
    obj.location = center
    obj.rotation_euler = Euler(rotation, "XYZ")
    return obj


def add_cylinder_between(
    name: str,
    start,
    end,
    radius: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    radius_end: float | None = None,
    segments: int = 16,
) -> bpy.types.Object:
    start_vertex = Vector(start)
    end_vertex = Vector(end)
    direction = end_vertex - start_vertex
    obj = add_cylinder(
        name,
        (start_vertex + end_vertex) * 0.5,
        radius,
        direction.length,
        material,
        collection,
        radius_top=radius if radius_end is None else radius_end,
        segments=segments,
    )
    obj.rotation_euler = direction.to_track_quat("Z", "Y").to_euler()
    return obj


def add_uv_sphere(
    name: str,
    center,
    scale,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    segments: int = 24,
    rings: int = 12,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=rings,
        radius=1.0,
        location=center,
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    move_to_collection(obj, collection)
    assign_material(obj, material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    return obj


def add_ico_sphere(
    name: str,
    center,
    scale,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    subdivisions: int = 2,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=1.0, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    move_to_collection(obj, collection)
    assign_material(obj, material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    return obj


def add_curve(
    name: str,
    points,
    bevel_depth: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    cyclic: bool = False,
    bevel_resolution: int = 2,
) -> bpy.types.Object:
    curve = bpy.data.curves.new(f"{name}_Curve", type="CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 2
    curve.bevel_depth = bevel_depth
    curve.bevel_resolution = bevel_resolution
    curve.fill_mode = "FULL"
    spline = curve.splines.new("POLY")
    spline.points.add(len(points) - 1)
    for point, coordinate in zip(spline.points, points):
        point.co = (*coordinate, 1.0)
    spline.use_cyclic_u = cyclic
    obj = bpy.data.objects.new(name, curve)
    collection.objects.link(obj)
    assign_material(obj, material)
    return obj


def add_text(
    name: str,
    body: str,
    location,
    size: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    rotation=(math.pi / 2, 0.0, 0.0),
    extrude: float = 0.025,
    spacing: float = 1.0,
) -> bpy.types.Object:
    curve = bpy.data.curves.new(f"{name}_Curve", type="FONT")
    curve.body = body
    curve.size = size
    curve.extrude = extrude
    curve.bevel_depth = min(0.012, extrude * 0.4)
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    curve.space_character = spacing
    if FONT_PATH.exists():
        curve.font = bpy.data.fonts.load(str(FONT_PATH))
    obj = bpy.data.objects.new(name, curve)
    obj.location = location
    obj.rotation_euler = Euler(rotation, "XYZ")
    collection.objects.link(obj)
    assign_material(obj, material)
    return obj


def add_mesh_boxes(
    name: str,
    boxes,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    bevel: float = 0.0,
):
    """Create one mesh from (center, size, rotation_euler) boxes."""
    verts = []
    faces = []
    for center, size, rotation in boxes:
        sx, sy, sz = (dimension * 0.5 for dimension in size)
        local_verts = [
            Vector((-sx, -sy, -sz)),
            Vector((sx, -sy, -sz)),
            Vector((sx, sy, -sz)),
            Vector((-sx, sy, -sz)),
            Vector((-sx, -sy, sz)),
            Vector((sx, -sy, sz)),
            Vector((sx, sy, sz)),
            Vector((-sx, sy, sz)),
        ]
        rotation_matrix = Euler(rotation, "XYZ").to_matrix()
        offset = Vector(center)
        start = len(verts)
        verts.extend(rotation_matrix @ vertex + offset for vertex in local_verts)
        faces.extend(
            [
                (start + 0, start + 3, start + 2, start + 1),
                (start + 4, start + 5, start + 6, start + 7),
                (start + 0, start + 1, start + 5, start + 4),
                (start + 1, start + 2, start + 6, start + 5),
                (start + 2, start + 3, start + 7, start + 6),
                (start + 3, start + 0, start + 4, start + 7),
            ]
        )
    return mesh_object(name, verts, faces, material, collection, bevel=bevel)


def look_at(obj: bpy.types.Object, target) -> None:
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


# ---------------------------------------------------------------------------
# Materials


def build_materials():
    mats = {}
    mats["stone"] = new_noise_material(
        "Stone_Warm_Grey", "#b9b3a4", "#d0cab9", 4.8, roughness=0.78, bump_strength=0.075
    )
    mats["stone_dark"] = new_noise_material(
        "Stone_Carved_Shadow", "#8b887d", "#aaa596", 7.0, roughness=0.82, bump_strength=0.08
    )
    mats["marble"] = new_noise_material(
        "Marble_Pale", "#d8d4c6", "#eeeadd", 5.5, roughness=0.55, bump_strength=0.035
    )
    mats["red"] = new_wood_material("Cinnabar_Lacquer", "#721b16", "#a22b21")
    mats["red_dark"] = new_wood_material("Deep_Cinnabar_Wood", "#49120f", "#741d18")
    mats["green"] = new_principled("Mineral_Green_Beams", "#24514b", roughness=0.32, coat=0.28)
    mats["green_dark"] = new_principled("Dark_Green_Painted_Wood", "#153a36", roughness=0.38, coat=0.18)
    mats["gold"] = new_principled("Antique_Gold", "#b58a3e", roughness=0.24, metallic=0.88)
    mats["gold_dark"] = new_principled("Aged_Bronze", "#756030", roughness=0.36, metallic=0.75)
    mats["plaster"] = new_noise_material(
        "Warm_Plaster", "#d9d0bd", "#eee6d4", 9.0, roughness=0.86, bump_strength=0.055
    )
    mats["paper"] = new_principled(
        "Window_Paper", "#d1ae77", roughness=0.7, emission="#d8a964", emission_strength=0.24
    )
    mats["roof"] = new_roof_material()
    mats["roof_up"] = new_principled("Roof_Ridge_Black_Teal", "#1f2d30", roughness=0.34, coat=0.16)
    mats["black"] = new_principled("Black_Lacquer", "#171716", roughness=0.28, coat=0.34)
    mats["paving"] = new_paving_material()
    mats["earth"] = new_noise_material(
        "Mossy_Earth", "#4f4b3e", "#6e6853", 0.65, roughness=0.95, bump_strength=0.16
    )
    mats["foliage"] = new_noise_material(
        "Pine_Needles", "#1f3526", "#3e5433", 2.7, roughness=0.88, bump_strength=0.12
    )
    mats["trunk"] = new_noise_material(
        "Pine_Bark", "#46362a", "#665040", 5.0, roughness=0.9, bump_strength=0.1
    )
    mats["lantern"] = new_principled(
        "Lantern_Glow", "#851912", roughness=0.45, emission="#df4b20", emission_strength=0.34
    )
    mats["tassel"] = new_principled("Lantern_Tassel", "#9d2117", roughness=0.72)
    mats["rock"] = new_noise_material(
        "Garden_Rock", "#67665d", "#8e8b7d", 3.0, roughness=0.92, bump_strength=0.24
    )
    return mats


# ---------------------------------------------------------------------------
# Stone platform, steps, and balustrade


def build_platform(collections, mats):
    collection = collections["stone"]
    add_box(
        "Platform_Foundation",
        (0.0, 0.0, 0.12),
        (18.8, 13.8, 0.24),
        mats["stone_dark"],
        collection,
        bevel=0.025,
    )
    add_box(
        "Platform_Main",
        (0.0, 0.0, 0.62),
        (18.2, 13.2, 0.76),
        mats["stone"],
        collection,
        bevel=0.035,
    )
    add_box(
        "Platform_Top_Slab",
        (0.0, 0.0, 1.025),
        (17.85, 12.85, 0.15),
        mats["stone"],
        collection,
        bevel=0.018,
    )
    add_box(
        "Platform_Upper_Moulding",
        (0.0, 0.0, 0.985),
        (18.0, 13.0, 0.075),
        mats["stone_dark"],
        collection,
        bevel=0.012,
    )
    add_box(
        "Platform_Floor_Inlay",
        (0.0, 0.05, 1.106),
        (16.95, 11.95, 0.018),
        mats["stone_dark"],
        collection,
        bevel=0.008,
    )

    # A central run of six broad steps, with an uncarved imperial ramp.
    step_count = 6
    rise = PLATFORM_TOP / step_count
    run = 0.37
    stair_width = 5.45
    stair_top_y = -6.32
    for index in range(step_count):
        top = PLATFORM_TOP - (index + 1) * rise
        center_y = stair_top_y - (index + 0.5) * run
        add_box(
            f"Front_Step_{index + 1:02d}",
            (0.0, center_y, top * 0.5),
            (stair_width, run + 0.025, max(top, 0.03)),
            mats["stone"],
            collection,
            bevel=0.012,
        )

    stair_bottom = Vector((0.0, stair_top_y - step_count * run, 0.025))
    stair_upper = Vector((0.0, stair_top_y + 0.08, PLATFORM_TOP + 0.015))
    ramp_center = (stair_bottom + stair_upper) * 0.5
    ramp_delta = stair_upper - stair_bottom
    ramp_angle = math.atan2(ramp_delta.z, ramp_delta.y)
    add_box(
        "Central_Carved_Ramp",
        ramp_center,
        (1.72, ramp_delta.length, 0.11),
        mats["stone_dark"],
        collection,
        bevel=0.02,
        rotation=(ramp_angle, 0.0, 0.0),
    )

    # Sloping stone cheek walls frame the steps.
    for side in (-1, 1):
        add_box(
            f"Stair_Cheek_{side:+d}",
            (side * 2.9, ramp_center.y, PLATFORM_TOP * 0.45),
            (0.28, ramp_delta.length - 0.2, 0.88),
            mats["stone"],
            collection,
            bevel=0.025,
            rotation=(ramp_angle * 0.72, 0.0, 0.0),
        )
        add_cylinder_between(
            f"Stair_Cheek_Rail_{side:+d}",
            (side * 2.9, stair_top_y + 0.05, 1.62),
            (side * 2.9, stair_bottom.y - 0.12, 0.72),
            0.087,
            mats["marble"],
            collection,
            segments=12,
        )
        add_box(
            f"Stair_Cheek_Newel_{side:+d}",
            (side * 2.9, stair_top_y + 0.04, 1.72),
            (0.31, 0.31, 0.85),
            mats["marble"],
            collection,
            bevel=0.04,
        )
        add_uv_sphere(
            f"Stair_Cheek_Lotus_{side:+d}",
            (side * 2.9, stair_top_y + 0.04, 2.19),
            (0.14, 0.14, 0.18),
            mats["marble"],
            collection,
            segments=16,
            rings=8,
        )


def add_rail_post(name, x, y, collection, mats, base_z=PLATFORM_TOP):
    add_box(
        f"{name}_Shaft",
        (x, y, base_z + 0.47),
        (0.22, 0.22, 0.94),
        mats["marble"],
        collection,
        bevel=0.03,
    )
    add_box(
        f"{name}_Cap",
        (x, y, base_z + 0.99),
        (0.33, 0.33, 0.105),
        mats["marble"],
        collection,
        bevel=0.035,
    )
    add_uv_sphere(
        f"{name}_Lotus_Bud",
        (x, y, base_z + 1.11),
        (0.115, 0.115, 0.17),
        mats["marble"],
        collection,
        segments=16,
        rings=8,
    )


def add_rail_panel(name, center, size, along_x, collection, mats):
    panel_size = (size, 0.15, 0.55) if along_x else (0.15, size, 0.55)
    add_box(
        f"{name}_Panel",
        center,
        panel_size,
        mats["marble"],
        collection,
        bevel=0.025,
    )
    inset_size = (size * 0.77, 0.012, 0.31) if along_x else (0.012, size * 0.77, 0.31)
    if along_x:
        inset_center = (center[0], center[1] + (0.082 if center[1] > 0 else -0.082), center[2])
    else:
        inset_center = (center[0] + (0.082 if center[0] > 0 else -0.082), center[1], center[2])
    add_box(
        f"{name}_Inset",
        inset_center,
        inset_size,
        mats["stone_dark"],
        collection,
        bevel=0.012,
    )
    rail_size = (size + 0.1, 0.235, 0.105) if along_x else (0.235, size + 0.1, 0.105)
    add_box(
        f"{name}_Coping",
        (center[0], center[1], center[2] + 0.33),
        rail_size,
        mats["marble"],
        collection,
        bevel=0.022,
    )


def build_balustrade(collections, mats):
    collection = collections["stone"]

    left_front = [-8.55, -7.12, -5.69, -4.26, -2.83]
    right_front = [2.83, 4.26, 5.69, 7.12, 8.55]
    for x in left_front + right_front:
        add_rail_post("Front_Rail_Post", x, -6.23, collection, mats)
    for positions in (left_front, right_front):
        for index, (left, right) in enumerate(zip(positions, positions[1:])):
            center = ((left + right) * 0.5, -6.23, 1.42)
            add_rail_panel(
                f"Front_Rail_{'L' if left < 0 else 'R'}_{index + 1:02d}",
                center,
                right - left - 0.24,
                True,
                collection,
                mats,
            )

    for side in (-1, 1):
        positions = [-6.23, -4.72, -3.21, -1.70, -0.19, 1.32, 2.83, 4.34, 5.85]
        for y in positions:
            add_rail_post("Side_Rail_Post", side * 8.68, y, collection, mats)
        for index, (front, back) in enumerate(zip(positions, positions[1:])):
            center = (side * 8.68, (front + back) * 0.5, 1.42)
            add_rail_panel(
                f"Side_Rail_{'L' if side < 0 else 'R'}_{index + 1:02d}",
                center,
                back - front - 0.24,
                False,
                collection,
                mats,
            )

    back_positions = [-8.55, -7.12, -5.69, -4.26, -2.83, -1.40, 0.03, 1.46, 2.89, 4.32, 5.75, 7.18, 8.55]
    for x in back_positions:
        add_rail_post("Back_Rail_Post", x, 6.23, collection, mats)
    for index, (left, right) in enumerate(zip(back_positions, back_positions[1:])):
        center = ((left + right) * 0.5, 6.23, 1.42)
        add_rail_panel(
            f"Back_Rail_{index + 1:02d}",
            center,
            right - left - 0.24,
            True,
            collection,
            mats,
        )


# ---------------------------------------------------------------------------
# Timber frame


def build_columns(collections, mats):
    collection = collections["timber"]
    column_x = [-7.2, -4.3, -1.45, 1.45, 4.3, 7.2]
    front_y = -4.55
    back_y = 4.55
    positions = [(x, front_y) for x in column_x] + [(x, back_y) for x in column_x]
    positions += [(-7.2, -1.5), (7.2, -1.5), (-7.2, 1.5), (7.2, 1.5)]

    for index, (x, y) in enumerate(positions):
        prefix = f"Column_{index + 1:02d}"
        add_cylinder(
            f"{prefix}_Stone_Drum",
            (x, y, PLATFORM_TOP + 0.15),
            0.45,
            0.3,
            mats["stone"],
            collection,
            radius_top=0.40,
            segments=32,
            bevel=0.012,
        )
        add_cylinder(
            f"{prefix}_Gold_Foot_Ring",
            (x, y, PLATFORM_TOP + 0.33),
            0.335,
            0.055,
            mats["gold"],
            collection,
            segments=28,
            bevel=0.006,
        )
        add_cylinder(
            f"{prefix}_Shaft",
            (x, y, 3.60),
            0.315,
            COLUMN_TOP - PLATFORM_TOP - 0.45,
            mats["red"],
            collection,
            radius_top=0.272,
            segments=32,
            bevel=0.0,
        )
        add_cylinder(
            f"{prefix}_Top_Collar",
            (x, y, 5.55),
            0.307,
            0.12,
            mats["gold_dark"],
            collection,
            segments=28,
            bevel=0.005,
        )
        add_box(
            f"{prefix}_Capital",
            (x, y, 5.82),
            (0.77, 0.77, 0.25),
            mats["green"],
            collection,
            bevel=0.025,
        )
        add_box(
            f"{prefix}_Capital_Gold_Plate",
            (x, y, 5.955),
            (0.83, 0.83, 0.055),
            mats["gold"],
            collection,
            bevel=0.01,
        )

    # Cross-ties over the front columns.
    for x in column_x:
        add_box(
            f"Front_Column_Tie_{x:+.2f}",
            (x, -3.2, 5.73),
            (0.28, 2.75, 0.28),
            mats["red_dark"],
            collection,
            bevel=0.025,
        )
    for x in column_x:
        add_box(
            f"Rear_Column_Tie_{x:+.2f}",
            (x, 3.2, 5.73),
            (0.28, 2.75, 0.28),
            mats["red_dark"],
            collection,
            bevel=0.025,
        )


def build_architraves(collections, mats):
    collection = collections["timber"]
    front_y = -4.55
    back_y = 4.55

    for prefix, y in (("Front", front_y), ("Rear", back_y)):
        add_box(
            f"{prefix}_Lower_Architrave",
            (0.0, y, 5.34),
            (14.85, 0.39, 0.42),
            mats["green"],
            collection,
            bevel=0.02,
        )
        add_box(
            f"{prefix}_Architrave_Gold_Line",
            (0.0, y - 0.205, 5.40),
            (14.72, 0.025, 0.055),
            mats["gold"],
            collection,
            bevel=0.006,
        )
        add_box(
            f"{prefix}_Upper_Architrave",
            (0.0, y, 6.05),
            (15.05, 0.34, 0.30),
            mats["red_dark"],
            collection,
            bevel=0.018,
        )
        # Painted panels between the columns.
        for index in range(5):
            bay_centers = [-5.75, -2.875, 0.0, 2.875, 5.75]
            add_box(
                f"{prefix}_Beam_Panel_{index + 1}",
                (bay_centers[index], y - 0.22, 5.34),
                (2.32 - (0.15 if index in (0, 4) else 0.0), 0.055, 0.26),
                mats["green_dark"],
                collection,
                bevel=0.012,
            )
            add_box(
                f"{prefix}_Beam_Panel_Gold_{index + 1}",
                (bay_centers[index], y - 0.257, 5.34),
                (1.65 - (0.2 if index in (0, 4) else 0.0), 0.018, 0.035),
                mats["gold_dark"],
                collection,
                bevel=0.005,
            )

    for side in (-1, 1):
        add_box(
            f"Side_Architrave_{side:+d}",
            (side * 7.2, 0.0, 5.34),
            (0.39, 9.35, 0.42),
            mats["green"],
            collection,
            bevel=0.02,
        )
        add_box(
            f"Side_Architrave_Gold_{side:+d}",
            (side * 7.2 + side * 0.205, 0.0, 5.40),
            (0.025, 9.2, 0.055),
            mats["gold"],
            collection,
            bevel=0.005,
        )


def add_bracket_set(name, center, rotation_z, collection, mats):
    boxes = [
        ((0.0, 0.0, 0.00), (0.58, 0.58, 0.22), (0.0, 0.0, 0.0)),
        ((0.0, 0.0, 0.18), (1.22, 0.48, 0.16), (0.0, 0.0, 0.0)),
        ((0.0, 0.0, 0.34), (1.75, 0.28, 0.14), (0.0, 0.0, 0.0)),
        ((0.0, 0.0, 0.49), (0.34, 1.55, 0.13), (0.0, 0.0, 0.0)),
        ((-0.55, 0.0, 0.50), (0.20, 0.20, 0.17), (0.0, 0.0, 0.0)),
        ((0.55, 0.0, 0.50), (0.20, 0.20, 0.17), (0.0, 0.0, 0.0)),
        ((0.0, 0.45, 0.62), (0.26, 0.24, 0.18), (0.0, 0.0, 0.0)),
        ((0.0, -0.45, 0.62), (0.26, 0.24, 0.18), (0.0, 0.0, 0.0)),
        ((0.0, 0.0, 0.66), (1.95, 0.23, 0.11), (0.0, 0.0, 0.0)),
    ]
    obj = add_mesh_boxes(name, boxes, mats["green"], collection, bevel=0.012)
    obj.location = center
    obj.rotation_euler.z = rotation_z

    caps = [
        ((-0.82, 0.0, 0.40), (0.11, 0.34, 0.08), (0.0, 0.0, 0.0)),
        ((0.82, 0.0, 0.40), (0.11, 0.34, 0.08), (0.0, 0.0, 0.0)),
        ((0.0, 0.0, 0.74), (1.5, 0.17, 0.045), (0.0, 0.0, 0.0)),
    ]
    cap_obj = add_mesh_boxes(f"{name}_Gold_Caps", caps, mats["gold_dark"], collection, bevel=0.006)
    cap_obj.location = center
    cap_obj.rotation_euler.z = rotation_z


def build_brackets(collections, mats):
    collection = collections["timber"]
    column_x = [-7.2, -4.3, -1.45, 1.45, 4.3, 7.2]
    front_x = sorted(column_x + [(-7.2 + -4.3) / 2, (-4.3 + -1.45) / 2, 0.0, 1.45 + 1.425, 4.3 + 1.45])
    for index, x in enumerate(front_x):
        add_bracket_set(f"Front_Dougong_{index + 1:02d}", (x, -4.95, 6.11), 0.0, collection, mats)
    for index, x in enumerate(front_x):
        add_bracket_set(f"Rear_Dougong_{index + 1:02d}", (x, 4.95, 6.11), math.pi, collection, mats)
    side_y = [-4.55, -2.95, -1.35, 0.25, 1.85, 3.45, 4.55]
    for side in (-1, 1):
        for index, y in enumerate(side_y):
            add_bracket_set(
                f"Side_Dougong_{'L' if side < 0 else 'R'}_{index + 1:02d}",
                (side * 7.62, y, 6.11),
                math.pi * 0.5 * (1 if side < 0 else -1),
                collection,
                mats,
            )

    # Plate beams above the bracket sets.
    for prefix, y in (("Front", -4.95), ("Rear", 4.95)):
        add_box(
            f"{prefix}_Top_Plate",
            (0.0, y, 6.73),
            (16.1, 0.42, 0.25),
            mats["green_dark"],
            collection,
            bevel=0.02,
        )
        add_box(
            f"{prefix}_Top_Plate_Gold",
            (0.0, y - 0.22, 6.73),
            (15.96, 0.018, 0.055),
            mats["gold"],
            collection,
            bevel=0.004,
        )
    for side in (-1, 1):
        add_box(
            f"Side_Top_Plate_{side:+d}",
            (side * 7.62, 0.0, 6.73),
            (0.42, 10.8, 0.25),
            mats["green_dark"],
            collection,
            bevel=0.02,
        )


# ---------------------------------------------------------------------------
# Walls, lattice work, and doors


def add_lattice_panel(
    name,
    location,
    width,
    height,
    rotation_z,
    paper_back,
    collection,
    mats,
    paper=True,
):
    boxes = []
    depth = 0.055
    frame = 0.078
    boxes.extend(
        [
            ((-width * 0.5, 0.0, 0.0), (frame, depth * 1.35, height), (0.0, 0.0, 0.0)),
            ((width * 0.5, 0.0, 0.0), (frame, depth * 1.35, height), (0.0, 0.0, 0.0)),
            ((0.0, 0.0, -height * 0.5), (width, depth * 1.35, frame), (0.0, 0.0, 0.0)),
            ((0.0, 0.0, height * 0.5), (width, depth * 1.35, frame), (0.0, 0.0, 0.0)),
        ]
    )

    inner_width = width - frame * 1.5
    inner_height = height - frame * 1.5
    columns = max(3, round(inner_width / 0.31))
    rows = max(4, round(inner_height / 0.31))
    bar_width = 0.032
    for index in range(1, columns):
        x = -inner_width * 0.5 + inner_width * index / columns
        boxes.append(((x, 0.0, 0.0), (bar_width, depth, inner_height), (0.0, 0.0, 0.0)))
    for index in range(1, rows):
        z = -inner_height * 0.5 + inner_height * index / rows
        boxes.append(((0.0, 0.0, z), (inner_width, depth, bar_width), (0.0, 0.0, 0.0)))

    cell_width = inner_width / columns
    cell_height = inner_height / rows
    diagonal_length = math.hypot(cell_width, cell_height) * 0.91
    diagonal_angle = math.atan2(cell_height, cell_width)
    for column in range(columns):
        for row in range(rows):
            if (column + row) % 3 != 0:
                continue
            x = -inner_width * 0.5 + (column + 0.5) * cell_width
            z = -inner_height * 0.5 + (row + 0.5) * cell_height
            boxes.append(
                ((x, 0.0, z), (diagonal_length, depth * 0.82, 0.028), (0.0, diagonal_angle, 0.0))
            )
            boxes.append(
                ((x, 0.0, z), (diagonal_length, depth * 0.82, 0.028), (0.0, -diagonal_angle, 0.0))
            )

    obj = add_mesh_boxes(name, boxes, mats["red_dark"], collection, bevel=0.006)
    obj.location = location
    obj.rotation_euler.z = rotation_z

    if paper:
        offset = Matrix.Rotation(rotation_z, 3, "Z") @ Vector((0.0, paper_back * 0.045, 0.0))
        add_box(
            f"{name}_Paper",
            Vector(location) + offset,
            (width - 0.11, 0.024, height - 0.11),
            mats["paper"],
            collection,
            bevel=0.005,
            rotation=(0.0, 0.0, rotation_z),
        )
    return obj


def add_door_panel(name, center, size, collection, mats):
    add_box(name, center, size, mats["red"], collection, bevel=0.018)
    # Gold door studs, laid out as a traditional nine-by-five rhythm.
    columns = 4
    rows = 4
    width = size[0]
    height = size[2]
    for column in range(columns):
        for row in range(rows):
            x = center[0] - width * 0.32 + column * (width * 0.64 / (columns - 1))
            z = center[2] - height * 0.29 + row * (height * 0.58 / (rows - 1))
            add_cylinder(
                f"{name}_Stud_{column}_{row}",
                (x, center[1] - size[1] * 0.5 - 0.018, z),
                0.034,
                0.042,
                mats["gold"],
                collection,
                segments=10,
                rotation=(math.pi * 0.5, 0.0, 0.0),
                bevel=0.0,
            )


def add_door_ring(name, center, collection, mats):
    bpy.ops.mesh.primitive_torus_add(
        major_radius=0.085,
        minor_radius=0.015,
        major_segments=20,
        minor_segments=6,
        location=center,
        rotation=(math.pi * 0.5, 0.0, 0.0),
    )
    obj = bpy.context.object
    obj.name = name
    move_to_collection(obj, collection)
    assign_material(obj, mats["gold"])
    for polygon in obj.data.polygons:
        polygon.use_smooth = True


def build_facade(collections, mats):
    collection = collections["facade"]
    bay_edges = [-7.2, -4.3, -1.45, 1.45, 4.3, 7.2]
    front_wall_y = -2.10

    add_box(
        "Front_Plaster_Wall",
        (0.0, front_wall_y, 3.14),
        (14.4, 0.22, 4.08),
        mats["plaster"],
        collection,
        bevel=0.02,
    )
    add_box(
        "Front_Wall_Dado",
        (0.0, front_wall_y - 0.13, 1.42),
        (14.35, 0.12, 0.66),
        mats["red_dark"],
        collection,
        bevel=0.018,
    )

    # Wall posts and rails articulate the five bays.
    for index, x in enumerate(bay_edges):
        add_box(
            f"Front_Wall_Post_{index + 1}",
            (x, front_wall_y - 0.17, 3.14),
            (0.23, 0.21, 4.08),
            mats["red"],
            collection,
            bevel=0.018,
        )
    for z in (5.16,):
        add_box(
            f"Front_Wall_Rail_{z:.2f}",
            (0.0, front_wall_y - 0.18, z),
            (14.4, 0.22, 0.15),
            mats["red"],
            collection,
            bevel=0.014,
        )

    bay_types = ["window", "window", "door", "window", "window"]
    for index, (left, right, bay_type) in enumerate(zip(bay_edges, bay_edges[1:], bay_types)):
        center_x = (left + right) * 0.5
        bay_width = right - left
        if bay_type == "window":
            opening_width = bay_width - 0.54
            opening_z = 3.28
            opening_height = 2.45
            add_box(
                f"Window_Recess_{index + 1}",
                (center_x, front_wall_y - 0.135, opening_z),
                (opening_width, 0.04, opening_height),
                mats["black"],
                collection,
                bevel=0.008,
            )
            add_lattice_panel(
                f"Front_Window_Lattice_{index + 1}",
                (center_x, front_wall_y - 0.30, opening_z),
                opening_width,
                opening_height,
                0.0,
                1.0,
                collection,
                mats,
            )
            # Windowsill and carved apron.
            add_box(
                f"Window_Sill_{index + 1}",
                (center_x, front_wall_y - 0.31, 1.98),
                (opening_width + 0.34, 0.22, 0.13),
                mats["stone_dark"],
                collection,
                bevel=0.02,
            )
            add_box(
                f"Window_Apron_{index + 1}",
                (center_x, front_wall_y - 0.23, 1.72),
                (opening_width, 0.11, 0.44),
                mats["red_dark"],
                collection,
                bevel=0.014,
            )
            add_box(
                f"Window_Head_{index + 1}",
                (center_x, front_wall_y - 0.29, 4.57),
                (opening_width + 0.24, 0.16, 0.13),
                mats["red"],
                collection,
                bevel=0.016,
            )
        else:
            door_bottom = 1.16
            door_top = 4.62
            center = (center_x, front_wall_y - 0.28, (door_bottom + door_top) * 0.5)
            add_box(
                f"Central_Door_Recess_{index + 1}",
                center,
                (bay_width - 0.44, 0.06, door_top - door_bottom),
                mats["black"],
                collection,
                bevel=0.01,
            )
            leaf_width = (bay_width - 0.62) * 0.5
            leaf_centers = (center_x - leaf_width * 0.52, center_x + leaf_width * 0.52)
            for leaf_index, leaf_center_x in enumerate(leaf_centers):
                add_door_panel(
                    f"Central_Door_Leaf_{leaf_index + 1}_Lower",
                    (leaf_center_x, front_wall_y - 0.34, 1.95),
                    (leaf_width, 0.095, 1.45),
                    collection,
                    mats,
                )
                add_lattice_panel(
                    f"Central_Door_Leaf_{leaf_index + 1}_Lattice",
                    (leaf_center_x, front_wall_y - 0.36, 3.57),
                    leaf_width,
                    1.78,
                    0.0,
                    1.0,
                    collection,
                    mats,
                )
                add_box(
                    f"Central_Door_Leaf_{leaf_index + 1}_Stile",
                    (leaf_center_x, front_wall_y - 0.39, 2.92),
                    (leaf_width + 0.04, 0.07, 0.12),
                    mats["gold_dark"],
                    collection,
                    bevel=0.012,
                )
                add_door_ring(
                    f"Central_Door_Ring_{leaf_index + 1}",
                    (leaf_center_x + (0.22 if leaf_index == 0 else -0.22), front_wall_y - 0.405, 2.53),
                    collection,
                    mats,
                )

            add_box(
                "Central_Door_Threshold",
                (center_x, front_wall_y - 0.38, 1.14),
                (bay_width - 0.28, 0.36, 0.15),
                mats["stone_dark"],
                collection,
                bevel=0.018,
            )
            add_box(
                "Central_Door_Lintel",
                (center_x, front_wall_y - 0.36, 4.77),
                (bay_width - 0.26, 0.14, 0.22),
                mats["red"],
                collection,
                bevel=0.016,
            )

    # Plaque, backing board, and gold corner brackets.
    add_box(
        "Name_Plaque_Board",
        (0.0, -4.82, 5.34),
        (2.95, 0.12, 0.78),
        mats["black"],
        collection,
        bevel=0.035,
    )
    add_box(
        "Name_Plaque_Gold_Border",
        (0.0, -4.89, 5.34),
        (2.77, 0.035, 0.62),
        mats["gold_dark"],
        collection,
        bevel=0.018,
    )
    add_box(
        "Name_Plaque_Inner",
        (0.0, -4.912, 5.34),
        (2.63, 0.02, 0.49),
        mats["black"],
        collection,
        bevel=0.01,
    )
    add_text(
        "Name_Plaque_Characters",
        "观云阁",
        (0.0, -4.94, 5.34),
        0.365,
        mats["gold"],
        collection,
        extrude=0.022,
        spacing=1.22,
    )

    # Side and rear walls.
    for side in (-1, 1):
        add_box(
            f"Side_Wall_{side:+d}",
            (side * 7.17, 1.22, 3.14),
            (0.24, 6.55, 4.08),
            mats["plaster"],
            collection,
            bevel=0.02,
        )
        add_box(
            f"Side_Wall_Dado_{side:+d}",
            (side * 7.31, 1.22, 1.43),
            (0.09, 6.48, 0.66),
            mats["red_dark"],
            collection,
            bevel=0.018,
        )
        for y in (-2.0, -0.55, 0.9, 2.35, 3.8, 4.45):
            add_box(
                f"Side_Wall_Post_{side:+d}_{y:.2f}",
                (side * 7.34, y, 3.14),
                (0.14, 0.24, 4.08),
                mats["red"],
                collection,
                bevel=0.012,
            )
        add_lattice_panel(
            f"Side_Window_{'L' if side < 0 else 'R'}",
            (side * 7.41, 1.72, 3.23),
            2.18,
            1.95,
            math.pi * 0.5 if side < 0 else -math.pi * 0.5,
            -1.0,
            collection,
            mats,
        )
        add_box(
            f"Side_Window_Sill_{side:+d}",
            (side * 7.43, 1.72, 2.14),
            (0.13, 2.45, 0.12),
            mats["stone_dark"],
            collection,
            bevel=0.016,
        )

    add_box(
        "Rear_Wall",
        (0.0, 4.48, 3.14),
        (14.4, 0.23, 4.08),
        mats["plaster"],
        collection,
        bevel=0.02,
    )
    add_box(
        "Rear_Wall_Dado",
        (0.0, 4.61, 1.43),
        (14.35, 0.10, 0.66),
        mats["red_dark"],
        collection,
        bevel=0.018,
    )
    for index, x in enumerate(bay_edges):
        add_box(
            f"Rear_Wall_Post_{index + 1}",
            (x, 4.62, 3.14),
            (0.22, 0.15, 4.08),
            mats["red"],
            collection,
            bevel=0.012,
        )
    for index, center_x in enumerate((-5.75, 0.0, 5.75)):
        add_lattice_panel(
            f"Rear_Window_{index + 1}",
            (center_x, 4.66, 3.52),
            1.85,
            1.35,
            math.pi,
            1.0,
            collection,
            mats,
        )


# ---------------------------------------------------------------------------
# Curved hip roof


def roof_surface_height(u: float, v: float) -> float:
    return (
        ROOF_EAVE_Z
        + (ROOF_RIDGE_Z - ROOF_EAVE_Z) * (v**1.58)
        + ROOF_CORNER_LIFT * (abs(u) ** 4.6) * ((1.0 - v) ** 4.0)
    )


def roof_patch_mesh(name, kind, collection, material, u_steps, v_steps):
    vertices = []
    faces = []
    uv_by_vertex = {}
    for j in range(v_steps + 1):
        v = j / v_steps
        half_width = ROOF_HALF_X - (ROOF_HALF_X - ROOF_RIDGE_HALF_X) * v
        for i in range(u_steps + 1):
            u = -1.0 + 2.0 * i / u_steps
            z = roof_surface_height(u, v)
            if kind in ("front", "back"):
                x = u * half_width
                y = (1.0 if kind == "back" else -1.0) * ROOF_HALF_Y * (1.0 - v)
            else:
                x = (1.0 if kind == "right" else -1.0) * half_width
                y = u * ROOF_HALF_Y * (1.0 - v)
            index = len(vertices)
            vertices.append((x, y, z))
            uv_by_vertex[index] = ((u + 1.0) * 0.5, v)

    for j in range(v_steps):
        for i in range(u_steps):
            a = j * (u_steps + 1) + i
            b = a + 1
            c = b + (u_steps + 1)
            d = a + (u_steps + 1)
            if kind in ("front", "right"):
                faces.append((a, b, c, d))
            else:
                faces.append((a, d, c, b))

    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update(calc_edges=True)
    uv_layer = mesh.uv_layers.new(name="Roof Tiles")
    for polygon in mesh.polygons:
        for loop_index in polygon.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            uv_layer.data[loop_index].uv = uv_by_vertex[vertex_index]
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    assign_material(obj, material)
    for polygon in mesh.polygons:
        polygon.use_smooth = True

    solidify = obj.modifiers.new("Roof Tile Bed", "SOLIDIFY")
    solidify.thickness = 0.20
    solidify.offset = -1.0
    solidify.use_rim = True
    bevel = obj.modifiers.new("Roof Edge Softness", "BEVEL")
    bevel.width = 0.025
    bevel.segments = 2
    bevel.limit_method = "ANGLE"
    return obj


def eave_perimeter_points(raise_amount: float = 0.0):
    points = []
    samples = 35
    for segment in range(4):
        for index in range(samples):
            s = -1.0 + 2.0 * index / (samples - 1)
            z = ROOF_EAVE_Z + raise_amount + ROOF_CORNER_LIFT * abs(s) ** 4.6
            if segment == 0:
                point = (s * ROOF_HALF_X, -ROOF_HALF_Y, z)
            elif segment == 1:
                point = (ROOF_HALF_X, s * ROOF_HALF_Y, z)
            elif segment == 2:
                point = (-s * ROOF_HALF_X, ROOF_HALF_Y, z)
            else:
                point = (-ROOF_HALF_X, -s * ROOF_HALF_Y, z)
            points.append(point)
    return points


def build_roof_rafters(collections, mats):
    collection = collections["roof"]
    # Front and rear rafters.
    for side in (-1, 1):
        boxes = []
        cap_boxes = []
        for index in range(39):
            x = -8.8 + index * (17.6 / 38)
            lift = ROOF_CORNER_LIFT * (abs(x) / ROOF_HALF_X) ** 4.6
            center_y = side * 6.10
            center_z = ROOF_EAVE_Z - 0.27 + lift * 0.48
            angle = -side * 0.145
            boxes.append(((x, center_y, center_z), (0.105, 2.10, 0.125), (angle, 0.0, 0.0)))
            cap_boxes.append(
                (
                    (x, side * 7.20, center_z + 0.08),
                    (0.15, 0.055, 0.16),
                    (0.0, 0.0, 0.0),
                )
            )
        add_mesh_boxes(
            f"{'Front' if side < 0 else 'Rear'}_Eave_Rafters",
            boxes,
            mats["red_dark"],
            collection,
            bevel=0.006,
        )
        add_mesh_boxes(
            f"{'Front' if side < 0 else 'Rear'}_Rafter_Gold_Ends",
            cap_boxes,
            mats["gold_dark"],
            collection,
            bevel=0.004,
        )

    # Side rafters.
    for side in (-1, 1):
        boxes = []
        caps = []
        for index in range(29):
            y = -6.2 + index * (12.4 / 28)
            lift = ROOF_CORNER_LIFT * (abs(y) / ROOF_HALF_Y) ** 4.6
            boxes.append(
                (
                    (side * 6.10, y, ROOF_EAVE_Z - 0.27 + lift * 0.48),
                    (2.10, 0.105, 0.125),
                    (0.0, side * 0.145, 0.0),
                )
            )
            caps.append(
                (
                    (side * 7.20, y, ROOF_EAVE_Z - 0.18 + lift * 0.48),
                    (0.055, 0.15, 0.16),
                    (0.0, 0.0, 0.0),
                )
            )
        add_mesh_boxes(
            f"{'Left' if side < 0 else 'Right'}_Eave_Rafters",
            boxes,
            mats["red_dark"],
            collection,
            bevel=0.006,
        )
        add_mesh_boxes(
            f"{'Left' if side < 0 else 'Right'}_Rafter_Gold_Ends",
            caps,
            mats["gold_dark"],
            collection,
            bevel=0.004,
        )


def add_roof_tile_caps(collections, mats):
    collection = collections["roof"]
    instances_front = []
    instances_side = []
    for index in range(43):
        x = -9.35 + index * (18.7 / 42)
        z = ROOF_EAVE_Z + ROOF_CORNER_LIFT * (abs(x) / ROOF_HALF_X) ** 4.6 + 0.008
        for side in (-1, 1):
            instances_front.append((x, side * ROOF_HALF_Y, z, 0.066, 0.14, (math.pi * 0.5, 0.0, 0.0)))
    for index in range(31):
        y = -6.75 + index * (13.5 / 30)
        z = ROOF_EAVE_Z + ROOF_CORNER_LIFT * (abs(y) / ROOF_HALF_Y) ** 4.6 + 0.008
        for side in (-1, 1):
            instances_side.append(
                (side * ROOF_HALF_X, y, z, 0.066, 0.14, (0.0, math.pi * 0.5, 0.0))
            )

    def build_caps(name, instances):
        verts = []
        faces = []
        for center, radius, depth, rotation in (
            ((item[0], item[1], item[2]), item[3], item[4], item[5]) for item in instances
        ):
            local_verts, local_faces = cylinder_data(radius, radius, depth, 10)
            rotation_matrix = Euler(rotation, "XYZ").to_matrix()
            offset = Vector(center)
            start = len(verts)
            verts.extend(rotation_matrix @ Vector(vertex) + offset for vertex in local_verts)
            faces.extend(tuple(start + index for index in face) for face in local_faces)
        return mesh_object(name, verts, faces, mats["roof_up"], collection, smooth=True)

    build_caps("Eave_Round_Tile_Ends", instances_front + instances_side)


def build_roof(collections, mats):
    collection = collections["roof"]
    roof_patch_mesh("Roof_Front_Slope", "front", collection, mats["roof"], 54, 24)
    roof_patch_mesh("Roof_Back_Slope", "back", collection, mats["roof"], 54, 24)
    roof_patch_mesh("Roof_Right_Slope", "right", collection, mats["roof"], 26, 24)
    roof_patch_mesh("Roof_Left_Slope", "left", collection, mats["roof"], 26, 24)

    # Thick painted fascia follows the lifted eave line on all four sides.
    eave_points = eave_perimeter_points(raise_amount=-0.035)
    add_curve(
        "Eave_Fascia_Board",
        eave_points,
        0.135,
        mats["green_dark"],
        collection,
        cyclic=True,
        bevel_resolution=2,
    )
    add_curve(
        "Eave_Gold_Trim",
        [(x, y, z + 0.118) for x, y, z in eave_points],
        0.026,
        mats["gold"],
        collection,
        cyclic=True,
        bevel_resolution=1,
    )

    # Hip ridges run from each eave corner to the ends of the main ridge.
    for x_sign in (-1, 1):
        for y_sign in (-1, 1):
            points = []
            for index in range(29):
                v = index / 28
                half_width = ROOF_HALF_X - (ROOF_HALF_X - ROOF_RIDGE_HALF_X) * v
                x = x_sign * half_width
                y = y_sign * ROOF_HALF_Y * (1.0 - v)
                z = roof_surface_height(1.0, v) + 0.04
                points.append((x, y, z))
            add_curve(
                f"Hip_Ridge_{'L' if x_sign < 0 else 'R'}_{'F' if y_sign < 0 else 'B'}",
                points,
                0.135,
                mats["roof_up"],
                collection,
                bevel_resolution=2,
            )
            tip = points[0]
            outward = Vector((x_sign, y_sign, 0.0)).normalized()
            add_curve(
                f"Corner_Sweep_{'L' if x_sign < 0 else 'R'}_{'F' if y_sign < 0 else 'B'}",
                [
                    tip,
                    Vector(tip) + outward * 0.30 + Vector((0.0, 0.0, 0.10)),
                    Vector(tip) + outward * 0.47 + Vector((0.0, 0.0, 0.30)),
                ],
                0.075,
                mats["gold_dark"],
                collection,
                bevel_resolution=2,
            )

    add_box(
        "Main_Roof_Ridge",
        (0.0, 0.0, ROOF_RIDGE_Z + 0.18),
        (2.0 * ROOF_RIDGE_HALF_X + 1.18, 0.48, 0.43),
        mats["roof_up"],
        collection,
        bevel=0.075,
    )
    add_box(
        "Main_Roof_Ridge_Gold_Line",
        (0.0, 0.0, ROOF_RIDGE_Z + 0.405),
        (2.0 * ROOF_RIDGE_HALF_X + 0.92, 0.14, 0.045),
        mats["gold_dark"],
        collection,
        bevel=0.008,
    )

    for sign in (-1, 1):
        end_x = sign * (ROOF_RIDGE_HALF_X + 0.63)
        base_z = ROOF_RIDGE_Z + 0.37
        chiwen_points = [
            (end_x, 0.0, base_z),
            (end_x - sign * 0.08, 0.0, base_z + 0.28),
            (end_x - sign * 0.22, 0.0, base_z + 0.55),
            (end_x - sign * 0.50, 0.0, base_z + 0.72),
            (end_x - sign * 0.68, 0.0, base_z + 0.64),
            (end_x - sign * 0.61, 0.0, base_z + 0.89),
        ]
        add_curve(
            f"Ridge_Chiwen_{'L' if sign < 0 else 'R'}",
            chiwen_points,
            0.13,
            mats["roof_up"],
            collection,
            bevel_resolution=3,
        )
        add_curve(
            f"Ridge_Chiwen_Accent_{'L' if sign < 0 else 'R'}",
            [
                (end_x - sign * 0.15, 0.0, base_z + 0.24),
                (end_x - sign * 0.42, 0.0, base_z + 0.42),
                (end_x - sign * 0.54, 0.0, base_z + 0.62),
            ],
            0.032,
            mats["gold_dark"],
            collection,
            bevel_resolution=1,
        )

    build_roof_rafters(collections, mats)
    add_roof_tile_caps(collections, mats)


# ---------------------------------------------------------------------------
# Lanterns, garden furniture, and landscape


def add_torus(
    name,
    center,
    major_radius,
    minor_radius,
    material,
    collection,
    rotation=(0.0, 0.0, 0.0),
):
    bpy.ops.mesh.primitive_torus_add(
        major_radius=major_radius,
        minor_radius=minor_radius,
        major_segments=24,
        minor_segments=6,
        location=center,
        rotation=rotation,
    )
    obj = bpy.context.object
    obj.name = name
    move_to_collection(obj, collection)
    assign_material(obj, material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    return obj


def add_hanging_lantern(name, x, y, collection, mats):
    add_cylinder_between(
        f"{name}_Cord",
        (x, y, 6.92),
        (x, y, 5.99),
        0.012,
        mats["black"],
        collection,
        segments=8,
    )
    add_cylinder(
        f"{name}_Top_Cap",
        (x, y, 5.95),
        0.21,
        0.09,
        mats["gold_dark"],
        collection,
        radius_top=0.14,
        segments=16,
        bevel=0.005,
    )
    add_uv_sphere(
        name,
        (x, y, 5.55),
        (0.30, 0.30, 0.40),
        mats["lantern"],
        collection,
        segments=28,
        rings=14,
    )
    for index, (z, radius) in enumerate(((5.55, 0.315), (5.73, 0.265), (5.37, 0.265))):
        add_torus(
            f"{name}_Rib_{index + 1}",
            (x, y, z),
            radius,
            0.011,
            mats["gold"],
            collection,
            rotation=(0.0, 0.0, 0.0),
        )
    add_cylinder(
        f"{name}_Bottom_Cap",
        (x, y, 5.17),
        0.12,
        0.08,
        mats["gold_dark"],
        collection,
        radius_top=0.17,
        segments=14,
        bevel=0.004,
    )
    add_cylinder_between(
        f"{name}_Tassel_Cord",
        (x, y, 5.14),
        (x, y, 4.83),
        0.012,
        mats["tassel"],
        collection,
        segments=8,
    )
    add_cylinder(
        f"{name}_Tassel",
        (x, y, 4.73),
        0.035,
        0.20,
        mats["tassel"],
        collection,
        radius_top=0.105,
        segments=12,
        bevel=0.004,
    )
    light_data = bpy.data.lights.new(f"{name}_Glow", "POINT")
    light_data.energy = 24.0
    light_data.color = (1.0, 0.29, 0.08)
    light_data.shadow_soft_size = 0.3
    light_obj = bpy.data.objects.new(f"{name}_Glow", light_data)
    light_obj.location = (x, y, 5.55)
    collection.objects.link(light_obj)


def add_stone_lantern(name, x, y, collection, mats):
    add_box(
        f"{name}_Base",
        (x, y, 0.17),
        (1.02, 1.02, 0.34),
        mats["stone_dark"],
        collection,
        bevel=0.055,
    )
    add_box(
        f"{name}_Lower_Plinth",
        (x, y, 0.40),
        (0.75, 0.75, 0.18),
        mats["stone"],
        collection,
        bevel=0.035,
    )
    add_cylinder(
        f"{name}_Shaft",
        (x, y, 0.96),
        0.18,
        1.0,
        mats["stone"],
        collection,
        radius_top=0.15,
        segments=8,
        bevel=0.008,
    )
    add_box(
        f"{name}_Lamp_Housing",
        (x, y, 1.63),
        (0.64, 0.64, 0.62),
        mats["stone"],
        collection,
        bevel=0.045,
    )
    for dx, dy, rotation in (
        (0.0, -0.326, 0.0),
        (0.0, 0.326, 0.0),
        (-0.326, 0.0, math.pi * 0.5),
        (0.326, 0.0, math.pi * 0.5),
    ):
        add_box(
            f"{name}_Lamp_Opening_{dx:+.2f}_{dy:+.2f}",
            (x + dx, y + dy, 1.64),
            (0.39, 0.022, 0.29),
            mats["paper"],
            collection,
            bevel=0.012,
            rotation=(0.0, 0.0, rotation),
        )
    add_cylinder(
        f"{name}_Lamp_Roof",
        (x, y, 2.02),
        0.55,
        0.28,
        mats["stone_dark"],
        collection,
        radius_top=0.08,
        segments=6,
        bevel=0.012,
    )
    add_uv_sphere(
        f"{name}_Finial",
        (x, y, 2.23),
        (0.11, 0.11, 0.16),
        mats["stone"],
        collection,
        segments=12,
        rings=6,
    )
    point = bpy.data.lights.new(f"{name}_Warm_Light", "POINT")
    point.energy = 9.0
    point.color = (1.0, 0.55, 0.23)
    point.shadow_soft_size = 0.22
    obj = bpy.data.objects.new(f"{name}_Warm_Light", point)
    obj.location = (x, y, 1.65)
    collection.objects.link(obj)


def add_pine(name, x, y, scale, collection, mats):
    # Leaning, segmented trunk.
    points = [
        Vector((x, y, 0.0)),
        Vector((x + 0.05 * scale, y + 0.10 * scale, 1.2 * scale)),
        Vector((x - 0.10 * scale, y + 0.16 * scale, 2.5 * scale)),
        Vector((x + 0.08 * scale, y + 0.05 * scale, 3.9 * scale)),
        Vector((x - 0.04 * scale, y + 0.12 * scale, 5.0 * scale)),
    ]
    radii = [0.28, 0.22, 0.17, 0.12, 0.07]
    for index, (start, end) in enumerate(zip(points, points[1:])):
        add_cylinder_between(
            f"{name}_Trunk_{index + 1}",
            start,
            end,
            radii[index] * scale,
            mats["trunk"],
            collection,
            radius_end=radii[index + 1] * scale,
            segments=12,
        )

    branch_tips = [
        (-1.15, -0.25, 3.2),
        (1.10, -0.18, 3.55),
        (-0.95, 0.35, 4.15),
        (0.95, 0.32, 4.42),
        (-0.55, -0.10, 4.85),
        (0.65, 0.10, 5.05),
        (0.0, 0.0, 5.25),
    ]
    for index, (dx, dy, z) in enumerate(branch_tips):
        start = Vector((x, y, (2.2 + index * 0.28) * scale))
        end = Vector((x + dx * scale, y + dy * scale, z * scale))
        add_cylinder_between(
            f"{name}_Branch_{index + 1}",
            start,
            end,
            0.08 * scale,
            mats["trunk"],
            collection,
            radius_end=0.035 * scale,
            segments=8,
        )
        add_ico_sphere(
            f"{name}_Foliage_{index + 1}",
            end,
            (1.42 * scale, 0.82 * scale, 0.35 * scale),
            mats["foliage"],
            collection,
            subdivisions=2,
        )
        # Secondary offset pads break up the silhouette.
        pad_offset = Vector((0.55 * scale * (1 if index % 2 else -1), 0.32 * scale, 0.02 * scale))
        add_ico_sphere(
            f"{name}_Foliage_Pad_{index + 1}",
            end + pad_offset,
            (0.92 * scale, 0.56 * scale, 0.23 * scale),
            mats["foliage"],
            collection,
            subdivisions=2,
        )


def add_rock_cluster(name, x, y, scale, collection, mats):
    for index, (dx, dy, dz, sx, sy, sz, rz) in enumerate(
        (
            (0.0, 0.0, 0.27, 0.9, 0.65, 0.52, 0.2),
            (0.62, 0.18, 0.17, 0.58, 0.48, 0.32, 1.1),
            (-0.48, 0.28, 0.12, 0.47, 0.36, 0.24, 2.0),
        )
    ):
        obj = add_ico_sphere(
            f"{name}_Stone_{index + 1}",
            (x + dx * scale, y + dy * scale, dz * scale),
            (sx * scale, sy * scale, sz * scale),
            mats["rock"],
            collection,
            subdivisions=2,
        )
        obj.rotation_euler.z = rz


def build_garden(collections, mats):
    collection = collections["garden"]
    add_box(
        "Courtyard_Paving",
        (0.0, -1.5, -0.055),
        (42.0, 34.0, 0.11),
        mats["paving"],
        collection,
        bevel=0.0,
    )
    add_box(
        "Courtyard_Earth_Border",
        (0.0, 0.0, -0.15),
        (125.0, 110.0, 0.20),
        mats["earth"],
        collection,
        bevel=0.0,
    )
    add_box(
        "Ceremonial_Path",
        (0.0, -12.2, 0.015),
        (4.62, 8.0, 0.035),
        mats["stone_dark"],
        collection,
        bevel=0.02,
    )
    for index, y in enumerate((-9.1, -10.45, -11.8, -13.15, -14.5, -15.85)):
        add_box(
            f"Path_Slab_Joint_{index + 1}",
            (0.0, y, 0.036),
            (4.2, 0.032, 0.009),
            mats["black"],
            collection,
            bevel=0.0,
        )

    add_stone_lantern("Stone_Lantern_Left", -6.6, -9.5, collection, mats)
    add_stone_lantern("Stone_Lantern_Right", 6.6, -9.5, collection, mats)

    add_pine("Pine_Left", -30.0, 8.0, 1.08, collection, mats)
    add_pine("Pine_Right", 25.0, 10.0, 1.12, collection, mats)
    add_pine("Pine_Back", -13.8, 18.5, 0.82, collection, mats)
    add_rock_cluster("Rock_Left", -10.8, 4.8, 1.2, collection, mats)
    add_rock_cluster("Rock_Right", 14.0, 10.0, 1.0, collection, mats)


# ---------------------------------------------------------------------------
# Presentation, lighting, and rendering


def build_world(scene):
    world = bpy.data.worlds.new("Soft_Morning_Sky")
    scene.world = world
    world.use_nodes = True
    nodes = world.node_tree.nodes
    links = world.node_tree.links
    nodes.clear()
    output = nodes.new("ShaderNodeOutputWorld")
    background = nodes.new("ShaderNodeBackground")
    sky = nodes.new("ShaderNodeTexSky")
    sky.sky_type = "MULTIPLE_SCATTERING"
    sky.sun_disc = False
    sky.sun_elevation = math.radians(13.0)
    sky.sun_rotation = math.radians(142.0)
    sky.altitude = 240.0
    sky.air_density = 1.0
    sky.aerosol_density = 2.4
    sky.ozone_density = 1.0
    background.inputs["Strength"].default_value = 0.15
    links.new(sky.outputs["Color"], background.inputs["Color"])
    links.new(background.outputs["Background"], output.inputs["Surface"])


def add_area_light(name, location, target, energy, size, color, collection):
    light_data = bpy.data.lights.new(name, "AREA")
    light_data.energy = energy
    light_data.shape = "DISK"
    light_data.size = size
    light_data.color = color
    obj = bpy.data.objects.new(name, light_data)
    obj.location = location
    collection.objects.link(obj)
    look_at(obj, target)
    return obj


def add_camera(name, location, target, lens, collection):
    camera_data = bpy.data.cameras.new(name)
    camera_data.lens = lens
    camera_data.sensor_width = 36.0
    camera_data.dof.use_dof = False
    camera = bpy.data.objects.new(name, camera_data)
    camera.location = location
    collection.objects.link(camera)
    look_at(camera, target)
    return camera


def build_presentation(scene, collections):
    collection = collections["presentation"]
    build_world(scene)

    sun_data = bpy.data.lights.new("Warm_Afternoon_Sun", "SUN")
    sun_data.energy = 3.2
    sun_data.angle = math.radians(4.0)
    sun_data.color = (1.0, 0.82, 0.64)
    sun = bpy.data.objects.new("Warm_Afternoon_Sun", sun_data)
    sun.location = (0.0, 0.0, 20.0)
    direction = Vector((0.0, 0.0, 4.0)) - Vector((-12.0, -15.0, 18.0))
    sun.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    collection.objects.link(sun)

    add_area_light(
        "Cool_Sky_Fill",
        (-9.0, -16.0, 12.0),
        (0.0, 0.0, 5.0),
        1550.0,
        11.0,
        (0.69, 0.80, 1.0),
        collection,
    )
    add_area_light(
        "Warm_Eave_Bounce",
        (12.0, -5.0, 8.5),
        (0.0, 0.0, 4.5),
        880.0,
        7.0,
        (1.0, 0.70, 0.42),
        collection,
    )
    add_area_light(
        "Rear_Rim",
        (7.0, 13.0, 11.0),
        (0.0, 0.0, 6.0),
        1300.0,
        8.0,
        (0.62, 0.78, 0.88),
        collection,
    )

    cameras = {
        "hero": add_camera(
            "Camera_Hero",
            (27.3, -34.7, 13.0),
            (0.0, -0.5, 5.25),
            51.0,
            collection,
        ),
        "elevation": add_camera(
            "Camera_Elevation",
            (0.0, -39.0, 8.6),
            (0.0, -0.2, 5.45),
            54.0,
            collection,
        ),
        "detail": add_camera(
            "Camera_Entrance_Detail",
            (7.2, -15.6, 4.6),
            (-0.4, -2.0, 3.55),
            62.0,
            collection,
        ),
    }
    scene.camera = cameras["hero"]
    return cameras


def configure_render(scene):
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.film_transparent = False
    scene.render.resolution_x = 900 if PREVIEW else 1800
    scene.render.resolution_y = 600 if PREVIEW else 1200
    scene.render.resolution_percentage = 100
    scene.render.use_file_extension = True

    if hasattr(scene, "eevee"):
        for attribute, value in (
            ("taa_render_samples", 16 if PREVIEW else 72),
            ("taa_samples", 16 if PREVIEW else 72),
            ("use_raytracing", True),
            ("use_shadows", True),
            ("shadow_ray_count", 2),
            ("shadow_step_count", 6),
        ):
            if hasattr(scene.eevee, attribute):
                try:
                    setattr(scene.eevee, attribute, value)
                except (AttributeError, TypeError):
                    pass
    try:
        scene.view_settings.look = "AgX - Medium High Contrast"
    except TypeError:
        pass
    scene.view_settings.exposure = -0.05


def render_views(scene, cameras):
    RENDER_DIR.mkdir(parents=True, exist_ok=True)
    views = ["hero"]
    if RENDER_ALL:
        views = ["hero", "elevation", "detail"]
    for view in views:
        scene.camera = cameras[view]
        suffix = "_preview" if PREVIEW else ""
        scene.render.filepath = str(RENDER_DIR / f"{view}{suffix}.png")
        bpy.ops.render.render(write_still=True)
        print(f"Rendered {scene.render.filepath}")
    scene.camera = cameras["hero"]


def parent_architecture_to_root(collections):
    root = bpy.data.objects.new("Chinese_Classical_Hall", None)
    root.empty_display_type = "PLAIN_AXES"
    root.empty_display_size = 1.5
    collections["root"].objects.link(root)
    for key in ("stone", "timber", "facade", "roof", "ornament"):
        for obj in list(collections[key].all_objects):
            if obj is not root:
                obj.parent = root
    return root


def main():
    clear_scene()
    RENDER_DIR.mkdir(parents=True, exist_ok=True)

    scene = bpy.context.scene
    scene.name = "Chinese Classical Hall"
    root_collection = make_collection("Chinese_Classical_Hall")
    collections = {
        "root": root_collection,
        "stone": make_collection("01_Stonework", root_collection),
        "timber": make_collection("02_Timber_Frame", root_collection),
        "facade": make_collection("03_Facade_and_Lattice", root_collection),
        "roof": make_collection("04_Curved_Roof", root_collection),
        "ornament": make_collection("05_Ornaments_and_Lanterns", root_collection),
    }
    presentation = make_collection("06_Presentation")
    garden = make_collection("07_Garden", presentation)
    presentation_lights = make_collection("08_Cameras_and_Lights", presentation)
    collections.update({"presentation": presentation, "garden": garden, "lights": presentation_lights})

    mats = build_materials()
    build_platform(collections, mats)
    build_balustrade(collections, mats)
    build_columns(collections, mats)
    build_architraves(collections, mats)
    build_brackets(collections, mats)
    build_facade(collections, mats)
    build_roof(collections, mats)
    add_hanging_lantern("Lantern_Left", -4.75, -5.26, collections["ornament"], mats)
    add_hanging_lantern("Lantern_Right", 4.75, -5.26, collections["ornament"], mats)
    parent_architecture_to_root(collections)

    build_garden(collections, mats)
    cameras = build_presentation(scene, collections)
    configure_render(scene)

    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
    render_views(scene, cameras)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
    print(f"Saved {BLEND_PATH}")


if __name__ == "__main__":
    main()



