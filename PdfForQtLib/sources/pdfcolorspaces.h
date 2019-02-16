//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFCOLORSPACES_H
#define PDFCOLORSPACES_H

#include "pdfflatarray.h"

#include <QColor>
#include <QSharedPointer>

namespace pdf
{
class PDFObject;
class PDFStream;
class PDFDocument;
class PDFDictionary;
class PDFAbstractColorSpace;

using PDFColorComponent = float;
using PDFColor = PDFFlatArray<PDFColorComponent, 4>;
using PDFColorSpacePointer = QSharedPointer<PDFAbstractColorSpace>;

static constexpr const int COLOR_SPACE_MAX_LEVEL_OF_RECURSION = 12;

static constexpr const char* COLOR_SPACE_NAME_DEVICE_GRAY = "DeviceGray";
static constexpr const char* COLOR_SPACE_NAME_DEVICE_RGB = "DeviceRGB";
static constexpr const char* COLOR_SPACE_NAME_DEVICE_CMYK = "DeviceCMYK";

static constexpr const char* COLOR_SPACE_NAME_ABBREVIATION_DEVICE_GRAY = "G";
static constexpr const char* COLOR_SPACE_NAME_ABBREVIATION_DEVICE_RGB = "RGB";
static constexpr const char* COLOR_SPACE_NAME_ABBREVIATION_DEVICE_CMYK = "CMYK";

static constexpr const char* COLOR_SPACE_NAME_DEFAULT_GRAY = "DefaultGray";
static constexpr const char* COLOR_SPACE_NAME_DEFAULT_RGB = "DefaultRGB";
static constexpr const char* COLOR_SPACE_NAME_DEFAULT_CMYK = "DefaultCMYK";

static constexpr const char* COLOR_SPACE_NAME_CAL_GRAY = "CalGray";
static constexpr const char* COLOR_SPACE_NAME_CAL_RGB = "CalRGB";
static constexpr const char* COLOR_SPACE_NAME_LAB = "Lab";
static constexpr const char* COLOR_SPACE_NAME_ICCBASED = "ICCBased";

static constexpr const char* CAL_WHITE_POINT = "WhitePoint";
static constexpr const char* CAL_BLACK_POINT = "BlackPoint";
static constexpr const char* CAL_GAMMA = "Gamma";
static constexpr const char* CAL_MATRIX = "Matrix";
static constexpr const char* CAL_RANGE = "Range";

static constexpr const char* ICCBASED_ALTERNATE = "Alternate";
static constexpr const char* ICCBASED_N = "N";
static constexpr const char* ICCBASED_RANGE = "Range";

using PDFColor3 = std::array<PDFColorComponent, 3>;

/// Matrix for color component multiplication (for example, conversion between some color spaces)
template<size_t Rows, size_t Cols>
class PDFColorComponentMatrix
{
public:
    explicit constexpr inline PDFColorComponentMatrix() : m_values() { }

    template<typename... Components>
    explicit constexpr inline PDFColorComponentMatrix(Components... components) : m_values({ static_cast<PDFColorComponent>(components)... }) { }

    std::array<PDFColorComponent, Rows> operator*(const std::array<PDFColorComponent, Cols>& color) const
    {
        std::array<PDFColorComponent, Rows> result = { };

        for (size_t row = 0; row < Rows; ++row)
        {
            for (size_t column = 0; column < Cols; ++column)
            {
                result[row] += m_values[row * Cols + column] * color[column];
            }
        }

        return result;
    }

    inline typename std::array<PDFColorComponent, Rows * Cols>::iterator begin() { return m_values.begin(); }
    inline typename std::array<PDFColorComponent, Rows * Cols>::iterator end() { return m_values.end(); }

private:
    std::array<PDFColorComponent, Rows * Cols> m_values;
};

using PDFColorComponentMatrix_3x3 = PDFColorComponentMatrix<3, 3>;

/// Represents PDF's color space (abstract class). Contains functions for parsing
/// color spaces.
class PDFAbstractColorSpace
{
public:
    explicit PDFAbstractColorSpace() = default;
    virtual ~PDFAbstractColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const = 0;
    virtual size_t getColorComponentCount() const = 0;

    /// Parses the desired color space. If desired color space is not found, then exception is thrown.
    /// If everything is OK, then shared pointer to the new color space is returned.
    /// \param colorSpaceDictionary Dictionary containing color spaces of the page
    /// \param document Document (for loading objects)
    /// \param colorSpace Identification of color space (either name or array), must be a direct object
    static PDFColorSpacePointer createColorSpace(const PDFDictionary* colorSpaceDictionary,
                                                 const PDFDocument* document,
                                                 const PDFObject& colorSpace);

protected:
    /// Clips the color component to range [0, 1]
    static constexpr PDFColorComponent clip01(PDFColorComponent component) { return qBound<PDFColorComponent>(0.0, component, 1.0); }

    /// Clips the color to range [0 1] in all components
    static constexpr PDFColor3 clip01(const PDFColor3& color)
    {
        PDFColor3 result = color;

        for (PDFColorComponent& component : result)
        {
            component = clip01(component);
        }

        return result;
    }

    /// Parses the desired color space. If desired color space is not found, then exception is thrown.
    /// If everything is OK, then shared pointer to the new color space is returned.
    /// \param colorSpaceDictionary Dictionary containing color spaces of the page
    /// \param document Document (for loading objects)
    /// \param colorSpace Identification of color space (either name or array), must be a direct object
    /// \param recursion Recursion guard
    static PDFColorSpacePointer createColorSpaceImpl(const PDFDictionary* colorSpaceDictionary,
                                                     const PDFDocument* document,
                                                     const PDFObject& colorSpace,
                                                     int recursion);

    /// Converts XYZ value to the standard RGB value (linear). No gamma correction is applied.
    /// Default transformation matrix is applied.
    /// \param xyzColor Color in XYZ space
    static PDFColor3 convertXYZtoRGB(const PDFColor3& xyzColor);

    /// Multiplies color by factor
    /// \param color Color to be multiplied
    /// \param factor Multiplication factor
    static constexpr PDFColor3 colorMultiplyByFactor(const PDFColor3& color, PDFColorComponent factor)
    {
        PDFColor3 result = color;
        for (PDFColorComponent& component : result)
        {
            component *= factor;
        }
        return result;
    }

    /// Multiplies color by factors (stored in components of the color)
    /// \param color Color to be multiplied
    /// \param factor Multiplication factors
    static constexpr PDFColor3 colorMultiplyByFactors(const PDFColor3& color, const PDFColor3& factors)
    {
        PDFColor3 result = { };
        for (size_t i = 0; i < color.size(); ++i)
        {
            result[i] = color[i] * factors[i];
        }
        return result;
    }

    /// Powers color by factors (stored in components of the color)
    /// \param color Color to be multiplied
    /// \param factor Power factors
    static constexpr PDFColor3 colorPowerByFactors(const PDFColor3& color, const PDFColor3& factors)
    {
        PDFColor3 result = { };
        for (size_t i = 0; i < color.size(); ++i)
        {
            result[i] = std::powf(color[i], factors[i]);
        }
        return result;
    }

    /// Converts RGB values of range [0.0, 1.0] to standard QColor
    /// \param color Color to be converted
    static inline QColor fromRGB01(const PDFColor3& color)
    {
        PDFColorComponent r = clip01(color[0]);
        PDFColorComponent g = clip01(color[1]);
        PDFColorComponent b = clip01(color[2]);

        QColor result(QColor::Rgb);
        result.setRgbF(r, g, b, 1.0);
        return result;
    }
};

class PDFDeviceGrayColorSpace : public PDFAbstractColorSpace
{
public:
    explicit PDFDeviceGrayColorSpace() = default;
    virtual ~PDFDeviceGrayColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;
};

class PDFDeviceRGBColorSpace : public PDFAbstractColorSpace
{
public:
    explicit PDFDeviceRGBColorSpace() = default;
    virtual ~PDFDeviceRGBColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;
};

class PDFDeviceCMYKColorSpace : public PDFAbstractColorSpace
{
public:
    explicit PDFDeviceCMYKColorSpace() = default;
    virtual ~PDFDeviceCMYKColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;
};

class PDFCalGrayColorSpace : public PDFAbstractColorSpace
{
public:
    explicit inline PDFCalGrayColorSpace(PDFColor3 whitePoint, PDFColor3 blackPoint, PDFColorComponent gamma);
    virtual ~PDFCalGrayColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;

    /// Creates CalGray color space from provided values.
    /// \param document Document
    /// \param dictionary Dictionary
    static PDFColorSpacePointer createCalGrayColorSpace(const PDFDocument* document, const PDFDictionary* dictionary);

private:
    PDFColor3 m_whitePoint;
    PDFColor3 m_blackPoint;
    PDFColorComponent m_gamma;

    /// What are these coefficients? We want to map white point from XYZ space to white point
    /// of RGB space. These coefficients are reciprocal values to the point converted from XYZ white
    /// point. So, if we call getColor(m_whitePoint), then we should get vector (1.0, 1.0, 1.0)
    /// after multiplication by these coefficients.
    PDFColor3 m_correctionCoefficients;
};

class PDFCalRGBColorSpace : public PDFAbstractColorSpace
{
public:
    explicit inline PDFCalRGBColorSpace(PDFColor3 whitePoint, PDFColor3 blackPoint, PDFColor3 gamma, PDFColorComponentMatrix_3x3 matrix);
    virtual ~PDFCalRGBColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;

    /// Creates CalRGB color space from provided values.
    /// \param document Document
    /// \param dictionary Dictionary
    static PDFColorSpacePointer createCalRGBColorSpace(const PDFDocument* document, const PDFDictionary* dictionary);

private:
    PDFColor3 m_whitePoint;
    PDFColor3 m_blackPoint;
    PDFColor3 m_gamma;
    PDFColorComponentMatrix_3x3 m_matrix;

    /// What are these coefficients? We want to map white point from XYZ space to white point
    /// of RGB space. These coefficients are reciprocal values to the point converted from XYZ white
    /// point. So, if we call getColor(m_whitePoint), then we should get vector (1.0, 1.0, 1.0)
    /// after multiplication by these coefficients.
    PDFColor3 m_correctionCoefficients;
};

class PDFLabColorSpace : public PDFAbstractColorSpace
{
public:
    explicit inline PDFLabColorSpace(PDFColor3 whitePoint, PDFColor3 blackPoint, PDFColorComponent aMin, PDFColorComponent aMax, PDFColorComponent bMin, PDFColorComponent bMax);
    virtual ~PDFLabColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;

    /// Creates Lab color space from provided values.
    /// \param document Document
    /// \param dictionary Dictionary
    static PDFColorSpacePointer createLabColorSpace(const PDFDocument* document, const PDFDictionary* dictionary);

private:
    PDFColor3 m_whitePoint;
    PDFColor3 m_blackPoint;
    PDFColorComponent m_aMin;
    PDFColorComponent m_aMax;
    PDFColorComponent m_bMin;
    PDFColorComponent m_bMax;

    /// What are these coefficients? We want to map white point from XYZ space to white point
    /// of RGB space. These coefficients are reciprocal values to the point converted from XYZ white
    /// point. So, if we call getColor(m_whitePoint), then we should get vector (1.0, 1.0, 1.0)
    /// after multiplication by these coefficients.
    PDFColor3 m_correctionCoefficients;
};

class PDFICCBasedColorSpace : public PDFAbstractColorSpace
{
private:
    static constexpr const size_t MAX_COLOR_COMPONENTS = 4;
    using Ranges = std::array<PDFColorComponent, MAX_COLOR_COMPONENTS * 2>;

public:
    explicit inline PDFICCBasedColorSpace(PDFColorSpacePointer alternateColorSpace, Ranges range);
    virtual ~PDFICCBasedColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;

    /// Creates ICC based color space from provided values.
    /// \param colorSpaceDictionary Color space dictionary
    /// \param document Document
    /// \param stream Stream with ICC profile
    /// \param recursion Recursion guard
    static PDFColorSpacePointer createICCBasedColorSpace(const PDFDictionary* colorSpaceDictionary, const PDFDocument* document, const PDFStream* stream, int recursion);

private:
    PDFColorSpacePointer m_alternateColorSpace;
    Ranges m_range;
};

}   // namespace pdf

#endif // PDFCOLORSPACES_H
