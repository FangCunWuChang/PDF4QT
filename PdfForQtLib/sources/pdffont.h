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

#include <QFont>
#include <QMatrix>
#include <QSharedPointer>

#include <unordered_map>

class QPainterPath;

namespace pdf
{
class PDFDocument;
class PDFRenderErrorReporter;

using CID = unsigned int;
using GID = unsigned int;

using GlyphIndices = std::array<GID, 256>;

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
    inline explicit TextSequenceItem(const QByteArray* characterContentStream, double advance) : characterContentStream(characterContentStream), advance(advance) { }

    inline bool isContentStream() const { return characterContentStream; }
    inline bool isCharacter() const { return glyph; }
    inline bool isAdvance() const { return advance != 0.0; }
    inline bool isNull() const { return !isCharacter() && !isAdvance(); }

    const QPainterPath* glyph = nullptr;
    const QByteArray* characterContentStream = nullptr;
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
    Type0,
    Type1,
    TrueType,
    Type3
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

    /// Returns embedded font data, or nullptr, if font is not embedded
    const QByteArray* getEmbeddedFontData() const;

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
class IRealizedFontImpl;

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
    /// \param reporter Error reporter
    void fillTextSequence(const QByteArray& byteArray, TextSequence& textSequence, PDFRenderErrorReporter* reporter);

    /// Return true, if we have horizontal writing system
    bool isHorizontalWritingSystem() const;

    /// Creates new realized font from the standard font. If font can't be created,
    /// then exception is thrown.
    static PDFRealizedFontPointer createRealizedFont(PDFFontPointer font, PDFReal pixelSize, PDFRenderErrorReporter* reporter);

private:
    /// Constructs new realized font
    explicit PDFRealizedFont(IRealizedFontImpl* impl) : m_impl(impl) { }

    IRealizedFontImpl* m_impl;
};

/// Base  class representing font in the PDF file
class PDFFont
{
public:
    explicit PDFFont(FontDescriptor fontDescriptor);
    virtual ~PDFFont() = default;

    /// Returns the font type
    virtual FontType getFontType() const = 0;

    /// Returns font descriptor
    const FontDescriptor* getFontDescriptor() const { return &m_fontDescriptor; }

    /// Creates font from the object. If font can't be created, exception is thrown.
    /// \param object Font dictionary
    /// \param document Document
    static PDFFontPointer createFont(const PDFObject& object, const PDFDocument* document);

protected:
    FontDescriptor m_fontDescriptor;

private:
    /// Tries to read font descriptor from the object
    /// \param fontDescriptorObject Font descriptor dictionary
    /// \param document Document
    static FontDescriptor readFontDescriptor(const PDFObject& fontDescriptorObject, const PDFDocument* document);
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
                           encoding::EncodingTable encoding,
                           GlyphIndices glyphIndices);
    virtual ~PDFSimpleFont() override = default;

    const encoding::EncodingTable* getEncoding() const { return &m_encoding; }
    const GlyphIndices* getGlyphIndices() const { return &m_glyphIndices; }

    /// Returns the glyph advance (or zero, if glyph advance is invalid)
    PDFInteger getGlyphAdvance(size_t index) const;

protected:
    QByteArray m_name;
    QByteArray m_baseFont;
    PDFInteger m_firstChar;
    PDFInteger m_lastChar;
    std::vector<PDFInteger> m_widths;
    PDFEncoding::Encoding m_encodingType;
    encoding::EncodingTable m_encoding;
    GlyphIndices m_glyphIndices;
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
                          StandardFontType standardFontType,
                          GlyphIndices glyphIndices);
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

class PDFType3Font : public PDFFont
{
public:
    explicit PDFType3Font(FontDescriptor fontDescriptor,
                          int firstCharacterIndex,
                          int lastCharacterIndex,
                          QMatrix fontMatrix,
                          std::map<int, QByteArray>&& characterContentStreams,
                          std::vector<double>&& widths,
                          const PDFObject& resources);

    virtual FontType getFontType() const override;

    /// Returns width of the character. If character doesn't exist, then zero is returned.
    double getWidth(int characterIndex) const;

    /// Return content stream for the character. If character doesn't exist, then nullptr
    /// is returned.
    const QByteArray* getContentStream(int characterIndex) const;

    const QMatrix& getFontMatrix() const { return m_fontMatrix; }
    const PDFObject& getResources() const { return m_resources; }

private:
    int m_firstCharacterIndex;
    int m_lastCharacterIndex;
    QMatrix m_fontMatrix;
    std::map<int, QByteArray> m_characterContentStreams;
    std::vector<double> m_widths;
    PDFObject m_resources;
};

/// Font cache which caches both fonts, and realized fonts. Cache has individual limit
/// for fonts, and realized fonts.
class PDFFontCache
{
public:
    inline explicit PDFFontCache(size_t fontCacheLimit, size_t realizedFontCacheLimit) :
        m_fontCacheLimit(fontCacheLimit),
        m_realizedFontCacheLimit(realizedFontCacheLimit),
        m_document(nullptr)
    {

    }

    /// Sets the document to the cache. Whole cache is cleared.
    /// \param document Document to be setted
    void setDocument(const PDFDocument* document);

    /// Retrieves font from the cache. If font can't be accessed or created,
    /// then exception is thrown.
    /// \param fontObject Font object
    PDFFontPointer getFont(const PDFObject& fontObject) const;

    /// Retrieves realized font from the cache. If realized font can't be accessed or created,
    /// then exception is thrown.
    /// \param font Font, which should be realized
    /// \param size Size of the font (in pixels)
    /// \param reporter Error reporter
    PDFRealizedFontPointer getRealizedFont(const PDFFontPointer& font, PDFReal size, PDFRenderErrorReporter* reporter) const;

private:
    const size_t m_fontCacheLimit;
    const size_t m_realizedFontCacheLimit;
    mutable QMutex m_mutex;
    const PDFDocument* m_document;
    mutable std::map<PDFObjectReference, PDFFontPointer> m_fontCache;
    mutable std::map<std::pair<PDFFontPointer, PDFReal>, PDFRealizedFontPointer> m_realizedFontCache;
};

/// Performs mapping from CID to GID (even identity mapping, if byte array is empty)
class PDFCIDtoGIDMapper
{
public:
    explicit inline PDFCIDtoGIDMapper(QByteArray&& mapping) : m_mapping(qMove(mapping)) { }

    /// Maps CID to GID (glyph identifier)
    GID map(CID cid) const
    {
        if (m_mapping.isEmpty())
        {
            // This means identity mapping
            return cid;
        }
        else if ((2 * cid + 1) < CID(m_mapping.size()))
        {
            return (GID(m_mapping[2 * cid]) << 8) + GID(m_mapping[2 * cid + 1]);
        }

        // This should occur only in case of bad (damaged) PDF file - because in this case,
        // encoding is missing. Return invalid glyph index.
        return 0;
    }

private:
    QByteArray m_mapping;
};

/// Represents a font CMAP (mapping of CIDs)
class PDFFORQTLIBSHARED_EXPORT PDFFontCMap
{
public:
    constexpr explicit PDFFontCMap() = default;

    /// Returns true, if mapping is valid
    bool isValid() const { return !m_entries.empty(); }

    /// Creates mapping from name (name must be one of predefined names)
    static PDFFontCMap createFromName(const QByteArray& name);

    /// Creates mapping from data (data must be a byte array containing the CMap)
    static PDFFontCMap createFromData(const QByteArray& data);

    /// Serializes the CMap to the byte array
    QByteArray serialize() const;

    /// Deserializes the CMap from the byte array
    static PDFFontCMap deserialize(const QByteArray& byteArray);

    /// Converts byte array to array of CIDs
    std::vector<CID> interpret(const QByteArray& byteArray) const;

    /// Converts CID to QChar, use only on ToUnicode CMaps
    QChar getToUnicode(CID cid) const;

private:

    struct Entry
    {
        constexpr explicit inline Entry() = default;
        constexpr explicit inline Entry(unsigned int from, unsigned int to, unsigned int byteCount, CID cid) : from(from), to(to), byteCount(byteCount), cid(cid) { }

        unsigned int from = 0;
        unsigned int to = 0;
        unsigned int byteCount = 0;
        CID cid = 0;

        // Can merge from other CID entry?
        bool canMerge(const Entry& other) const
        {
            const bool sameBytes = byteCount == other.byteCount;
            const bool compatibleRange = (to + 1) == other.from;
            const bool compatibleCID = (cid + to + 1) - from == other.cid;
            return sameBytes && compatibleRange && compatibleCID;
        }

        inline constexpr Entry merge(const Entry& other) const
        {
            return Entry(from, other.to, byteCount, cid);
        }

        inline constexpr bool operator<(const Entry& other) const
        {
            return std::tie(byteCount, from) < std::tie(other.byteCount, other.from);
        }
    };

    using Entries = std::vector<Entry>;

    explicit PDFFontCMap(Entries&& entries, bool vertical);

    /// Optimizes the entries - merges entries, which can be merged. This function
    /// requires, that entries are sorted.
    static Entries optimize(const Entries& entries);

    Entries m_entries;
    unsigned int m_maxKeyLength = 0;
    bool m_vertical = false;
};

/// Composite font (CID-keyed font)
class PDFType0Font : public PDFFont
{
public:
    explicit inline PDFType0Font(FontDescriptor fontDescriptor, PDFFontCMap cmap, PDFFontCMap toUnicode, PDFCIDtoGIDMapper mapper, PDFReal defaultAdvance, std::unordered_map<CID, PDFReal> advances) :
        PDFFont(qMove(fontDescriptor)),
        m_cmap(qMove(cmap)),
        m_toUnicode(qMove(toUnicode)),
        m_mapper(qMove(mapper)),
        m_defaultAdvance(defaultAdvance),
        m_advances(qMove(advances))
    {

    }

    virtual ~PDFType0Font() = default;

    virtual FontType getFontType() const override { return FontType::Type0; }

    const PDFFontCMap* getCMap() const { return &m_cmap; }
    const PDFFontCMap* getToUnicode() const { return &m_toUnicode; }
    const PDFCIDtoGIDMapper* getCIDtoGIDMapper() const { return &m_mapper; }

    /// Returns the glyph advance, if it can be obtained, or zero, if it cannot
    /// be obtained or error occurs.
    /// \param cid CID of the glyph
    PDFReal getGlyphAdvance(CID cid) const;

private:
    PDFFontCMap m_cmap;
    PDFFontCMap m_toUnicode;
    PDFCIDtoGIDMapper m_mapper;
    PDFReal m_defaultAdvance;
    std::unordered_map<CID, PDFReal> m_advances;
};

/// Repository with predefined CMaps
class PDFFORQTLIBSHARED_EXPORT PDFFontCMapRepository
{
public:
    /// Returns instance of CMAP repository
    static PDFFontCMapRepository* getInstance();

    /// Adds CMAP to the repository
    void add(const QByteArray& key, QByteArray value) { m_cmaps[key] = qMove(value); }

    /// Clears the repository
    void clear() { m_cmaps.clear(); }

    /// Saves the repository content to the file
    void saveToFile(const QString& fileName) const;

    /// Loads the repository content from the file
    bool loadFromFile(const QString& fileName);

private:
    explicit PDFFontCMapRepository();

    /// Storage for predefined cmaps
    std::map<QByteArray, QByteArray> m_cmaps;
};

}   // namespace pdf

#endif // PDFFONT_H
