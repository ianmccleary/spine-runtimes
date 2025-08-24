/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated April 5, 2025. Replaces all prior versions.
 *
 * Copyright (c) 2013-2025, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "SpineSprite.h"
#include "SpineEvent.h"
#include "SpineTrackEntry.h"
#include "SpineSkeleton.h"
#include "SpineRendererObject.h"
#include "SpineSlotNode.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#if TOOLS_ENABLED
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/font.hpp>
#endif

SpineMesh3D::SpineMesh3D() : renderer_object(nullptr), winding(0.f)
{
	if (RS::get_singleton())
	{
		mesh = RS::get_singleton()->mesh_create();
		set_base(mesh);
	}
};

SpineMesh3D::~SpineMesh3D()
{
	if (mesh.is_valid())
	{
		RS::get_singleton()->free_rid(mesh);
	}
}

void SpineMesh3D::_notification(int what)
{
	switch (what)
	{
		case NOTIFICATION_READY:
		{
			set_process_internal(true);
			break;
		}
		default:
			break;
	}
}

void SpineMesh3D::_bind_methods()
{ }

namespace
{
	constexpr int32_t MAX_UINT_16 = std::numeric_limits<uint16_t>::max();

	godot::Vector3 generate_tangent_from_normal(const godot::Vector3& normal)
	{
		return godot::Vector3(normal.z, -normal.x, normal.y).cross(normal.normalized()).normalized();
	}

	CompressedNormalTangent compress_normal(const godot::Vector3& normal)
	{
		CompressedNormalTangent output;

		auto normal_encoded = normal.octahedron_encode();
		output.na = static_cast<uint16_t>(godot::Math::clamp(static_cast<int32_t>(normal_encoded.x * MAX_UINT_16), 0, MAX_UINT_16));
		output.nb =	static_cast<uint16_t>(godot::Math::clamp(static_cast<int32_t>(normal_encoded.y * MAX_UINT_16), 0, MAX_UINT_16));

		auto tangent_encoded = generate_tangent_from_normal(normal).octahedron_tangent_encode(1.f);
		output.ta = static_cast<uint16_t>(godot::Math::clamp(static_cast<int32_t>(tangent_encoded.x * MAX_UINT_16), 0, MAX_UINT_16));
		output.tb = static_cast<uint16_t>(godot::Math::clamp(static_cast<int32_t>(tangent_encoded.y * MAX_UINT_16), 0, MAX_UINT_16));
		if (output.ta == 0 && output.tb == MAX_UINT_16)
		{
			output.ta = MAX_UINT_16;
		}
		return output;
	}
}

ElementLayout::ElementLayout(RS::ArrayFormat format, size_t vertex_count, int32_t type)
{
	offset = RS::get_singleton()->mesh_surface_get_format_offset(format, vertex_count, type);
	switch (type)
	{
		case RS::ARRAY_VERTEX:
		stride = RS::get_singleton()->mesh_surface_get_format_vertex_stride(format, vertex_count);
		break;
		
		case RS::ARRAY_NORMAL:
		case RS::ARRAY_TANGENT:
		stride = RS::get_singleton()->mesh_surface_get_format_normal_tangent_stride(format, vertex_count);
		break;

		case RS::ARRAY_COLOR:
		case RS::ARRAY_TEX_UV:
		stride = RS::get_singleton()->mesh_surface_get_format_attribute_stride(format, vertex_count);
		break;

		default:
		stride = 0;
		break;
	}
}

bool SpineMesh3D::prepare_mesh(int new_vertex_count, const uint16_t* new_indices, int new_index_count)
{
	const auto current_vertex_count = get_vertex_count();
	const auto current_index_count = get_index_count();

	bool remake_surface = false;

	remake_surface |= current_vertex_count != new_vertex_count;
	remake_surface |= current_index_count != new_index_count || memcmp(index_buffer.ptr(), new_indices, new_index_count * INDEX_ELEMENT_SIZE);

	if (remake_surface)
	{
		vertex_buffer.resize(new_vertex_count * VERTEX_ELEMENT_SIZE);
		attribute_buffer.resize(new_vertex_count * ATTRIB_ELEMENT_SIZE);

		index_buffer.resize(new_index_count * INDEX_ELEMENT_SIZE);
		memcpy(index_buffer.ptrw(), new_indices, new_index_count * INDEX_ELEMENT_SIZE);

		godot::Dictionary surface_dict;
		surface_dict["primitive"] = godot::RenderingServer::PrimitiveType::PRIMITIVE_TRIANGLES;
		surface_dict["format"] = SURFACE_FORMAT;
		surface_dict["vertex_data"] = vertex_buffer;
		surface_dict["vertex_count"] = new_vertex_count;
		surface_dict["attribute_data"] = attribute_buffer;
		surface_dict["index_data"] = index_buffer;
		surface_dict["index_count"] = new_index_count;
		surface_dict["aabb"] = godot::AABB();

		// Update mesh
		RS::get_singleton()->mesh_clear(mesh);
		RS::get_singleton()->mesh_add_surface(mesh, surface_dict);

		// Update layout information
		Dictionary surface = RS::get_singleton()->mesh_get_surface(mesh, 0);
		RS::ArrayFormat surface_format = (RS::ArrayFormat) static_cast<int64_t>(surface["format"]);

		vertex_layout = ElementLayout(surface_format, new_vertex_count, RS::ARRAY_VERTEX);
		normal_layout = ElementLayout(surface_format, new_vertex_count, RS::ARRAY_NORMAL);
		tangent_layout = ElementLayout(surface_format, new_vertex_count, RS::ARRAY_TANGENT);
		uv_layout = ElementLayout(surface_format, new_vertex_count, RS::ARRAY_TEX_UV);
		color_layout = ElementLayout(surface_format, new_vertex_count, RS::ARRAY_COLOR);

		const auto new_vertex_buffer = static_cast<PackedByteArray>(surface["vertex_data"]);
		const auto new_attribute_buffer = static_cast<PackedByteArray>(surface["attribute_data"]);

		vertex_buffer = new_vertex_buffer;
		attribute_buffer = new_attribute_buffer;
		return true;
	}

	return false;
}

struct AttribBufferElement
{
	int32_t color;
	float uv_x;
	float uv_y;
};

void SpineMesh3D::assign_vertices(spine::Vector<float>& new_vertices)
{
	const auto vertex_count = get_vertex_count();
	ERR_FAIL_COND(vertex_count != new_vertices.size() / 2);

	for (int i = 0; i < vertex_count; ++i)
	{
		const auto p = Vector2(new_vertices[i * 2], new_vertices[i * 2 + 1]);
		memcpy(&vertex_buffer.ptrw()[vertex_layout.calculate_buffer_index(i)], &p, ELEMENT_SIZE_POSITION);
	}
}

void SpineMesh3D::assign_uvs_and_color(spine::Vector<float>& new_uvs, spine::Color new_color)
{
	const auto vertex_count = get_vertex_count();
	ERR_FAIL_COND(vertex_count != new_uvs.size() / 2);

	const auto color = godot::Color(new_color.r, new_color.g, new_color.b, new_color.a).to_abgr32();

	for (int i = 0; i < vertex_count; ++i)
	{
		const auto uv = Vector2(new_uvs[i * 2], new_uvs[i * 2 + 1]);
		memcpy(&attribute_buffer.ptrw()[color_layout.calculate_buffer_index(i)], &color, ELEMENT_SIZE_COLOR);
		memcpy(&attribute_buffer.ptrw()[uv_layout.calculate_buffer_index(i)], &uv, ELEMENT_SIZE_UV);
	}
}

void SpineMesh3D::update_normals(float attachment_scale_x, float attachment_scale_y)
{
	auto new_winding = godot::Math::sign(attachment_scale_x) * godot::Math::sign(attachment_scale_y);
	if (!godot::Math::is_equal_approx(new_winding, winding))
	{
		winding = new_winding;
		const auto vertex_count = get_vertex_count();
		const auto normal = compress_normal(Vector3(0.f, 0.f, winding));
		for (int i = 0; i < vertex_count; ++i)
		{
			memcpy(&vertex_buffer.ptrw()[normal_layout.calculate_buffer_index(i)], &normal, sizeof(::CompressedNormalTangent));
		}
	}
}

void SpineMesh3D::update_mesh()
{
	auto aabb_new = AABB(Vector3(), Vector3());

	const auto vertex_count = get_vertex_count();
	const auto vertices = get_vertex_buffer_rw<Vector2>();
	for (int i = 0; i < vertex_count; i++)
	{
		const auto position = vertices[i];
		if (i == 0)
		{
			aabb_new.position = Vector3(position.x, position.y, 0.f);
		}
		else
		{
			aabb_new.expand_to(Vector3(position.x, position.y, 0.f));
		}
	}
	const auto aabb_center = aabb_new.get_center();
	aabb_new.expand_to(Vector3(aabb_center.x, aabb_center.y, -0.1f));
	aabb_new.expand_to(Vector3(aabb_center.x, aabb_center.y, 0.1f));

	RS::get_singleton()->mesh_surface_update_vertex_region(mesh, 0, 0, vertex_buffer);
	RS::get_singleton()->mesh_surface_update_attribute_region(mesh, 0, 0, attribute_buffer);
	RS::get_singleton()->mesh_set_custom_aabb(mesh, aabb_new);
}

void SpineMesh3D::set_material(Ref<Material> new_material)
{
	if (material != new_material)
	{
		material = new_material;
		RS::get_singleton()->instance_geometry_set_material_override(get_instance(), material.is_valid() ? material->get_rid() : RID());
	}
}

void SpineSprite::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_skeleton_data_res", "skeleton_data_res"), &SpineSprite::set_skeleton_data_res);
	ClassDB::bind_method(D_METHOD("get_skeleton_data_res"), &SpineSprite::get_skeleton_data_res);
	ClassDB::bind_method(D_METHOD("get_skeleton"), &SpineSprite::get_skeleton);
	ClassDB::bind_method(D_METHOD("get_animation_state"), &SpineSprite::get_animation_state);
	ClassDB::bind_method(D_METHOD("on_skeleton_data_changed"), &SpineSprite::on_skeleton_data_changed);

	ClassDB::bind_method(D_METHOD("get_global_bone_transform", "bone_name"), &SpineSprite::get_global_bone_transform);
	ClassDB::bind_method(D_METHOD("set_global_bone_transform", "bone_name", "global_transform"), &SpineSprite::set_global_bone_transform);

	ClassDB::bind_method(D_METHOD("set_update_mode", "v"), &SpineSprite::set_update_mode);
	ClassDB::bind_method(D_METHOD("get_update_mode"), &SpineSprite::get_update_mode);

	ClassDB::bind_method(D_METHOD("set_normal_material", "material"), &SpineSprite::set_normal_material);
	ClassDB::bind_method(D_METHOD("get_normal_material"), &SpineSprite::get_normal_material);
	ClassDB::bind_method(D_METHOD("set_additive_material", "material"), &SpineSprite::set_additive_material);
	ClassDB::bind_method(D_METHOD("get_additive_material"), &SpineSprite::get_additive_material);
	ClassDB::bind_method(D_METHOD("set_multiply_material", "material"), &SpineSprite::set_multiply_material);
	ClassDB::bind_method(D_METHOD("get_multiply_material"), &SpineSprite::get_multiply_material);
	ClassDB::bind_method(D_METHOD("set_screen_material", "material"), &SpineSprite::set_screen_material);
	ClassDB::bind_method(D_METHOD("get_screen_material"), &SpineSprite::get_screen_material);

	ClassDB::bind_method(D_METHOD("get_time_scale"), &SpineSprite::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_time_scale", "v"), &SpineSprite::set_time_scale);

	ClassDB::bind_method(D_METHOD("get_z_spacing"), &SpineSprite::get_z_spacing);
	ClassDB::bind_method(D_METHOD("set_z_spacing", "v"), &SpineSprite::set_z_spacing);

	ClassDB::bind_method(D_METHOD("get_use_aabb_sorting"), &SpineSprite::get_use_aabb_sorting);
	ClassDB::bind_method(D_METHOD("set_use_aabb_sorting", "v"), &SpineSprite::set_use_aabb_sorting);
	
	ClassDB::bind_method(D_METHOD("get_use_sorting_offset"), &SpineSprite::get_use_sorting_offset);
	ClassDB::bind_method(D_METHOD("set_use_sorting_offset", "v"), &SpineSprite::set_use_sorting_offset);
	ClassDB::bind_method(D_METHOD("get_sorting_offset_multiplier"), &SpineSprite::get_sorting_offset_multiplier);
	ClassDB::bind_method(D_METHOD("set_sorting_offset_multiplier", "v"), &SpineSprite::set_sorting_offset_multiplier);

	ClassDB::bind_method(D_METHOD("update_skeleton", "delta"), &SpineSprite::update_skeleton);
	ClassDB::bind_method(D_METHOD("new_skin", "name"), &SpineSprite::new_skin);

	ADD_SIGNAL(MethodInfo("animation_started", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite"), PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"), PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_interrupted", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite"), PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"), PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_ended", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite"), PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"), PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_completed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite"), PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"), PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_disposed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite"), PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"), PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry")));
	ADD_SIGNAL(MethodInfo("animation_event", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite"), PropertyInfo(Variant::OBJECT, "animation_state", PROPERTY_HINT_TYPE_STRING, "SpineAnimationState"), PropertyInfo(Variant::OBJECT, "track_entry", PROPERTY_HINT_TYPE_STRING, "SpineTrackEntry"), PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_TYPE_STRING, "SpineEvent")));
	ADD_SIGNAL(MethodInfo("before_animation_state_update", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite")));
	ADD_SIGNAL(MethodInfo("before_animation_state_apply", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite")));
	ADD_SIGNAL(MethodInfo("before_world_transforms_change", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite")));
	ADD_SIGNAL(MethodInfo("world_transforms_changed", PropertyInfo(Variant::OBJECT, "spine_sprite", PROPERTY_HINT_TYPE_STRING, "SpineSprite")));
	ADD_SIGNAL(MethodInfo("_internal_spine_objects_invalidated"));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skeleton_data_res", PropertyHint::PROPERTY_HINT_RESOURCE_TYPE, "SpineSkeletonDataResource"), "set_skeleton_data_res", "get_skeleton_data_res");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "z_spacing"), "set_z_spacing", "get_z_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_aabb_sorting"), "set_use_aabb_sorting", "get_use_aabb_sorting");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_sorting_offset"), "set_use_sorting_offset", "get_use_sorting_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sorting_offset_multiplier"), "set_sorting_offset_multiplier", "get_sorting_offset_multiplier");
	ADD_GROUP("Materials", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_normal_material", "get_normal_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "additive_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_additive_material", "get_additive_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiply_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_multiply_material", "get_multiply_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "screen_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_screen_material", "get_screen_material");

	ADD_GROUP("Preview", "");
	// Filled in in _get_property_list()
}

SpineSprite::SpineSprite()
	: update_mode(SpineConstant::UpdateMode_Process),
	time_scale(1.0),
	preview_skin("Default"),
	preview_animation("-- Empty --"),
	preview_frame(false),
	preview_time(0),
	skeleton_clipper(nullptr),
	modified_bones(false)
{
	skeleton_clipper = new spine::SkeletonClipping();
}

SpineSprite::~SpineSprite()
{
	delete skeleton_clipper;
}

void SpineSprite::set_skeleton_data_res(const Ref<SpineSkeletonDataResource> &_skeleton_data)
{
	skeleton_data_res = _skeleton_data;
	on_skeleton_data_changed();
}

Ref<SpineSkeletonDataResource> SpineSprite::get_skeleton_data_res()
{
	return skeleton_data_res;
}

void SpineSprite::on_skeleton_data_changed()
{
	remove_meshes();
	skeleton.unref();
	animation_state.unref();
	emit_signal(SNAME("_internal_spine_objects_invalidated"));

	if (skeleton_data_res.is_valid())
	{
		if (!skeleton_data_res->is_connected(SNAME("skeleton_data_changed"), callable_mp(this, &SpineSprite::on_skeleton_data_changed)))
			skeleton_data_res->connect(SNAME("skeleton_data_changed"), callable_mp(this, &SpineSprite::on_skeleton_data_changed));
	}

	if (skeleton_data_res.is_valid() && skeleton_data_res->is_skeleton_data_loaded())
	{
		skeleton = Ref<SpineSkeleton>(memnew(SpineSkeleton));
		skeleton->set_spine_sprite(this);

		animation_state = Ref<SpineAnimationState>(memnew(SpineAnimationState));
		animation_state->set_spine_sprite(this);
		animation_state->get_spine_object()->setListener(this);

		animation_state->update(0);
		animation_state->apply(skeleton);
		skeleton->update_world_transform(SpineConstant::Physics_Update);
		generate_meshes_for_slots(skeleton);

		if (update_mode == SpineConstant::UpdateMode_Process)
		{
			_notification(NOTIFICATION_INTERNAL_PROCESS);
		}
		else if (update_mode == SpineConstant::UpdateMode_Physics)
		{
			_notification(NOTIFICATION_INTERNAL_PHYSICS_PROCESS);
		}
	}

	NOTIFY_PROPERTY_LIST_CHANGED();
}

void SpineSprite::generate_meshes_for_slots(Ref<SpineSkeleton> skeleton_ref)
{
	auto skeleton = skeleton_ref->get_spine_object();
	for (int i = 0, n = (int) skeleton->getSlots().size(); i < n; i++)
	{
		auto mesh_instance = memnew(SpineMesh3D);
		mesh_instance->set_position(Vector3(0, 0, 0));
		add_child(mesh_instance);
		mesh_instances.push_back(mesh_instance);
		slot_nodes.add(spine::Vector<SpineSlotNode *>());
	}
}

void SpineSprite::remove_meshes()
{
	for (int i = 0; i < mesh_instances.size(); ++i)
	{
		remove_child(mesh_instances[i]);
		memdelete(mesh_instances[i]);
	}
	mesh_instances.clear();
	slot_nodes.clear();
}

void SpineSprite::sort_slot_nodes()
{
	for (int i = 0; i < (int) slot_nodes.size(); i++)
	{
		slot_nodes[i].setSize(0, nullptr);
	}

	auto draw_order = skeleton->get_spine_object()->getDrawOrder();
	for (int i = 0; i < get_child_count(); i++)
	{
		auto child = cast_to<Node2D>(get_child(i));
		if (!child)
			continue;
		// Needed so that debug drawables are rendered in front of attachments and other nodes under the sprite.
		child->set_draw_behind_parent(true);
		auto slot_node = Object::cast_to<SpineSlotNode>(get_child(i));
		if (!slot_node) continue;
		if (slot_node->get_slot_index() == -1 || slot_node->get_slot_index() >= (int) draw_order.size())
		{
			continue;
		}
		slot_nodes[slot_node->get_slot_index()].add(slot_node);
	}

	for (int i = 0; i < (int) draw_order.size(); i++)
	{
		int slot_index = draw_order[i]->getData().getIndex();
		int mesh_index = mesh_instances[i]->get_index();
		spine::Vector<SpineSlotNode *> &nodes = slot_nodes[slot_index];
		for (int j = 0; j < (int) nodes.size(); j++)
		{
			auto node = nodes[j];
			move_child(node, mesh_index + 1);
		}
	}
}

Ref<SpineSkeleton> SpineSprite::get_skeleton()
{
	return skeleton;
}

Ref<SpineAnimationState> SpineSprite::get_animation_state()
{
	return animation_state;
}

void SpineSprite::_notification(int what)
{
	switch (what)
	{
		case NOTIFICATION_READY:
		{
			set_process_internal(update_mode == SpineConstant::UpdateMode_Process);
			set_physics_process_internal(update_mode == SpineConstant::UpdateMode_Physics);
			break;
		}
		case NOTIFICATION_INTERNAL_PROCESS:
		{
			if (update_mode == SpineConstant::UpdateMode_Process)
				update_skeleton(get_process_delta_time());
			break;
		}
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS:
		{
			if (update_mode == SpineConstant::UpdateMode_Physics)
				update_skeleton(get_physics_process_delta_time());
			break;
		}
		default:
			break;
	}
}

void SpineSprite::_get_property_list(List<PropertyInfo> *list) const
{
	if (!skeleton_data_res.is_valid() || !skeleton_data_res->is_skeleton_data_loaded())
		return;
	
	PackedStringArray animation_names;
	PackedStringArray skin_names;
	skeleton_data_res->get_animation_names(animation_names);
	skeleton_data_res->get_skin_names(skin_names);
	animation_names.insert(0, "-- Empty --");

	PropertyInfo preview_skin_property;
	preview_skin_property.name = "preview_skin";
	preview_skin_property.type = Variant::STRING;
	preview_skin_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	preview_skin_property.hint_string = String(",").join(skin_names);
	preview_skin_property.hint = PROPERTY_HINT_ENUM;
	list->push_back(preview_skin_property);

	PropertyInfo preview_anim_property;
	preview_anim_property.name = "preview_animation";
	preview_anim_property.type = Variant::STRING;
	preview_anim_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	preview_anim_property.hint_string = String(",").join(animation_names);
	preview_anim_property.hint = PROPERTY_HINT_ENUM;
	list->push_back(preview_anim_property);

	PropertyInfo preview_frame_property;
	preview_frame_property.name = "preview_frame";
	preview_frame_property.type = Variant::BOOL;
	preview_frame_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	list->push_back(preview_frame_property);

	PropertyInfo preview_time_property;
	preview_time_property.name = "preview_time";
	preview_time_property.type = VARIANT_FLOAT;
	preview_time_property.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_STORAGE;
	float animation_duration = 0;
	if (!EMPTY(preview_animation) && preview_animation != "-- Empty --")
	{
		auto animation = skeleton_data_res->find_animation(preview_animation);
		if (animation.is_valid()) animation_duration = animation->get_duration();
	}

	preview_time_property.hint_string = String("0.0,") + String::num(animation_duration) + String(",0.01");
	preview_time_property.hint = PROPERTY_HINT_RANGE;
	list->push_back(preview_time_property);
}

bool SpineSprite::_get(const StringName &property, Variant &value) const
{
	if (property == StringName("preview_skin"))
	{
		value = preview_skin;
		return true;
	}

	if (property == StringName("preview_animation"))
	{
		value = preview_animation;
		return true;
	}

	if (property == StringName("preview_frame"))
	{
		value = preview_frame;
		return true;
	}

	if (property == StringName("preview_time"))
	{
		value = preview_time;
		return true;
	}
	return false;
}

static void update_preview_animation(SpineSprite *sprite, const String &skin, const String &animation, bool frame, float time)
{
	if (!Engine::get_singleton()->is_editor_hint()) return;
	if (!sprite->get_skeleton().is_valid()) return;

	if (EMPTY(skin) || skin == "Default")
	{
		sprite->get_skeleton()->set_skin(nullptr);
	}
	else
	{
		sprite->get_skeleton()->set_skin_by_name(skin);
	}

	sprite->get_skeleton()->set_to_setup_pose();

	if (EMPTY(animation) || animation == "-- Empty --")
	{
		sprite->get_animation_state()->set_empty_animation(0, 0);
		return;
	}

	auto track_entry = sprite->get_animation_state()->set_animation(animation, true, 0);
	track_entry->set_mix_duration(0);
	if (frame)
	{
		track_entry->set_time_scale(0);
		track_entry->set_track_time(time);
	}
}

bool SpineSprite::_set(const StringName &property, const Variant &value)
{
	if (property == StringName("preview_skin"))
	{
		preview_skin = value;
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		NOTIFY_PROPERTY_LIST_CHANGED();
		return true;
	}

	if (property == StringName("preview_animation"))
	{
		preview_animation = value;
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		NOTIFY_PROPERTY_LIST_CHANGED();
		return true;
	}

	if (property == StringName("preview_frame"))
	{
		preview_frame = value;
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		return true;
	}

	if (property == StringName("preview_time"))
	{
		preview_time = value;
		update_preview_animation(this, preview_skin, preview_animation, preview_frame, preview_time);
		return true;
	}

	return false;
}

void SpineSprite::update_skeleton(float delta)
{
	if (!skeleton_data_res.is_valid() ||
		!skeleton_data_res->is_skeleton_data_loaded() ||
		!skeleton.is_valid() ||
		!skeleton->get_spine_object() ||
		!animation_state.is_valid() ||
		!animation_state->get_spine_object())
		return;

	emit_signal(SNAME("before_animation_state_update"), this);
	animation_state->update(delta * time_scale);

	if (!is_visible_in_tree())
		return;

	emit_signal(SNAME("before_animation_state_apply"), this);
	animation_state->apply(skeleton);
	emit_signal(SNAME("before_world_transforms_change"), this);
	skeleton->update(delta * time_scale);
	skeleton->update_world_transform(SpineConstant::Physics_Update);
	modified_bones = false;
	emit_signal(SNAME("world_transforms_changed"), this);
	if (modified_bones)
		skeleton->update_world_transform(SpineConstant::Physics_Update);
	sort_slot_nodes();
	update_meshes(skeleton);
}

void SpineSprite::update_meshes(Ref<SpineSkeleton> skeleton_ref)
{
	const int QUAD_INDICES_LENGTH = 6;
	const uint16_t QUAD_INDICES[QUAD_INDICES_LENGTH] =
	{
		0, 1, 2,
		2, 3, 0
	};

	const auto skeleton = skeleton_ref->get_spine_object();
	const auto slot_count = skeleton->getSlots().size();

	for (int i = 0; i < slot_count; ++i)
	{
		const auto slot = skeleton->getDrawOrder()[i];
		const auto attachment = slot->getAttachment();
		const auto mesh_instance = mesh_instances[i];

		mesh_instance->set_visible(false);
		mesh_instance->set_position(Vector3(0.f, 0.f, i * z_spacing));
		if (use_sorting_offset)
		{
			mesh_instance->set_sorting_offset(i * sorting_offset_multiplier);
		}
		mesh_instance->renderer_object = nullptr;

		if (!attachment)
		{
			skeleton_clipper->clipEnd(*slot);
			continue;
		}
		if (!slot->getBone().isActive())
		{
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		spine::Color skeleton_color = skeleton->getColor();
		spine::Color slot_color = slot->getColor();
		spine::Color tint(skeleton_color.r * slot_color.r, skeleton_color.g * slot_color.g, skeleton_color.b * slot_color.b, skeleton_color.a * slot_color.a);
		SpineRendererObject *renderer_object;

		if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti))
		{
			const auto region = static_cast<spine::RegionAttachment*>(attachment);

			mesh_instance->prepare_mesh(
				4,
				QUAD_INDICES,
				QUAD_INDICES_LENGTH);

			region->computeWorldVertices(
				*slot,
				mesh_instance->get_vertex_buffer_rw<float>(),
				mesh_instance->get_vertex_layout().offset / sizeof(float),
				mesh_instance->get_vertex_layout().stride / sizeof(float));

			renderer_object = static_cast<SpineRendererObject*>(static_cast<spine::AtlasRegion*>(region->getRegion())->page->texture);

			auto attachment_color = region->getColor();
			tint.r *= attachment_color.r;
			tint.g *= attachment_color.g;
			tint.b *= attachment_color.b;
			tint.a *= attachment_color.a;

			mesh_instance->assign_uvs_and_color(
				region->getUVs(),
				tint);
			
			mesh_instance->update_normals(
				slot->getBone().getScaleX() * region->getScaleX(),
				slot->getBone().getScaleY() * region->getScaleY());
		}
		else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti))
		{
			const auto mesh = static_cast<spine::MeshAttachment*>(attachment);

			mesh_instance->prepare_mesh(
				// prepare_mesh expects number of vertices, getWorldVerticesLength returns number of elements
				static_cast<int>(mesh->getWorldVerticesLength()) / 2,
				mesh->getTriangles().buffer(),
				mesh->getTriangles().size());
			
			mesh->computeWorldVertices(
				*slot,
				0,
				mesh->getWorldVerticesLength(),
				mesh_instance->get_vertex_buffer_rw<float>(),
				mesh_instance->get_vertex_layout().offset / sizeof(float),
				mesh_instance->get_vertex_layout().stride / sizeof(float));

			renderer_object = static_cast<SpineRendererObject*>(static_cast<spine::AtlasRegion*>(mesh->getRegion())->page->texture);

			auto attachment_color = mesh->getColor();
			tint.r *= attachment_color.r;
			tint.g *= attachment_color.g;
			tint.b *= attachment_color.b;
			tint.a *= attachment_color.a;
			
			mesh_instance->assign_uvs_and_color(
				mesh->getUVs(),
				tint);
			
			mesh_instance->update_normals(
				slot->getBone().getScaleX(),
				slot->getBone().getScaleY());
		}
		else if (attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti))
		{
			auto clip = (spine::ClippingAttachment *) attachment;
			skeleton_clipper->clipStart(*slot, clip);
			continue;
		}
		else
		{
			skeleton_clipper->clipEnd(*slot);
			continue;
		}

		if (skeleton_clipper->isClipping())
		{
			// Clip the mesh if necessary
			const auto clip_occurred = skeleton_clipper->clipTriangles(
				mesh_instance->get_vertex_buffer<float>(),
				mesh_instance->get_vertex_layout().offset / sizeof(float),
				mesh_instance->get_vertex_layout().stride / sizeof(float),
				mesh_instance->get_index_buffer<uint16_t>(),
				mesh_instance->get_index_count(),
				mesh_instance->get_attribute_buffer<float>(), // Reinterpreting the attribute buffer as float works, because color and uvs use 4 bytes each. Otherwise it would break
				mesh_instance->get_uv_layout().offset / sizeof(float),
				mesh_instance->get_uv_layout().stride / sizeof(float));
			
			if (skeleton_clipper->getClippedTriangles().size() == 0)
			{
				skeleton_clipper->clipEnd(*slot);
				continue;
			}

			// Update the mesh with the new clipped data
			if (clip_occurred)
			{
				auto& clipped_vertices = skeleton_clipper->getClippedVertices();
				auto& clipped_uvs = skeleton_clipper->getClippedUVs();
				auto& clipped_indices = skeleton_clipper->getClippedTriangles();
				const auto vertex_count = static_cast<int>(clipped_vertices.size()) / 2;
				mesh_instance->prepare_mesh(vertex_count, clipped_indices.buffer(), clipped_indices.size());
				mesh_instance->assign_vertices(clipped_vertices);
				mesh_instance->assign_uvs_and_color(clipped_uvs, tint);
			}
		}

		if (mesh_instance->get_index_count() > 0)
		{
			mesh_instance->renderer_object = renderer_object;

			spine::BlendMode blend_mode = slot->getData().getBlendMode();
			Ref<Material> custom_material;

			// See if we have a slot node for this slot with a custom material
			auto &nodes = slot_nodes[slot->getData().getIndex()];
			if (nodes.size() > 0)
			{
				auto slot_node = nodes[0];
				if (slot_node)
				{
					switch (blend_mode)
					{
						case spine::BlendMode_Normal:
							custom_material = slot_node->get_normal_material();
							break;
						case spine::BlendMode_Additive:
							custom_material = slot_node->get_additive_material();
							break;
						case spine::BlendMode_Multiply:
							custom_material = slot_node->get_multiply_material();
							break;
						case spine::BlendMode_Screen:
							custom_material = slot_node->get_screen_material();
							break;
					}
				}
			}

			// Else, check if we have a material on the sprite itself
			if (!custom_material.is_valid())
			{
				switch (blend_mode) {
					case spine::BlendMode_Normal:
						custom_material = normal_material;
						break;
					case spine::BlendMode_Additive:
						custom_material = additive_material;
						break;
					case spine::BlendMode_Multiply:
						custom_material = multiply_material;
						break;
					case spine::BlendMode_Screen:
						custom_material = screen_material;
						break;
				}
			}

			// Set the custom material, or the default material
			if (custom_material.is_valid())
				mesh_instance->set_material(custom_material);
			
			mesh_instance->update_mesh();
			mesh_instance->set_visible(true);
		}
		skeleton_clipper->clipEnd(*slot);
	}
	skeleton_clipper->clipEnd();
}

void createLinesFromMesh(PackedVector2Array &scratch_points, spine::Vector<unsigned short> &triangles, spine::Vector<float> *vertices)
{
	scratch_points.resize(0);
	for (int i = 0; i < triangles.size(); i += 3)
	{
		int i1 = triangles[i];
		int i2 = triangles[i + 1];
		int i3 = triangles[i + 2];
		Vector2 v1(vertices->buffer()[i1 * 2], vertices->buffer()[i1 * 2 + 1]);
		Vector2 v2(vertices->buffer()[i2 * 2], vertices->buffer()[i2 * 2 + 1]);
		Vector2 v3(vertices->buffer()[i3 * 2], vertices->buffer()[i3 * 2 + 1]);
		scratch_points.push_back(v1);
		scratch_points.push_back(v2);
		scratch_points.push_back(v2);
		scratch_points.push_back(v3);
		scratch_points.push_back(v3);
		scratch_points.push_back(v1);
	}
}

void SpineSprite::callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event)
{
	Ref<SpineTrackEntry> entry_ref = Ref<SpineTrackEntry>(memnew(SpineTrackEntry));
	entry_ref->set_spine_object(this, entry);

	Ref<SpineEvent> event_ref(nullptr);
	if (event)
	{
		event_ref = Ref<SpineEvent>(memnew(SpineEvent));
		event_ref->set_spine_object(this, event);
	}

	switch (type)
	{
		case spine::EventType_Start:
			emit_signal(SNAME("animation_started"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Interrupt:
			emit_signal(SNAME("animation_interrupted"), this, animation_state, entry_ref);
			break;
		case spine::EventType_End:
			emit_signal(SNAME("animation_ended"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Complete:
			emit_signal(SNAME("animation_completed"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Dispose:
			emit_signal(SNAME("animation_disposed"), this, animation_state, entry_ref);
			break;
		case spine::EventType_Event:
			emit_signal(SNAME("animation_event"), this, animation_state, entry_ref, event_ref);
			break;
	}
}

Transform3D SpineSprite::get_global_bone_transform(const String &bone_name)
{
	if (!animation_state.is_valid() && !skeleton.is_valid())
		return get_global_transform();
	
	auto bone = skeleton->find_bone(bone_name);
	if (!bone.is_valid())
		return get_global_transform();

	return bone->get_global_transform();
}

void SpineSprite::set_global_bone_transform(const String &bone_name, Transform3D transform)
{
	if (!animation_state.is_valid() && !skeleton.is_valid())
		return;
	
	auto bone = skeleton->find_bone(bone_name);
	if (!bone.is_valid())
		return;
	
	bone->set_global_transform(transform);
}

SpineConstant::UpdateMode SpineSprite::get_update_mode()
{
	return update_mode;
}

void SpineSprite::set_update_mode(SpineConstant::UpdateMode v)
{
	update_mode = v;
	set_process_internal(update_mode == SpineConstant::UpdateMode_Process);
	set_physics_process_internal(update_mode == SpineConstant::UpdateMode_Physics);
}

Ref<SpineSkin> SpineSprite::new_skin(const String &name)
{
	Ref<SpineSkin> skin = memnew(SpineSkin);
	skin->init(name, this);
	return skin;
}

Ref<Material> SpineSprite::get_normal_material()
{
	return normal_material;
}

void SpineSprite::set_normal_material(Ref<Material> material)
{
	normal_material = material;
}

Ref<Material> SpineSprite::get_additive_material()
{
	return additive_material;
}

void SpineSprite::set_additive_material(Ref<Material> material)
{
	additive_material = material;
}

Ref<Material> SpineSprite::get_multiply_material()
{
	return multiply_material;
}

void SpineSprite::set_multiply_material(Ref<Material> material)
{
	multiply_material = material;
}

Ref<Material> SpineSprite::get_screen_material()
{
	return screen_material;
}

void SpineSprite::set_screen_material(Ref<Material> material)
{
	screen_material = material;
}

void SpineSprite::set_time_scale(float time_scale)
{
	this->time_scale = time_scale;
}

float SpineSprite::get_time_scale()
{
	return time_scale;
}

void SpineSprite::set_z_spacing(float value)
{
	z_spacing = value;
}

float SpineSprite::get_z_spacing()
{
	return z_spacing;
}

void SpineSprite::set_use_aabb_sorting(bool value)
{
	use_aabb_sorting = value;
	for (const auto mesh_instance : mesh_instances)
	{
		mesh_instance->set_sorting_use_aabb_center(value);
	}
}

void SpineSprite::set_use_sorting_offset(bool value)
{
	use_sorting_offset = value;
	if (!value)
	{
		// Reset if not using sorting offset
		for (const auto mesh_instance : mesh_instances)
		{
			mesh_instance->set_sorting_offset(0.f);
		}
	}
}

#ifndef SPINE_GODOT_EXTENSION
// FIXME
#ifdef TOOLS_ENABLED
Rect2 SpineSprite::_edit_get_rect() const {
	if (skeleton_data_res.is_valid() && skeleton_data_res->is_skeleton_data_loaded()) {
		auto data = skeleton_data_res->get_skeleton_data();
		return Rect2(data->getX(), -data->getY() - data->getHeight(), data->getWidth(), data->getHeight());
	}
	return Node2D::_edit_get_rect();
}

bool SpineSprite::_edit_use_rect() const {
	return skeleton_data_res.is_valid() && skeleton_data_res->is_skeleton_data_loaded();
}
#endif
#endif