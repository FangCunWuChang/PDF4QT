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

#ifndef PDFFONT_H
#define PDFFONT_H

#include "pdfglobal.h"
#include "pdfencoding.h"
#include "pdfobject.h"

#include <QRawFont>
#include <QSharedPointer>

namespace pdf
{
class PDFDocument;

enum class TextRenderingMode
{
    Fill = 0,
    Stroke = 1,
    FillStroke = 2,
    Invisible = 3,
    FillClip = 4,
    StrokeClip = 5,
    FillStrokeClip = 6,
    Clip = 7
};

/// Item of the text sequence (either single character, or advance)
struct TextSequenceItem
{
    inline explicit TextSequenceItem() = default;
    inline explicit TextSequenceItem(const QPainterPath* glyph, QChar character, PDFReal advance) : glyph(glyph), character(character), advance(advance) { }
    inline explicit TextSequenceItem(PDFReal advance) : character(), advance(advance) { }

    inline bool isCharacter() const { return !character.isNull(); }
    inline bool isAdvance() const { return advance != 0.0; }
    inline bool isNull() const { return !isCharacter() && !isAdvance(); }

    const QPainterPath* glyph = nullptr;
    QChar character;
    PDFReal advance = 0;
};

struct TextSequence
{
    std::vector<TextSequenceItem> items;
};

constexpr bool isTextRenderingModeFilled(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Fill:
        case TextRenderingMode::FillClip:
        case TextRenderingMode::FillStroke:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

constexpr bool isTextRenderingModeStroked(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Stroke:
        case TextRenderingMode::FillStroke:
        case TextRenderingMode::StrokeClip:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

constexpr bool isTextRenderingModeClipped(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Clip:
        case TextRenderingMode::FillClip:
        case TextRenderingMode::StrokeClip:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

enum class FontType
{
    Invalid,
    Type1,
    TrueType
};

/// Standard Type1 fonts
enum class StandardFontType
{
    Invalid,
    TimesRoman,
    TimesRomanBold,
    TimesRomanItalics,
    TimesRomanBoldItalics,
    Helvetica,
    HelveticaBold,
    HelveticaOblique,
    HelveticaBoldOblique,
    Courier,
    CourierBold,
    CourierOblique,
    CourierBoldOblique,
    Symbol,
    ZapfDingbats
};

/// Returns builtin encoding for the standard font
static constexpr PDFEncoding::Encoding getEncodingForStandardFont(StandardFontType standardFont)
{
    switch (standardFont)
    {
        case StandardFontType::Symbol:
            return PDFEncoding::Encoding::Symbol;

        case StandardFontType::ZapfDingbats:
            return PDFEncoding::Encoding::ZapfDingbats;

        default:
            return PDFEncoding::Encoding::Standard;
    }
}

struct FontDescriptor
{
    bool isEmbedded() const { return !fontFile.isEmpty() || !fontFile2.isEmpty() || !fontFile3.isEmpty(); }

    QByteArray fontName;
    QByteArray fontFamily;
    QFont::Stretch fontStretch = QFont::AnyStretch;
    PDFReal fontWeight = 400.0;
    PDFInteger flags;
    QRectF boundingBox;
    PDFReal italicAngle = 0.0;
    PDFReal ascent = 0.0;
    PDFReal descent = 0.0;
    PDFReal leading = 0.0;
    PDFReal capHeight = 0.0;
    PDFReal xHeight = 0.0;
    PDFReal stemV = 0.0;
    PDFReal stemH = 0.0;
    PDFReal avgWidth = 0.0;
    PDFReal maxWidth = 0.0;
    PDFReal missingWidth = 0.0;

    /// Byte array with Type 1 font program (embedded font)
    QByteArray fontFile;

    /// Byte array with TrueType font program (embedded font)
    QByteArray fontFile2;

    /// Byte array with font program, whose format is defined by the Subtype array
    /// in the font dictionary.
    QByteArray fontFile3;

    /// Character set
    QByteArray charset;
};

class PDFFont;

using PDFFontPointer = QSharedPointer<PDFFont>;

class PDFRealizedFont;
class PDFRealizedFontImpl;

using PDFRealizedFontPointer = QSharedPointer<PDFRealizedFont>;

/// Font, which has fixed pixel size. It is programmed as PIMPL, because we need
/// to remove FreeType types from the interface (so we do not include FreeType in the interface).
class PDFRealizedFont
{
public:
    ~PDFRealizedFont();

    /// Fills the text sequence by interpreting byte array according font data and
    /// produces glyphs for the font.
    /// \param byteArray Array of bytes to be interpreted
    /// \param textSequence Text sequence to be filled
    void fillTextSequence(const QByteArray& byteArray, TextSequence& textSequence);

    /// Return true, if we have horizontal writing system
    bool isHorizontalWritingSystem() const;

    /// Creates new realized font from the standard font. If font can't be created,
    /// then exception is thrown.
    static PDFRealizedFontPointer createRealizedFont(const PDFFont* font, PDFReal pixelSize);

private:
    /// Constructs new realized font
    explicit PDFRealizedFont(PDFRealizedFontImpl* impl) : m_impl(impl) { }

    PDFRealizedFontImpl* m_impl;
};

/// Base  class representing font in the PDF file
class PDFFont
{
public:
    explicit PDFFont(FontDescriptor fontDescriptor);
    virtual ~PDFFont() = default;

    /// Returns the font type
    virtual FontType getFontType() const = 0;

    /// Realizes the font (physical materialization of the font using pixel size,
    /// if font can't be realized, then empty QRawFont is returned).
    /// \param fontSize Size of the font
    virtual PDFRealizedFontPointer getRealizedFont(PDFReal fontSize) const = 0;

    /// Returns text using the font encoding
    /// \param byteArray Byte array with encoded string
    virtual QString getTextUsingEncoding(const QByteArray& byteArray) const = 0;

    /// Returns font descriptor
    const FontDescriptor* getFontDescriptor() const { return &m_fontDescriptor; }

    /// Creates font from the object. If font can't be created, exception is thrown.
    /// \param object Font dictionary
    /// \param document Document
    static PDFFontPointer createFont(const PDFObject& object, const PDFDocument* document);

protected:
    FontDescriptor m_fontDescriptor;
};

/// Simple font, see PDF reference 1.7, chapter 5.5. Simple fonts have encoding table,
/// which maps single-byte character to the glyph in the font.
class PDFSimpleFont : public PDFFont
{
public:
    explicit PDFSimpleFont(FontDescriptor fontDescriptor,
                           QByteArray name,
                           QByteArray baseFont,
                           PDFInteger firstChar,
                           PDFInteger lastChar,
                           std::vector<PDFInteger> widths,
                           PDFEncoding::Encoding encodingType,
                           encoding::EncodingTable encoding);
    virtual ~PDFSimpleFont() override = default;

    virtual PDFRealizedFontPointer getRealizedFont(PDFReal fontSize) const override;
    virtual QString getTextUsingEncoding(const QByteArray& byteArray) const override;

protected:
    QByteArray m_name;
    QByteArray m_baseFont;
    PDFInteger m_firstChar;
    PDFInteger m_lastChar;
    std::vector<PDFInteger> m_widths;
    PDFEncoding::Encoding m_encodingType;
    encoding::EncodingTable m_encoding;
};

class PDFType1Font : public PDFSimpleFont
{
public:
    explicit PDFType1Font(FontDescriptor fontDescriptor,
                          QByteArray name,
                          QByteArray baseFont,
                          PDFInteger firstChar,
                          PDFInteger lastChar,
                          std::vector<PDFInteger> widths,
                          PDFEncoding::Encoding encodingType,
                          encoding::EncodingTable encoding,
                          StandardFontType standardFontType);
    virtual ~PDFType1Font() override = default;

    virtual FontType getFontType() const override;

    /// Returns the assigned standard font (or invalid, if font is not standard)
    StandardFontType getStandardFontType() const { return m_standardFontType; }

private:
    StandardFontType m_standardFontType; ///< Type of the standard font (or invalid, if it is not a standard font)
};

class PDFTrueTypeFont : public PDFSimpleFont
{
public:
    using PDFSimpleFont::PDFSimpleFont;

    virtual FontType getFontType() const override;
};

}   // namespace pdf

#endif // PDFFONT_H