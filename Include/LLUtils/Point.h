/*
Copyright (c) 2019 Lior Lahav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include "MathUtil.h"
#include "Platform.h"

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
// for casting from POINT and SIZE windows structs
#include <windows.h> 
#endif



namespace LLUtils
{
    template <class POINT_TYPE>
    class Point
    {
        
    public:
        static const Point<POINT_TYPE> Zero;
        static const Point<POINT_TYPE> One;
        
        POINT_TYPE x;
        POINT_TYPE y;

        using point_type = POINT_TYPE;

        uint32_t abs(const uint32_t val) const
        {
            return static_cast<uint32_t>(std::abs(static_cast<long const>(val)));
        }

        template <class T>
        T abs(const T val) const
        {
            return static_cast<T>(std::abs(static_cast<T>(val)));
        }

        Point() {}
        Point(POINT_TYPE aX, POINT_TYPE aY)
        {
            x = aX;
            y = aY;
        }

        Point(POINT_TYPE aT)
        {
            x = aT;
            y = aT;
        }

#pragma region operations
        Point Abs() const
        {
            using namespace std;
            return Point(abs(x), abs(y));
        }
        

		Point Round() const
		{
			return { std::round(x) , std::round(y) };
		}


        Point Sqrt() const
        {
            return{ static_cast<POINT_TYPE>(std::sqrt(x)), static_cast<POINT_TYPE>(std::sqrt(y)) };
        }

        POINT_TYPE DistanceSquared(const Point& rhs) const
        {
            return (x - rhs.x) * (x - rhs.x) + (y - rhs.y) * (y - rhs.y);
        }

        double Distance(const Point& rhs) const
        {
            return sqrt(DistanceSquared(rhs));
        }

#pragma endregion operations

 #pragma region Binary operators
        bool operator==( const Point& rhs) const
        {
            return x == rhs.x && y == rhs.y;
        }

        bool operator!=(const Point& rhs) const
        {
            return (*this == rhs) == false;
        }

        Point operator-(const Point& rhs) const
        {
            return{ x - rhs.x , y - rhs.y };
        }
        
        Point operator+(const Point& rhs) const
        {
            return Point ( x + rhs.x , y  + rhs.y );
        }
        Point operator*(const Point& rhs) const
        {
            return Point(x * rhs.x, y * rhs.y);
        }

        Point operator/(const Point& point) const
        {
            return Point(x / point.x, y / point.y);
        }
        //Scalars
        template <class SCALAR_TYPE>
        Point operator-(SCALAR_TYPE scalar) const
        {
            return{ x - scalar , y - scalar };
        }
        template <class SCALAR_TYPE>
        Point operator/(SCALAR_TYPE scalar) const
        {
            return Point(x / scalar, y / scalar);
        }
        template <class SCALAR_TYPE>
        Point operator*(SCALAR_TYPE scalar) const
        {
            return Point(x * scalar, y * scalar);
        }


#pragma endregion Binary operators

#pragma region Unary operators

        Point operator-()
        {
            return{ -x,-y };
        }

        template <class T>
        Point& operator*=(T scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }

        
        template <class T>
        Point& operator+=(T scalar)
        {
            x += scalar;
            y += scalar;
            return *this;
        }

        Point& operator*=(Point rhs)
        {
            x *= rhs.x;
            y *= rhs.y;
            return *this;
        }

        Point& operator+=(Point rhs)
        {
            x += rhs.x;
            y += rhs.y;
            return *this;
        }

        Point& operator-=(Point rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            return *this;
        }
#pragma endregion Unary operators
        
        // Casting operator
        template <class BASE_TYPE>
        explicit operator Point<BASE_TYPE>() const
        {
            return Point<BASE_TYPE>(static_cast<BASE_TYPE>(x), static_cast<BASE_TYPE>(y));
        }

        Point Sign() const
        {
            return Point(Math::Sign(x), Math::Sign(y));
        }


#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
        Point(const POINT& rhs)
        {
            x = rhs.x;
            y = rhs.y;
        }

        Point(const SIZE& rhs)
        {
            x = rhs.cx;
            y = rhs.cy;
        }
#endif

    };

    template <class POINT_TYPE>
    Point<POINT_TYPE> operator /(POINT_TYPE val, const Point<POINT_TYPE>& point)
    {
        return Point<POINT_TYPE>(val / point.x, val / point.y);
    }

    template <class POINT_TYPE>
    const Point<POINT_TYPE>  Point<POINT_TYPE>::Zero = Point<POINT_TYPE>(0, 0);

    template <class POINT_TYPE>
    const Point<POINT_TYPE>  Point<POINT_TYPE>::One = Point<POINT_TYPE>(1, 1);

    typedef Point<int32_t> PointI32;
    typedef Point<float> PointF32;
    typedef Point<double> PointF64;
}

/*class TestPoint
{
public:
bool RunTest()
{
PointI32 pointI32;

if (pointI32.IsZero() == false)
return false;

pointI32.x = 3;

pointI32 += 5.0;

if (pointI32.x != 3 || pointI32.x != 8)
return false;

POINTF

}
};*/
