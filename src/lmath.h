#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;

struct RGB8
{
    uint8_t r, g, b;
};

struct RGBA8
{
    uint8_t r, g, b, a;
};

struct RGB32
{
    uint32_t r, g, b;
};

struct RGBA32
{
    uint32_t r, g, b, a;
};