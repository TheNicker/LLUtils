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
#include "Point.h"
#include "Exception.h"
namespace LLUtils
{
    enum Corner
    {
        None,
        TopLeft,
        BottomLeft,
        TopRight,
        BottomRight
    };

    template <class T>
    class Rect
    {
      public:

        using Point_Type = Point<T>;
        Rect() = default;

        Rect(const Point_Type& point1, const Point_Type& point2)
        {
            p0 = point1;
            p1 = point2;

            if (p0.x > p1.x)
                std::swap(p0.x, p1.x);

            if (p0.y > p1.y)
                std::swap(p0.y, p1.y);
        }

        Rect Intersection(const Rect& rect) const
        {
            T x0 = (std::max)(p0.x, rect.p0.x);
            T x1 = (std::min)(p1.x, rect.p1.x);
            T y0 = (std::max)(p0.y, rect.p0.y);
            T y1 = (std::min)(p1.y, rect.p1.y);

            return {{x0, y0}, {x1, y1}};
        }
        Rect Infalte(T x, T y)
        {
            // TODO: solve specific case for integers when inflating using odd numbers.
            Rect infalted = *this;
            infalted.p0.x -= x / 2;
            infalted.p0.y -= y / 2;
            infalted.p1.x += x / 2;
            infalted.p1.y += y / 2;
            return infalted;
        }

        bool IsNonNegative() const
        {
            return p0.x >= 0 && p1.x >= 0 && p0.y >= 0 && p1.y >= 0;
        }
        bool IsInside(const Rect& rect) const
        {
            // clang-format off
            return
                p0.x >= rect.p0.x
                && p1.x <= rect.p1.x
                && p0.y >= rect.p0.y
                && p1.y <= rect.p1.y;
            // clang-format on
        }

        bool IsInside(const Point_Type& point) const
        {
            return point.x >= p0.x && point.x <= p1.x && point.y >= p0.y && point.y <= p1.y;
        }

        Rect Round() const
        {
            return {p0.Round(), p1.Round()};
        }

        Rect& operator+=(Point_Type translation)
        {
            p0 += translation;
            p1 += translation;
            return *this;
        }

        T GetWidth() const
        {
            return p1.x - p0.x;
        }
        T GetHeight() const
        {
            return p1.y - p0.y;
        }
        bool IsEmpty() const
        {
            return GetWidth() == 0 || GetHeight() == 0;
        }

        Point_Type GetCorner(const Corner corner) const
        {
            switch (corner)
            {
                case Corner::TopLeft:
                    return p0;
                case Corner::BottomRight:
                    return p1;
                case Corner::BottomLeft:
                    return Point_Type(p0.x, p1.y);
                case Corner::TopRight:
                    return Point_Type(p1.x, p0.y);
                case Corner::None:
                default:
                    LL_EXCEPTION_UNEXPECTED_VALUE;
            }
        }

        // Casting operator
        template <class BASE_TYPE>
        explicit operator Rect<BASE_TYPE>() const
        {
            using BASE_POINT_TYPE = Point<BASE_TYPE>;
            return Rect<BASE_TYPE>(static_cast<BASE_POINT_TYPE>(p0), static_cast<BASE_POINT_TYPE>(p1));
        }

        Point_Type& LeftTop()
        {
            return p0;
        }

        Point_Type& RightBottom()
        {
            return p1;
        }

      private:

        Point_Type p0;
        Point_Type p1;

      public:

        static const Rect Zero;
    };

    template <class T>
    const Rect<T> Rect<T>::Zero = Rect<T>(Point<T>::Zero, Point<T>::Zero);

    using RectI32 = Rect<int32_t>;
    using RectF32 = Rect<float>;
    using RectF64 = Rect<double>;
}  // namespace LLUtils