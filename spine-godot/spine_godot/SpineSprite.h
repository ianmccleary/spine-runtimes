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
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/canvas_item_material.hpp>

class SpineSlotNode;

struct SpineRendererObject;

class SpineSprite;

class Attachment;

class SpineMesh3D : public VisualInstance3D {
	GDCLASS(SpineMesh3D, VisualInstance3D);

	friend class SpineSprite;

protected:
	void _notification(int what);
	static void _bind_methods();

	PackedVector2Array vertices;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	SpineRendererObject *renderer_object;

	bool indices_changed;

	RID mesh;
	uint32_t surface_offsets[RS::ARRAY_MAX];
	int num_vertices;
	int num_indices;
	PackedByteArray vertex_buffer;
	PackedByteArray attribute_buffer;
	uint32_t vertex_stride;
	uint32_t normal_tangent_stride;
	uint32_t attribute_stride;

public:
	SpineMesh3D();
	~SpineMesh3D();

	void update_mesh();
	void set_material(Ref<Material> material);
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

	String preview_skin;
	String preview_animation;
	bool preview_frame;
	float preview_time;

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

#ifndef SPINE_GODOT_EXTENSION
// FIXME
#ifdef TOOLS_ENABLED
	virtual Rect2 _edit_get_rect() const;
	virtual bool _edit_use_rect() const;
#endif
#endif

	static void clear_statics();
};
