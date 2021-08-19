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
#include "StringUtility.h"
#include <bit>
#include <cmath>

namespace LLUtils
{
    struct Color
    {
        using ColorValue = uint32_t;
        using Channel = uint8_t;
        ColorValue colorValue;
        static constexpr int reverse = std::endian::native == std::endian::little ? 3 : 0;
        
        Channel& R = *(reinterpret_cast<Channel*>(&colorValue) + std::abs(reverse - 0));
        Channel& G = *(reinterpret_cast<Channel*>(&colorValue) + std::abs(reverse - 1));
        Channel& B = *(reinterpret_cast<Channel*>(&colorValue) + std::abs(reverse - 2));
        Channel& A = *(reinterpret_cast<Channel*>(&colorValue) + std::abs(reverse - 3));


        Color(const Color& color)
        {
            colorValue = color.colorValue;
        }

        Color(Color&& color) noexcept
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
            static_assert(std::is_floating_point<T>(), "Only floating point support normalization");

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
            R = static_cast<Channel>(std::round(r * 255.0));
            G = static_cast<Channel>(std::round(g * 255.0));
            B = static_cast<Channel>(std::round(b * 255.0));
            A = static_cast<Channel>(std::round(a * 255.0));
		}


		//Intergal constructor
		template <typename ParamType, typename std::enable_if_t<std::is_integral_v<ParamType>, int> = 0>
        Color(ParamType r, ParamType g, ParamType b, ParamType a = 255)
        {
            R = static_cast<Channel>(r);
            G = static_cast<Channel>(g);
            B = static_cast<Channel>(b);
            A = static_cast<Channel>(a);
        }
     
        Color(ColorValue color)
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
            Channel r = 0;
            Channel g = 0;
            Channel b = 0;

            if (sat == 0)
            {
                r = g = b = static_cast<Channel>(lum * 255);
            }
            else
            {
                double v1, v2;
                const double hueNormalized = static_cast<double>(hue) / 360.0;

                v2 = (lum < 0.5) ? (lum * (1.0 + sat)) : ((lum + sat) - (lum * sat));
                v1 = 2.0 * lum - v2;

                r = static_cast<Channel>(255.0 * HueToRGB(v1, v2, hueNormalized + (1.0 / 3.0)));
                g = static_cast<Channel>(255.0 * HueToRGB(v1, v2, hueNormalized));
                b = static_cast<Channel>(255.0 * HueToRGB(v1, v2, hueNormalized - (1.0 / 3.0)));
            }

            return { r,g,b };
        }

        Color Blend(const Color& source)
        {
            std::array<double, 4> normalizedDest = GetNormalizedColorValue<double>();
            std::array<double, 4> normalizedSource = source.GetNormalizedColorValue<double>();

            double invSourceAlpha = 1.0 - normalizedSource[3];

            double a = normalizedSource[3] + invSourceAlpha * normalizedDest[3];
            double r = normalizedSource[0] * normalizedSource[3] + invSourceAlpha * normalizedDest[0] * normalizedDest[3];
            double g = normalizedSource[1] * normalizedSource[3] + invSourceAlpha * normalizedDest[1] * normalizedDest[3];
            double b = normalizedSource[2] * normalizedSource[3] + invSourceAlpha * normalizedDest[2] * normalizedDest[3];

            if (a != 0.0)
            {
                r /= a;
                g /= a;
                b /= a;
            };

            return { r,g,b,a };

            /*blended.A = static_cast<Channel>((source.A  + A * invSourceAlpha / 0xFF) );
            blended.R = static_cast<Channel>(((( (int)source.A * source.R) + (A * R) * invSourceAlpha)) / 0xFF);
            blended.G = static_cast<Channel>(((( (int)source.A * source.G) + (A * G) * invSourceAlpha)) / 0xFF);
            blended.B = static_cast<Channel>(((( (int)source.A * source.B) + (A * B) * invSourceAlpha)) / 0xFF);

            if (blended.A != 0)
            {
                blended.R /= blended.A;
                blended.G /= blended.A;
                blended.B /= blended.A;
            }
            
            return blended;*/
        }
        static Color FromString(const std::string& str)
        {
            static const Color DefaultParseFailureColor = Color(0xFF, 0xFF, 0xFF, 0xFF);
            constexpr Channel DefaultAlphaValue = 0xff;
            using namespace std;

            auto HexPairToByte = [](const std::array<char, 3>& hexByte) -> Channel
            {
                return static_cast<Channel>(std::strtoul(hexByte.data(), nullptr, 16));
            };
            std::array<Channel, 4>  colorBytes {};
            std::string trimmed = str;
            StringUtility::trim(trimmed, "\t\n\r ");

            std::string::difference_type hexIndex = -1;
            trimmed = StringUtility::ToLower(trimmed);
            std::string_view view = std::string_view(trimmed.c_str(), trimmed.length());

			if (view.length() > 0 && view.at(0) == '#')
            {
                hexIndex = 1;
            }
            else if (view.length() > 1 && view.substr(0,2) == "0x")
                hexIndex = 2;

            
			if (hexIndex != -1 && view.length() > static_cast<size_t>(hexIndex))
			{
                constexpr Channel CharPerComponent = 2;
                view = view.substr(static_cast<size_t>(hexIndex), view.length() - static_cast<size_t>(hexIndex));
                bool lastSingleDigitComponent = view.length() % 2;
                Channel numComponents =  static_cast<Channel>(view.length() / CharPerComponent + (lastSingleDigitComponent ? 1 : 0));
				if (numComponents > 4)
				{
                    numComponents = 4;
                    lastSingleDigitComponent = false;
				}
				
                const bool isAlphaChannel = numComponents == 4;
				size_t componentsToProcessInLoop = isAlphaChannel ? numComponents - 1u: numComponents - (lastSingleDigitComponent == true ? 1u : 0u);
				size_t comp = 0;

				//Assign two bytes componenets
				for (; comp < componentsToProcessInLoop;comp++)
                    colorBytes.at(comp) = HexPairToByte({ view.at(comp * 2), view.at(comp * 2 + 1) ,0 });

                //Assign last single component, alpha or color.
                if (lastSingleDigitComponent == true)
                    colorBytes.at(isAlphaChannel ? 3 : 2 - comp) = HexPairToByte({'0', view.at(comp * 2) ,0 });

				//Assign default alpha if no alpha provided
				if (isAlphaChannel == false)
                    colorBytes.at(3) = DefaultAlphaValue;
                else if (lastSingleDigitComponent == false) // if two components alpha.
                    colorBytes.at(3) = HexPairToByte({ view.at(comp * 2), view.at(comp * 2 + 1) ,0 });

				

                if constexpr (std::endian::native == std::endian::little)
                {
                    return Color(_byteswap_ulong(*reinterpret_cast<ColorValue*>((colorBytes.data()))));
                }
                else
                {
                    return Color(*reinterpret_cast<ColorValue*>(colorBytes.data()));
                }
                
			}

            return DefaultParseFailureColor;
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
                    return (v1 + (v2 - v1) * ((2.0 / 3.0) - vH) * 6);

                return v1;
            }
    };
}