/*
Copyright (c) 2021 Lior Lahav

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
    /// <summary>
    /// Color class stores arbitrary width, 4 colors channels in the order R, G ,B ,A where R is the lowest memory address and
    ///  A is the highest.
    /// </summary>
#pragma pack(push,1)
    template <typename channelType, size_t numchannels = 4>
    struct ColorBase
    {
        using color_channel_type = channelType;
        static constexpr size_t num_channels = numchannels;
        template <typename channel_type>
        using GenericColorData = std::array<channel_type, num_channels>;
        using ColorData = GenericColorData<color_channel_type>;

        static constexpr color_channel_type max_channel_value = (std::numeric_limits<color_channel_type>::max)();

        ColorData channels{};

        constexpr color_channel_type& R() { return channels[0]; }
        constexpr color_channel_type& G() { return channels[1]; }
        constexpr color_channel_type& B() { return channels[2]; }
        constexpr color_channel_type& A() { return channels[3]; }

        constexpr const color_channel_type& R() const { return channels[0]; }
        constexpr const color_channel_type& G() const { return channels[1]; }
        constexpr const color_channel_type& B() const { return channels[2]; }
        constexpr const color_channel_type& A() const { return channels[3]; }

        constexpr ColorBase() = default;

        bool operator ==(const ColorBase& rhs) const
        {
            return channels == rhs.channels;
        }

        bool operator !=(const ColorBase& rhs) const
        {
            return channels != rhs.channels;
        }


        constexpr ColorBase(ColorData _channels)
        {
            channels = _channels;
        }

        /// <summary>
        /// Initialize color from a 32 bit number in the form RRGGBBAA.
        /// Reverse byte order on little endian platform.
        /// </summary>
        /// <param name="color"></param>

        ColorBase(uint32_t color)
        {
            // make sure Color class stores color values in 8 bit X 4 channels to match the 32 bit 
            // color parameter
            static_assert(sizeof(color) == sizeof(channels), "default color size is not 32 bit");
            if constexpr (std::endian::native == std::endian::little)
            {
                auto reversedBytes = _byteswap_ulong(color);
                channels = *reinterpret_cast<ColorData*>(&reversedBytes);
            }
            else
            {
                channels = *reinterpret_cast<ColorData*>(&color);
            }
        }


        //Floating point constructor
        template <typename ParamType, typename std::enable_if_t<std::is_floating_point_v<ParamType>, int> = 0>
        constexpr ColorBase(ParamType r, ParamType g, ParamType b, ParamType a = 1.0)
        {

            if constexpr (std::is_integral<color_channel_type>())
            {

                R() = static_cast<color_channel_type>(std::round(r * static_cast<ParamType>(max_channel_value)));
                G() = static_cast<color_channel_type>(std::round(g * static_cast<ParamType>(max_channel_value)));
                B() = static_cast<color_channel_type>(std::round(b * static_cast<ParamType>(max_channel_value)));
                A() = static_cast<color_channel_type>(std::round(a * static_cast<ParamType>(max_channel_value)));
            }
            else if constexpr (std::is_floating_point<color_channel_type>())
            {
                R() = static_cast<color_channel_type>(r);
                G() = static_cast<color_channel_type>(g);
                B() = static_cast<color_channel_type>(b);
                A() = static_cast<color_channel_type>(a);
            }
        }


        //Intergal constructor
        template <typename ParamType, ParamType max_value = std::numeric_limits<ParamType>::max(), typename std::enable_if_t<std::is_integral_v<ParamType>, int> = 0>
        constexpr ColorBase(ParamType r, ParamType g, ParamType b, ParamType a = max_value)
        {
            if constexpr (std::is_integral<color_channel_type>())
            {
                R() = static_cast<color_channel_type>(r);
                G() = static_cast<color_channel_type>(g);
                B() = static_cast<color_channel_type>(b);
                A() = static_cast<color_channel_type>(a);
            }
            else if constexpr (std::is_floating_point<color_channel_type>())
            {
                R() = static_cast<color_channel_type>(r / static_cast<color_channel_type>(max_value));
                G() = static_cast<color_channel_type>(g / static_cast<color_channel_type>(max_value));
                B() = static_cast<color_channel_type>(b / static_cast<color_channel_type>(max_value));
                A() = static_cast<color_channel_type>(a / static_cast<color_channel_type>(max_value));
            }
        }

        static ColorBase FromHSL(uint16_t hue, double sat, double lum)
        {
            color_channel_type r = 0;
            color_channel_type g = 0;
            color_channel_type b = 0;

            if (sat == 0)
            {
                r = g = b = static_cast<color_channel_type>(lum * 255);
            }
            else
            {
                double v1, v2;
                const double hueNormalized = static_cast<double>(hue) / 360.0;

                v2 = (lum < 0.5) ? (lum * (1.0 + sat)) : ((lum + sat) - (lum * sat));
                v1 = 2.0 * lum - v2;

                r = static_cast<color_channel_type>(255.0 * HueToRGB(v1, v2, hueNormalized + (1.0 / 3.0)));
                g = static_cast<color_channel_type>(255.0 * HueToRGB(v1, v2, hueNormalized));
                b = static_cast<color_channel_type>(255.0 * HueToRGB(v1, v2, hueNormalized - (1.0 / 3.0)));
            }

            return { r,g,b };
        }

        ColorBase Blend(const ColorBase& source)
        {

            const auto dst = static_cast<ColorBase<double>>(*this);
            const auto src = static_cast<ColorBase<double>>(source);  

            const double invSourceAlpha = 1.0 - src.A();

            double a = src.A() + invSourceAlpha * dst.A();
            double r = src.R() * src.A() + invSourceAlpha * dst.R() * dst.A();
            double g = src.G() * src.A() + invSourceAlpha * dst.G() * dst.A();
            double b = src.B() * src.A() + invSourceAlpha * dst.B() * dst.A();


            if (a != 0.0)
            {
                r /= a;
                g /= a;
                b /= a;
            }

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
        template <typename new_type>
        explicit operator ColorBase<new_type>() const
        {
            return { R(),G(),B(),A() };
        }


        ColorBase MultiplyAlpha() const 
        {
            static_assert(std::is_floating_point_v<color_channel_type> == true, "Color underlying type needs to be a floating point");
            return { R() * A() , G() * A(), B() * A() , A() };
        }



        ColorBase DivideAlpha() const
        {
            static_assert(std::is_floating_point_v<color_channel_type> == true, "Color underlying type needs to be a floating point");
            if (A() != 0)
                return { R() / A() , G() / A(), B() / A() , A() };
            else 
                return  { R() , G() , B()  , A() };
        }


        /// <summary>
        /// Blend source color into this color for a multiplied alpha color space
        /// </summary>
        /// <typeparam name="enable_if_t"></typeparam>
        /// <param name="source"></param>
        /// <returns></returns>
        //template < std::enable_if_t<std::is_floating_point_v<color_channel_type>, int> = 0>
        ColorBase BlendPreMultiplied(const ColorBase& source) const
        {
            static_assert(std::is_floating_point_v<color_channel_type> == true, "Color underlying type needs to be a floating point");
            const color_channel_type invSourceAlpha = static_cast<color_channel_type>(1.0) - source.A();
            return
            { source.R() + R() * invSourceAlpha
                ,  source.G() + G() * invSourceAlpha
                ,  source.B() + B() * invSourceAlpha
                ,  source.A() + A() * invSourceAlpha

            };
        }


        static ColorBase FromString(const std::string& str)
        {
            static const ColorBase DefaultParseFailureColor = ColorBase(max_channel_value, max_channel_value, max_channel_value, max_channel_value);
            constexpr color_channel_type DefaultAlphaValue = max_channel_value;
            using namespace std;

            auto HexPairToByte = [](const std::array<char, 3>& hexByte) -> color_channel_type
            {
                return static_cast<color_channel_type>(std::strtoul(hexByte.data(), nullptr, 16));
            };
            ColorData colorBytes{};
            std::string trimmed = str;
            StringUtility::trim(trimmed, "\t\n\r ");

            std::string::difference_type hexIndex = -1;
            trimmed = StringUtility::ToLower(trimmed);
            std::string_view view = std::string_view(trimmed.c_str(), trimmed.length());

            if (view.length() > 0 && view.at(0) == '#')
            {
                hexIndex = 1;
            }
            else if (view.length() > 1 && view.substr(0, 2) == "0x")
                hexIndex = 2;


            if (hexIndex != -1 && view.length() > static_cast<size_t>(hexIndex))
            {
                constexpr size_t CharPerComponent = 2;
                view = view.substr(static_cast<size_t>(hexIndex), view.length() - static_cast<size_t>(hexIndex));
                bool lastSingleDigitComponent = view.length() % 2;
                size_t numComponents = view.length() / CharPerComponent + (lastSingleDigitComponent ? 1 : 0);
                if (numComponents > 4)
                {
                    numComponents = 4;
                    lastSingleDigitComponent = false;
                }

                const bool isAlphaChannel = numComponents == 4;
                size_t componentsToProcessInLoop = isAlphaChannel ? numComponents - 1u : numComponents - (lastSingleDigitComponent == true ? 1u : 0u);
                size_t comp = 0;

                //Assign two bytes componenets
                for (; comp < componentsToProcessInLoop; comp++)
                    colorBytes.at(comp) = HexPairToByte({ { view.at(comp * 2), view.at(comp * 2 + 1) ,0 } });

                //Assign last single component, alpha or color.
                if (lastSingleDigitComponent == true)
                    colorBytes.at(isAlphaChannel ? 3 : 2 - comp) = HexPairToByte({ { '0', view.at(comp * 2) ,0 } });

                //Assign default alpha if no alpha provided
                if (isAlphaChannel == false)
                    colorBytes.at(3) = DefaultAlphaValue;
                else if (lastSingleDigitComponent == false) // if two components alpha.
                    colorBytes.at(3) = HexPairToByte({ { view.at(comp * 2), view.at(comp * 2 + 1) ,0 } });


                return { colorBytes };
            }

            //If couldn't parse color in the form of 0xXXXXXX or #XXXXXX, try r,g,b,[a]
            auto splitValues = StringUtility::split(str, ',');
            bool error = false;

            for (size_t i = 0; i < std::min(splitValues.size(), colorBytes.size()); i++)
            {
                try
                {
                    colorBytes.at(i) = static_cast<color_channel_type>(stoi(splitValues.at(i)));
                }
                catch (...)
                {
                    error = true;
                    break;
                }
            }

            if (!error)
            {
                if (splitValues.size() < 4) // no alpha channel supplied, set alpha channel to 255
                    colorBytes.at(3) = 255;

                return { colorBytes };
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
#pragma pack(pop)
    // 8 bit color per channel 
    using Color = ColorBase<uint8_t>;
    
    //16 bit unsigned integer color channel
    using ColorI16 = ColorBase<uint16_t>;
    
    //32 bit floating point color channel
    using ColorF32 = ColorBase<float>;
    
    //64 bit floating point color channel
    using ColorF64 = ColorBase<double>;

}