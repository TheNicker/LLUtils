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
#include <algorithm>
#include <string>
#include <cmath>
#include <array>

namespace LLUtils
{
    struct Color
    {
        uint32_t colorValue;
        uint8_t& B = *(reinterpret_cast<uint8_t*>(&colorValue) + 0);
        uint8_t& G = *(reinterpret_cast<uint8_t*>(&colorValue) + 1);
    	uint8_t& R = *(reinterpret_cast<uint8_t*>(&colorValue) + 2);
        uint8_t& A = *(reinterpret_cast<uint8_t*>(&colorValue) + 3);

        Color(const Color& color)
        {
            colorValue = color.colorValue;
        }

        Color(Color&& color)
        {
            colorValue = color.colorValue;
        }
        

        Color& operator=(const Color& color)
        {
            colorValue = color.colorValue;
            return *this;
        }

        Color& operator=(Color&& color)
        {
            colorValue = color.colorValue;
            return *this;
        }

        template <class T>
        const std::array<T,4> GetNormalizedColorValue() const
        {
            static_assert(std::is_floating_point<T>(), "Only floating point support normilization");

            return  {
                      R / static_cast<T>(255.0)
                    , G / static_cast<T>(255.0)
                    , B / static_cast<T>(255.0)
                    , A / static_cast<T>(255.0)
                    };
        }
        Color() = default;
		
		//Floating point constructor
		template <typename ParamType, typename std::enable_if_t<std::is_floating_point_v<ParamType>, int> = 0>
		Color(ParamType r, ParamType g, ParamType b, ParamType a = 1.0) 
		{
            R = static_cast<uint8_t>(std::round(r * 255.0));
            G = static_cast<uint8_t>(std::round(g * 255.0));
            B = static_cast<uint8_t>(std::round(b * 255.0));
            A = static_cast<uint8_t>(std::round(a * 255.0));
		}


		//Intergal constructor
		template <typename ParamType, typename std::enable_if_t<std::is_integral_v<ParamType>, int> = 0>
        Color(ParamType r, ParamType g, ParamType b, ParamType a = 255)
        {
            R = static_cast<uint8_t>(r);
            G = static_cast<uint8_t>(g);
            B = static_cast<uint8_t>(b);
            A = static_cast<uint8_t>(a);
        }
     
        Color(uint32_t color)
        {
            colorValue = color;
        }

		bool operator ==(const Color rhs) const
		{
			return this->colorValue == rhs.colorValue;
		}

		bool operator !=(const Color rhs) const
		{
			return this->colorValue != rhs.colorValue;
		}

        static Color FromHSL(uint16_t hue, double sat, double lum)
        {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;

            if (sat == 0)
            {
                r = g = b = static_cast<uint8_t>(lum * 255);
            }
            else
            {
                double v1, v2;
                const double hueNormalized = static_cast<double>(hue) / 360.0;

                v2 = (lum < 0.5) ? (lum * (1.0 + sat)) : ((lum + sat) - (lum * sat));
                v1 = 2.0 * lum - v2;

                r = static_cast<uint8_t>(255.0 * HueToRGB(v1, v2, hueNormalized + (1.0 / 3.0)));
                g = static_cast<uint8_t>(255.0 * HueToRGB(v1, v2, hueNormalized));
                b = static_cast<uint8_t>(255.0 * HueToRGB(v1, v2, hueNormalized - (1.0 / 3.0)));
            }

            return { r,g,b };
        }

        Color Blend(const Color& source)
        {
            Color blended;
            uint8_t invSourceAlpha = 0xFF - source.A;
            blended.R = (source.A * source.R + invSourceAlpha * R) / 0xFF;
            blended.G = (source.A * source.G + invSourceAlpha * G) / 0xFF;
            blended.B = (source.A * source.B + invSourceAlpha * B) / 0xFF;
            blended.A = (std::max)(A, source.A);
            return blended;
        }
        static Color FromString(const std::string& str)
        {
            using namespace std;;
            Color c = static_cast<uint32_t>(0xFF) << 24;
            char strByteColor[3];
            strByteColor[2] = 0;
            if (str.length() > 0)
            {
                if (str[0] == '#')
                {
                    string hexValues = str.substr(1);
                    if (hexValues.length() > 8)
                        hexValues.erase(hexValues.length());

                    size_t length = hexValues.length();
                    size_t i = 0;
                    while (i < length)
                    {
                        if (length - i >= 2)
                        {
                            strByteColor[0] = hexValues[i];
                            strByteColor[1] = hexValues[i + 1];
                            uint8_t val = static_cast<uint8_t>(std::strtoul(strByteColor, nullptr, 16));
                            *(reinterpret_cast<uint8_t*>(&c.colorValue) + i / 2) = val;
                            i += 2;
                        }
                        else if (length - i == 1)
                        {
                            strByteColor[0] = '0';
                            strByteColor[1] = hexValues[i];
                            uint8_t val = static_cast<uint8_t>(std::strtoul(strByteColor, nullptr, 16));
                            *(reinterpret_cast<uint8_t*>(&c.colorValue) + i / 2) = val;
                            i += 2;
                        }

                    }

                }

            }
            return c;
        }

        private:
            static double HueToRGB(double v1, double v2, double vH) {
                if (vH < 0)
                    vH += 1;

                if (vH > 1)
                    vH -= 1;

                if ((6 * vH) < 1)
                    return (v1 + (v2 - v1) * 6 * vH);

                if ((2 * vH) < 1)
                    return v2;

                if ((3 * vH) < 2)
                    return (v1 + (v2 - v1) * ((2.0f / 3) - vH) * 6);

                return v1;
            }
    };
}