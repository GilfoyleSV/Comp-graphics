#include "application.hpp"

#include <vector>
#include <fstream>
#include <iostream>
#include <cmath>

#include <imgui.h>
#include "math3d.hpp"

namespace application {

namespace {

struct Vertex {
	math::Vec3 pos;
	math::Vec3 color;
};

struct UniformBufferObject {
	math::Mat4 model;
	math::Mat4 view;
	math::Mat4 proj;
	math::Vec4 baseColor;
};

// Процедурный расчёт цвета для вершины на основе локальных координат (Задание 5)
math::Vec3 calcProceduralColor(const math::Vec3& p) {
	float r = (p.x + 0.6f) / 1.2f;
	float g = (p.y + 0.5f) / 1.2f;
	float b = (p.z + 0.6f) / 1.2f;
	return {
		0.2f + 0.8f * r,
		0.2f + 0.8f * g,
		0.2f + 0.8f * b
	};
}

// Построение геометрии пирамиды с 4-угольным основанием (Вариант 6)
std::vector<Vertex> buildPyramidVertices() {
	math::Vec3 top(0.0f, 0.7f, 0.0f);
	math::Vec3 fl(-0.6f, -0.5f,  0.6f); // Front-Left
	math::Vec3 fr( 0.6f, -0.5f,  0.6f); // Front-Right
	math::Vec3 br( 0.6f, -0.5f, -0.6f); // Back-Right
	math::Vec3 bl(-0.6f, -0.5f, -0.6f); // Back-Left

	auto addTri = [](std::vector<Vertex>& v, const math::Vec3& a, const math::Vec3& b, const math::Vec3& c) {
		v.push_back({ a, calcProceduralColor(a) });
		v.push_back({ b, calcProceduralColor(b) });
		v.push_back({ c, calcProceduralColor(c) });
	};

	std::vector<Vertex> vertices;
	// 4 боковые грани (треугольники)
	addTri(vertices, top, fl, fr); // Передняя
	addTri(vertices, top, fr, br); // Правая
	addTri(vertices, top, br, bl); // Задняя
	addTri(vertices, top, bl, fl); // Левая

	// Основание (2 треугольника)
	addTri(vertices, fl, bl, br);
	addTri(vertices, fl, br, fr);

	return vertices;
}

// Vulkan ресурсы
std::vector<Vertex> pyramid_vertices;
uint32_t vertex_count = 0;

VkBuffer vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vertex_buffer_alloc = VK_NULL_HANDLE;

VkBuffer ubo_buffer = VK_NULL_HANDLE;
VmaAllocation ubo_buffer_alloc = VK_NULL_HANDLE;

VkDescriptorSetLayout desc_set_layout = VK_NULL_HANDLE;
VkDescriptorPool desc_pool = VK_NULL_HANDLE;
VkDescriptorSet desc_set = VK_NULL_HANDLE;

VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline graphics_pipeline = VK_NULL_HANDLE;

// Состояние интерфейса и параметры (Задания 1–5)
// Задание 1: Проекция
int projection_type = 0; // 0 = Перспективная, 1 = Ортографическая
float fov_deg = 45.0f;
float ortho_size = 2.0f;
float near_plane = 0.1f;
float far_plane = 100.0f;

// Задание 2: Позиция, поворот, масштаб
float position[3] = { 0.0f, 0.0f, 0.0f };
float rotation_deg[3] = { 0.0f, 0.0f, 0.0f };
float scale[3] = { 1.0f, 1.0f, 1.0f };
bool uniform_scale = true;
float uniform_scale_val = 1.0f;

// Задание 3: Сложная траектория и анимация
bool animation_enabled = false;
float animation_speed = 1.0f;
float trajectory_radius = 1.5f;
float trajectory_height = 0.5f;
int trajectory_type = 0; // 0 = Лемниската ("восьмерка"), 1 = Окружность, 2 = 3D Спираль
double anim_time = 0.0;
double last_time = 0.0;

// Задание 4: Изменение цвета
float base_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

// Вспомогательная функция чтения файла
std::vector<char> readBinaryFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Failed to open shader file: " << filename << std::endl;
		return {};
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
	if (code.empty()) return VK_NULL_HANDLE;

	VkShaderModuleCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size(),
		.pCode = reinterpret_cast<const uint32_t*>(code.data()),
	};

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan shader module\n";
		return VK_NULL_HANDLE;
	}
	return module;
}

} // namespace

bool initialize() {
	auto& context = graphics::internal::context;

	// 1. Создание вершинного буфера
	pyramid_vertices = buildPyramidVertices();
	vertex_count = static_cast<uint32_t>(pyramid_vertices.size());
	VkDeviceSize buffer_size = sizeof(Vertex) * vertex_count;

	VkBufferCreateInfo vb_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = buffer_size,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VmaAllocationCreateInfo vb_alloc_info = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateBuffer(context.allocator, &vb_info, &vb_alloc_info,
	                    &vertex_buffer, &vertex_buffer_alloc, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create vertex buffer\n";
		return false;
	}

	void* mapped_data = nullptr;
	vmaMapMemory(context.allocator, vertex_buffer_alloc, &mapped_data);
	std::memcpy(mapped_data, pyramid_vertices.data(), buffer_size);
	vmaUnmapMemory(context.allocator, vertex_buffer_alloc);

	// 2. Создание Uniform буфера
	VkBufferCreateInfo ubo_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(UniformBufferObject),
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VmaAllocationCreateInfo ubo_alloc_info = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateBuffer(context.allocator, &ubo_info, &ubo_alloc_info,
	                    &ubo_buffer, &ubo_buffer_alloc, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create uniform buffer\n";
		return false;
	}

	// 3. Создание Descriptor Set Layout
	VkDescriptorSetLayoutBinding ubo_layout_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &ubo_layout_binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &desc_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}

	// 4. Создание Descriptor Pool и Descriptor Set
	VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
	};
	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &desc_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	VkDescriptorSetAllocateInfo alloc_set_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = desc_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &desc_set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &alloc_set_info, &desc_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor set\n";
		return false;
	}

	VkDescriptorBufferInfo d_buf_info = {
		.buffer = ubo_buffer,
		.offset = 0,
		.range = sizeof(UniformBufferObject),
	};
	VkWriteDescriptorSet descriptor_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = desc_set,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &d_buf_info,
	};
	vkUpdateDescriptorSets(context.device, 1, &descriptor_write, 0, nullptr);

	// 5. Создание Pipeline Layout
	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &desc_set_layout,
	};
	if (vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	// 6. Загрузка шейдеров
	auto vert_code = readBinaryFile("shaders/pyramid.vert.spv");
	auto frag_code = readBinaryFile("shaders/pyramid.frag.spv");
	if (vert_code.empty() || frag_code.empty()) {
		std::cerr << "Shader binaries are missing! Check CMake shader build.\n";
		return false;
	}

	VkShaderModule vert_module = createShaderModule(context.device, vert_code);
	VkShaderModule frag_module = createShaderModule(context.device, frag_code);

	VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vert_module,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag_module,
			.pName = "main",
		},
	};

	// 7. Настройка вершинного ввода
	VkVertexInputBindingDescription binding_description = {
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	VkVertexInputAttributeDescription attribute_descriptions[2] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, pos),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, color),
		},
	};
	VkPipelineVertexInputStateCreateInfo vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding_description,
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = attribute_descriptions,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo rasterizer = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE, // Видны все стороны пирамиды
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisampling = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
	};

	VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo color_blending = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};

	VkGraphicsPipelineCreateInfo pipeline_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blending,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &graphics_pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline for pyramid\n";
		vkDestroyShaderModule(context.device, frag_module, nullptr);
		vkDestroyShaderModule(context.device, vert_module, nullptr);
		return false;
	}

	vkDestroyShaderModule(context.device, frag_module, nullptr);
	vkDestroyShaderModule(context.device, vert_module, nullptr);

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	if (graphics_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(context.device, graphics_pipeline, nullptr);
		graphics_pipeline = VK_NULL_HANDLE;
	}
	if (pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
		pipeline_layout = VK_NULL_HANDLE;
	}
	if (desc_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(context.device, desc_pool, nullptr);
		desc_pool = VK_NULL_HANDLE;
	}
	if (desc_set_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(context.device, desc_set_layout, nullptr);
		desc_set_layout = VK_NULL_HANDLE;
	}
	if (ubo_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, ubo_buffer, ubo_buffer_alloc);
		ubo_buffer = VK_NULL_HANDLE;
	}
	if (vertex_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_buffer_alloc);
		vertex_buffer = VK_NULL_HANDLE;
	}
}

void update(double current_time) {
	double dt = current_time - last_time;
	if (last_time == 0.0) dt = 0.0;
	last_time = current_time;

	// Обновление сложной анимации и траектории (Задание 3)
	if (animation_enabled) {
		anim_time += dt * animation_speed;
		float t = static_cast<float>(anim_time);

		if (trajectory_type == 0) {
			// Лемниската ("восьмерка")
			position[0] = trajectory_radius * std::cos(t);
			position[2] = trajectory_radius * std::sin(t) * std::cos(t);
			position[1] = trajectory_height * std::sin(2.0f * t);
		} else if (trajectory_type == 1) {
			// Круговая орбита
			position[0] = trajectory_radius * std::cos(t);
			position[2] = trajectory_radius * std::sin(t);
			position[1] = trajectory_height * std::sin(t);
		} else {
			// 3D Спираль / волна
			position[0] = trajectory_radius * std::cos(t * 1.5f);
			position[2] = trajectory_radius * std::sin(t * 1.5f);
			position[1] = trajectory_height * std::sin(t * 3.0f);
		}

		// Автоматическое вращение во время движения
		rotation_deg[1] += 45.0f * animation_speed * static_cast<float>(dt);
		rotation_deg[0] += 25.0f * animation_speed * static_cast<float>(dt);
		while (rotation_deg[1] >= 360.0f) rotation_deg[1] -= 360.0f;
		while (rotation_deg[0] >= 360.0f) rotation_deg[0] -= 360.0f;
	}

	// ImGui Controls Panel
	ImGui::Begin("Lab #1: 3D Pyramid (Variant 6)", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	// 1. Projection (Requirement 1)
	if (ImGui::CollapsingHeader("1. Projection Matrix", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::RadioButton("Perspective", &projection_type, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Orthographic", &projection_type, 1);

		if (projection_type == 0) {
			ImGui::SliderFloat("FOV (degrees)", &fov_deg, 20.0f, 120.0f);
		} else {
			ImGui::SliderFloat("Ortho Size", &ortho_size, 0.5f, 10.0f);
		}
		ImGui::DragFloat("Near Plane", &near_plane, 0.05f, 0.01f, 10.0f);
		ImGui::DragFloat("Far Plane", &far_plane, 1.0f, 10.0f, 500.0f);
	}

	// 2. Affine Transformations (Requirement 2)
	if (ImGui::CollapsingHeader("2. Affine Transformations", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Position (X, Y, Z)", position, 0.05f, -10.0f, 10.0f);
		ImGui::DragFloat3("Rotation (deg)", rotation_deg, 1.0f, -360.0f, 360.0f);

		ImGui::Checkbox("Uniform Scale", &uniform_scale);
		if (uniform_scale) {
			if (ImGui::DragFloat("Scale", &uniform_scale_val, 0.05f, 0.1f, 10.0f)) {
				scale[0] = scale[1] = scale[2] = uniform_scale_val;
			}
		} else {
			ImGui::DragFloat3("Scale (X, Y, Z)", scale, 0.05f, 0.1f, 10.0f);
		}

		if (ImGui::Button("Reset Transformations")) {
			position[0] = position[1] = position[2] = 0.0f;
			rotation_deg[0] = rotation_deg[1] = rotation_deg[2] = 0.0f;
			scale[0] = scale[1] = scale[2] = uniform_scale_val = 1.0f;
		}
	}

	// 3. Complex Trajectory & Animation (Requirement 3)
	if (ImGui::CollapsingHeader("3. Animation & Trajectory", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (animation_enabled) {
			if (ImGui::Button("Pause")) {
				animation_enabled = false;
			}
		} else {
			if (ImGui::Button("Play")) {
				animation_enabled = true;
			}
		}
		ImGui::SameLine();
		ImGui::Text(animation_enabled ? "Status: Playing" : "Status: Paused");

		const char* trajectories[] = { "Lemniscate (Figure 8)", "Circular Orbit", "3D Spiral" };
		ImGui::Combo("Trajectory Type", &trajectory_type, trajectories, IM_COUNTOF(trajectories));

		ImGui::SliderFloat("Animation Speed", &animation_speed, 0.1f, 5.0f);
		ImGui::SliderFloat("Trajectory Radius", &trajectory_radius, 0.5f, 4.0f);
		ImGui::SliderFloat("Trajectory Height", &trajectory_height, 0.0f, 2.0f);
	}

	// 4 & 5. Color & Procedural vertex colors (Requirements 4 & 5)
	if (ImGui::CollapsingHeader("4-5. Figure Color & Procedural Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit4("Base Color", base_color);
		ImGui::TextWrapped("Vertex colors are procedurally calculated from local coordinates (X, Y, Z) - Requirement 5.\n"
		                   "The selected Base Color is multiplied by the vertex colors.");
	}

	ImGui::End();

	// Расчет матриц Model, View, Projection
	auto& context = graphics::internal::context;
	float aspect = 1.0f;
	if (context.swapchain_extent.height > 0) {
		aspect = static_cast<float>(context.swapchain_extent.width) / static_cast<float>(context.swapchain_extent.height);
	}

	math::Mat4 m_model = math::Mat4::translate({ position[0], position[1], position[2] }) *
	                     math::Mat4::rotateZ(math::radians(rotation_deg[2])) *
	                     math::Mat4::rotateY(math::radians(rotation_deg[1])) *
	                     math::Mat4::rotateX(math::radians(rotation_deg[0])) *
	                     math::Mat4::scale({ scale[0], scale[1], scale[2] });

	math::Mat4 m_view = math::Mat4::lookAt({ 0.0f, 1.8f, 3.8f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

	math::Mat4 m_proj;
	if (projection_type == 0) {
		m_proj = math::Mat4::perspective(math::radians(fov_deg), aspect, near_plane, far_plane);
	} else {
		m_proj = math::Mat4::orthographic(-ortho_size * aspect, ortho_size * aspect,
		                                  -ortho_size, ortho_size, near_plane, far_plane);
	}

	UniformBufferObject ubo = {
		.model = m_model,
		.view = m_view,
		.proj = m_proj,
		.baseColor = { base_color[0], base_color[1], base_color[2], base_color[3] },
	};

	// Запись обновленных данных в UBO
	void* mapped_ubo = nullptr;
	vmaMapMemory(context.allocator, ubo_buffer_alloc, &mapped_ubo);
	std::memcpy(mapped_ubo, &ubo, sizeof(ubo));
	vmaUnmapMemory(context.allocator, ubo_buffer_alloc);
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(fd.command_buffer, &begin_info);

	const VkClearValue clear_values[2] = {
		{ .color = { { 0.12f, 0.14f, 0.18f, 1.0f } } }, // Приятный темно-серый фон
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = 2,
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	// Отрисовка 3D пирамиды
	if (graphics_pipeline != VK_NULL_HANDLE) {
		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

		VkViewport viewport = {
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(context.swapchain_extent.width),
			.height = static_cast<float>(context.swapchain_extent.height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

		VkRect2D scissor = {
			.offset = { 0, 0 },
			.extent = context.swapchain_extent,
		};
		vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, offsets);
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        pipeline_layout, 0, 1, &desc_set, 0, nullptr);

		vkCmdDraw(fd.command_buffer, vertex_count, 1, 0, 0);
	}

	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application