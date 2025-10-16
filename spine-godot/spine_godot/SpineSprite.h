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

#pragma once

#include "SpineSkeleton.h"
#include "SpineAnimationState.h"
#include "SpineCommon.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/visual_instance3d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/canvas_item_material.hpp>

class SpineSlotNode;

struct SpineRendererObject;

class SpineSprite;

class Attachment;

struct ElementLayout
{
	uint32_t offset = 0;
	uint32_t stride = 0;

	ElementLayout() = default;
	ElementLayout(RS::ArrayFormat format, size_t vertex_count, int32_t type);

	uint32_t calculate_buffer_index(int element_index)
	{
		return element_index * stride + offset;
	}
};

struct CompressedNormalTangent
{
	uint16_t na;
	uint16_t nb;
	uint16_t ta;
	uint16_t tb;
};

class SpineMesh3D : public VisualInstance3D {
	GDCLASS(SpineMesh3D, VisualInstance3D);

	friend class SpineSprite;

protected:

	static const auto ELEMENT_SIZE_POSITION = sizeof(godot::Vector2);
	static const auto ELEMENT_SIZE_NORMAL_TANGENT = sizeof(CompressedNormalTangent);
	static const auto ELEMENT_SIZE_UV = sizeof(godot::Vector2);
	static const auto ELEMENT_SIZE_COLOR = sizeof(int32_t);

	static const auto VERTEX_ELEMENT_SIZE = ELEMENT_SIZE_POSITION + ELEMENT_SIZE_NORMAL_TANGENT;
	static const auto ATTRIB_ELEMENT_SIZE = ELEMENT_SIZE_UV + ELEMENT_SIZE_COLOR;
	static const auto INDEX_ELEMENT_SIZE = sizeof(uint16_t);

	static const uint64_t SURFACE_FORMAT =
		RS::ARRAY_FORMAT_VERTEX |
		RS::ARRAY_FORMAT_NORMAL |
		RS::ARRAY_FORMAT_TANGENT |
		RS::ARRAY_FORMAT_COLOR |
		RS::ARRAY_FORMAT_TEX_UV |
		RS::ARRAY_FORMAT_INDEX |
		RS::ARRAY_FLAG_USE_2D_VERTICES |
		RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE |
		RS::ARRAY_FLAG_FORMAT_CURRENT_VERSION;

	void _notification(int what);
	static void _bind_methods();

	SpineRendererObject *renderer_object;

	RID mesh;
	Ref<Material> material;
	PackedByteArray vertex_buffer;
	PackedByteArray attribute_buffer;
	PackedByteArray index_buffer;
	ElementLayout vertex_layout;
	ElementLayout normal_layout;
	ElementLayout tangent_layout;
	ElementLayout uv_layout;
	ElementLayout color_layout;
	float winding;

public:
	SpineMesh3D();
	~SpineMesh3D();

	const ElementLayout& get_vertex_layout() const { return vertex_layout; }
	const ElementLayout& get_normal_layout() const { return normal_layout; }
	const ElementLayout& get_tangent_layout() const { return tangent_layout; }
	const ElementLayout& get_uv_layout() const { return uv_layout; }
	const ElementLayout& get_color_layout() const { return color_layout; }

	template<typename T>
	T* get_vertex_buffer_rw() { return reinterpret_cast<T*>(vertex_buffer.ptrw()); }

	template<typename T>
	const T* get_vertex_buffer() const { return reinterpret_cast<const T*>(vertex_buffer.ptr()); }

	template<typename T>
	const T* get_index_buffer() const { return reinterpret_cast<const T*>(index_buffer.ptr()); }

	template<typename T>
	const T* get_attribute_buffer() const { return reinterpret_cast<const T*>(attribute_buffer.ptr()); }

	size_t get_vertex_count() const { return vertex_buffer.size() / VERTEX_ELEMENT_SIZE; }
	size_t get_index_count() const { return index_buffer.size() / INDEX_ELEMENT_SIZE; }

	bool prepare_mesh(int new_vertex_count, const uint16_t* new_indices, int new_index_count);
	void assign_vertices(spine::Vector<float>& new_vertices);
	void assign_uvs_and_color(spine::Vector<float>& new_uvs, spine::Color new_color);
	void update_normals(float attachment_scale_x, float attachment_scale_y, bool force);
	void update_mesh();

	void set_material(Ref<Material> new_material);
};

class SpineSprite : public Node3D,
					public spine::AnimationStateListenerObject {
	GDCLASS(SpineSprite, Node3D)

	friend class SpineBone;

protected:
	Ref<SpineSkeletonDataResource> skeleton_data_res;
	Ref<SpineSkeleton> skeleton;
	Ref<SpineAnimationState> animation_state;
	SpineConstant::UpdateMode update_mode;
	float time_scale;
	float z_spacing = 0.f;
	int layers = 1;

	String preview_skin;
	String preview_animation;
	bool preview_frame;
	float preview_time;

	bool use_sorting_offset = true;
	float sorting_offset_multiplier = .1f;
	bool use_aabb_sorting = false;
	
	mutable HashMap<StringName, Variant> instance_shader_parameters;
	mutable HashMap<StringName, StringName> instance_shader_parameter_property_remap;

	spine::Vector<spine::Vector<SpineSlotNode *>> slot_nodes;
	Vector<SpineMesh3D *> mesh_instances;
	Ref<Material> normal_material;
	Ref<Material> additive_material;
	Ref<Material> multiply_material;
	Ref<Material> screen_material;
	spine::SkeletonClipping *skeleton_clipper;
	bool modified_bones;

	static void _bind_methods();
	void _notification(int what);
	void _get_property_list(List<godot::PropertyInfo> *list) const;
	bool _get(const StringName &property, Variant &value) const;
	bool _set(const StringName &property, const Variant &value);

	void generate_meshes_for_slots(Ref<SpineSkeleton> skeleton_ref);
	void remove_meshes();
	void sort_slot_nodes();
	void update_meshes(Ref<SpineSkeleton> skeleton_ref);
	void set_modified_bones() { modified_bones = true; }

	void callback(spine::AnimationState *state, spine::EventType type, spine::TrackEntry *entry, spine::Event *event) override;

	const StringName* _instance_uniform_get_remap(const StringName &p_name) const;

public:
	SpineSprite();
	~SpineSprite();

	void set_skeleton_data_res(const Ref<SpineSkeletonDataResource> &_spine_skeleton_data_resource);

	Ref<SpineSkeletonDataResource> get_skeleton_data_res();

	Ref<SpineSkeleton> get_skeleton();

	Ref<SpineAnimationState> get_animation_state();

	void on_skeleton_data_changed();

	void update_skeleton(float delta);

	Transform3D get_global_bone_transform(const String &bone_name);
	void set_global_bone_transform(const String &bone_name, Transform3D transform);

	SpineConstant::UpdateMode get_update_mode();

	void set_update_mode(SpineConstant::UpdateMode v);

	Ref<SpineSkin> new_skin(const String &name);

	Ref<Material> get_normal_material();
	void set_normal_material(Ref<Material> material);

	Ref<Material> get_additive_material();
	void set_additive_material(Ref<Material> material);

	Ref<Material> get_multiply_material();
	void set_multiply_material(Ref<Material> material);

	Ref<Material> get_screen_material();
	void set_screen_material(Ref<Material> material);

	void set_time_scale(float time_scale);
	float get_time_scale();

	void set_z_spacing(float value);
	float get_z_spacing();

	void set_layer_mask(int mask);
	int get_layer_mask();

	bool get_use_aabb_sorting() const { return use_aabb_sorting; }
	void set_use_aabb_sorting(bool value);

	bool get_use_sorting_offset() const { return use_sorting_offset; }
	void set_use_sorting_offset(bool value);

	float get_sorting_offset_multiplier() const { return sorting_offset_multiplier; }
	void set_sorting_offset_multiplier(float value) { sorting_offset_multiplier = value; }

	void set_instance_shader_parameter(const StringName &p_name, const Variant &p_value);
	Variant get_instance_shader_parameter(const StringName &p_name) const;

#ifndef SPINE_GODOT_EXTENSION
// FIXME
#ifdef TOOLS_ENABLED
	virtual Rect2 _edit_get_rect() const;
	virtual bool _edit_use_rect() const;
#endif
#endif
};
