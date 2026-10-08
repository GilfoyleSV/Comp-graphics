#pragma once

#include <cmath>
#include <cstring>

namespace math {

constexpr float PI = 3.14159265358979323846f;

inline float radians(float deg) {
	return deg * (PI / 180.0f);
}

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	Vec3() = default;
	Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

	Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
	Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
	Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

	float length() const {
		return std::sqrt(x * x + y * y + z * z);
	}

	Vec3 normalized() const {
		float l = length();
		if (l > 0.00001f) {
			return { x / l, y / l, z / l };
		}
		return { 0.0f, 0.0f, 0.0f };
	}

	static float dot(const Vec3& a, const Vec3& b) {
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	static Vec3 cross(const Vec3& a, const Vec3& b) {
		return {
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		};
	}
};

struct Vec4 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 0.0f;

	Vec4() = default;
	Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
	Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
};

// Column-major 4x4 Matrix (matches GLSL mat4 layout)
// data[col][row]
struct Mat4 {
	float data[4][4] = { 0 };

	static Mat4 identity() {
		Mat4 m;
		m.data[0][0] = 1.0f;
		m.data[1][1] = 1.0f;
		m.data[2][2] = 1.0f;
		m.data[3][3] = 1.0f;
		return m;
	}

	static Mat4 translate(const Vec3& t) {
		Mat4 m = identity();
		m.data[3][0] = t.x;
		m.data[3][1] = t.y;
		m.data[3][2] = t.z;
		return m;
	}

	static Mat4 scale(const Vec3& s) {
		Mat4 m = identity();
		m.data[0][0] = s.x;
		m.data[1][1] = s.y;
		m.data[2][2] = s.z;
		return m;
	}

	static Mat4 rotateX(float angle_rad) {
		Mat4 m = identity();
		float c = std::cos(angle_rad);
		float s = std::sin(angle_rad);
		m.data[1][1] = c;
		m.data[1][2] = s;
		m.data[2][1] = -s;
		m.data[2][2] = c;
		return m;
	}

	static Mat4 rotateY(float angle_rad) {
		Mat4 m = identity();
		float c = std::cos(angle_rad);
		float s = std::sin(angle_rad);
		m.data[0][0] = c;
		m.data[0][2] = -s;
		m.data[2][0] = s;
		m.data[2][2] = c;
		return m;
	}

	static Mat4 rotateZ(float angle_rad) {
		Mat4 m = identity();
		float c = std::cos(angle_rad);
		float s = std::sin(angle_rad);
		m.data[0][0] = c;
		m.data[0][1] = s;
		m.data[1][0] = -s;
		m.data[1][1] = c;
		return m;
	}

	Mat4 operator*(const Mat4& b) const {
		Mat4 result;
		for (int c = 0; c < 4; ++c) {
			for (int r = 0; r < 4; ++r) {
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k) {
					sum += data[k][r] * b.data[c][k];
				}
				result.data[c][r] = sum;
			}
		}
		return result;
	}

	static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
		Vec3 f = (center - eye).normalized();
		Vec3 s = Vec3::cross(f, up).normalized();
		Vec3 u = Vec3::cross(s, f);

		Mat4 m = identity();
		m.data[0][0] = s.x;
		m.data[1][0] = s.y;
		m.data[2][0] = s.z;
		m.data[3][0] = -Vec3::dot(s, eye);

		m.data[0][1] = u.x;
		m.data[1][1] = u.y;
		m.data[2][1] = u.z;
		m.data[3][1] = -Vec3::dot(u, eye);

		m.data[0][2] = -f.x;
		m.data[1][2] = -f.y;
		m.data[2][2] = -f.z;
		m.data[3][2] = Vec3::dot(f, eye);

		return m;
	}

	// Perspective projection for Vulkan (Y points down, Z in [0, 1])
	static Mat4 perspective(float fov_y_rad, float aspect, float zNear, float zFar) {
		Mat4 m;
		float tanHalfFov = std::tan(fov_y_rad / 2.0f);

		m.data[0][0] = 1.0f / (aspect * tanHalfFov);
		m.data[1][1] = -1.0f / tanHalfFov; // Vulkan Y inverted
		m.data[2][2] = zFar / (zNear - zFar);
		m.data[2][3] = -1.0f;
		m.data[3][2] = (zFar * zNear) / (zNear - zFar);
		return m;
	}

	// Orthographic projection for Vulkan (Y points down, Z in [0, 1])
	static Mat4 orthographic(float left, float right, float bottom, float top, float zNear, float zFar) {
		Mat4 m;
		m.data[0][0] = 2.0f / (right - left);
		m.data[1][1] = -2.0f / (top - bottom); // Vulkan Y inverted
		m.data[2][2] = 1.0f / (zNear - zFar);
		m.data[3][0] = -(right + left) / (right - left);
		m.data[3][1] = -(top + bottom) / (top - bottom);
		m.data[3][2] = zNear / (zNear - zFar);
		m.data[3][3] = 1.0f;
		return m;
	}
};

} // namespace math
