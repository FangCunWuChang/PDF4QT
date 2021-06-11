//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFOBJECTUTILS_H
#define PDFOBJECTUTILS_H

#include "pdfobject.h"

#include <QtCore>

#include <set>
#include <vector>

namespace pdf
{
class PDFObjectStorage;
class PDFDocument;

/// Utilities for manipulation with objects
class PDFObjectUtils
{
public:
    /// Returns a list of references referenced by \p objects. So, all references, which are present
    /// in objects, appear in the result set, including objects, which are referenced by referenced
    /// objects (so, transitive closure above reference graph is returned).
    /// \param objects Objects
    /// \param storage Storage
    static std::set<PDFObjectReference> getReferences(const std::vector<PDFObject>& objects, const PDFObjectStorage& storage);

    /// Returns a list of references directly referenced from object. References itself are not followed.
    static std::set<PDFObjectReference> getDirectReferences(const PDFObject& object);

    static PDFObject replaceReferences(const PDFObject& object, const std::map<PDFObjectReference, PDFObjectReference>& referenceMapping);

private:
    PDFObjectUtils() = delete;
};

/// Storage, which can mark objects (for example, when we want to mark already visited objects
/// during parsing some complex structure, such as tree)
class PDFMarkedObjectsContext
{
public:
    inline explicit PDFMarkedObjectsContext() = default;

    inline bool isMarked(PDFObjectReference reference) const { return m_markedReferences.count(reference); }
    inline void mark(PDFObjectReference reference) { m_markedReferences.insert(reference); }
    inline void unmark(PDFObjectReference reference) { m_markedReferences.erase(reference); }

private:
    std::set<PDFObjectReference> m_markedReferences;
};

/// Class for marking/unmarking objects functioning as guard. If not already marked, then
/// during existence of this guard, object is marked and then it is unmarked. If it is
/// already marked, then nothing happens and locked flag is set to false.
class PDFMarkedObjectsLock
{
public:
    explicit inline PDFMarkedObjectsLock(PDFMarkedObjectsContext* context, PDFObjectReference reference) :
        m_context(context),
        m_reference(reference),
        m_locked(!reference.isValid() || !context->isMarked(reference))
    {
        if (m_locked && reference.isValid())
        {
            context->mark(reference);
        }
    }

    explicit inline PDFMarkedObjectsLock(PDFMarkedObjectsContext* context, const PDFObject& object) :
        PDFMarkedObjectsLock(context, object.isReference() ? object.getReference() : PDFObjectReference())
    {

    }

    inline ~PDFMarkedObjectsLock()
    {
        if (m_locked && m_reference.isValid())
        {
            m_context->unmark(m_reference);
        }
    }

    explicit operator bool() const { return m_locked; }

private:
    PDFMarkedObjectsContext* m_context;
    PDFObjectReference m_reference;
    bool m_locked;
};

/// Classifies objects according to their type. Some heuristic is used
/// when object type is missing or document is not well-formed.
class Pdf4QtLIBSHARED_EXPORT PDFObjectClassifier
{
public:

    inline PDFObjectClassifier() = default;

    /// Performs object classification on a document. Old classification
    /// is being cleared.
    /// \param document Document
    void classify(const PDFDocument* document);

    enum Type : uint32_t
    {
        None            = 0x00000000,
        Page            = 0x00000001,
        ContentStream   = 0x00000002,
        GraphicState    = 0x00000004,
        ColorSpace      = 0x00000008,
        Pattern         = 0x00000010,
        Shading         = 0x00000020,
        Image           = 0x00000040,
        Form            = 0x00000080,
        Font            = 0x00000100,
        Action          = 0x00000200,
        Annotation      = 0x00000400
    };

    Q_DECLARE_FLAGS(Types, Type)

    /// Returns true, if object with given reference exists
    /// and was classified.
    /// \param reference Reference
    bool hasObject(PDFObjectReference reference) const;

    /// Returns true, if any object with given type is present in a document
    /// \param type Object type
    bool hasType(Type type) const { return m_allTypesUsed.testFlag(type); }

    /// Returns a list of objects with a given type
    /// \param type Type
    std::vector<PDFObjectReference> getObjectsByType(Type type) const;

private:
    struct Classification
    {
        PDFObjectReference reference;
        Types types = None;
    };

    /// Marks object with a given type
    void mark(PDFObjectReference reference, Type type);

    /// Marks objects in dictionary with a given type
    void markDictionary(const PDFDocument* document, PDFObject object, Type type);

    std::vector<Classification> m_classification;
    Types m_allTypesUsed;
};

} // namespace pdf

#endif // PDFOBJECTUTILS_H
