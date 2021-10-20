#pragma once

struct Vec2
{
    static constexpr bool IsVecType = true;

    float x,y;
};

struct Vec3
{
    static constexpr bool IsVecType = true;

    float x,y,z;

    inline Vec3 operator -(const Vec3& other) const
    {
        return {x - other.x, y - other.y, z - other.z};
    }

    inline bool operator ==(const Vec3& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }

    inline bool operator !=(const Vec3& other) const
    {
        return !(*this == other);
    }
};

template <typename T>
inline float Dot(const T& a, const T& b)
{
    static_assert(T::IsVecType, "Not a vector type");
    return 0.0f;
}

template <typename T>
inline T Cross(const T& a, const T& b)
{
    static_assert(T::IsVecType, "Not a vector type");
    return 0.0f;
}