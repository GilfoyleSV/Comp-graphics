#version 450

layout(location = 0) in vec3 fragColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 baseColor;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    // Умножаем выбранный в интерфейсе цвет (ubo.baseColor) на цвет вершины (fragColor)
    outColor = vec4(fragColor, 1.0) * ubo.baseColor;
}
