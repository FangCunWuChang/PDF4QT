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

#ifndef PDFITEMMODELS_H
#define PDFITEMMODELS_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QAbstractItemModel>

namespace pdf
{
class PDFDocument;
class PDFOptionalContentActivity;

/// Represents tree item in the GUI tree
class PDFTreeItem
{
public:
    inline explicit PDFTreeItem() = default;
    inline explicit PDFTreeItem(PDFTreeItem* parent) : m_parent(parent) { }
    virtual ~PDFTreeItem();

    template<typename T, typename... Arguments>
    inline T* addChild(Arguments&&... arguments)
    {
        T* item = new T(this, std::forward(arguments)...);
        m_children.push_back(item);
        return item;
    }

    void addCreatedChild(PDFTreeItem* item)
    {
        item->m_parent = this;
        m_children.push_back(item);
    }

    int getRow() const { return m_parent->m_children.indexOf(const_cast<PDFTreeItem*>(this)); }
    int getChildCount() const { return m_children.size(); }
    const PDFTreeItem* getChild(int index) const { return m_children.at(index); }
    const PDFTreeItem* getParent() const { return m_parent; }

private:
    PDFTreeItem* m_parent = nullptr;
    QList<PDFTreeItem*> m_children;
};

/// Root of all tree item models
class PDFFORQTLIBSHARED_EXPORT PDFTreeItemModel : public QAbstractItemModel
{
public:
    explicit PDFTreeItemModel(QObject* parent);

    void setDocument(const pdf::PDFDocument* document);

    bool isEmpty() const;

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual bool hasChildren(const QModelIndex& parent) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual void update() = 0;

protected:
    const PDFDocument* m_document;
    std::unique_ptr<PDFTreeItem> m_rootItem;
};

class PDFOptionalContentTreeItem : public PDFTreeItem
{
public:
    inline explicit PDFOptionalContentTreeItem(PDFOptionalContentTreeItem* parent, PDFObjectReference reference, QString text, bool locked) :
        PDFTreeItem(parent),
        m_reference(reference),
        m_text(qMove(text)),
        m_locked(locked)
    {

    }

    PDFObjectReference getReference() const { return m_reference; }
    QString getText() const;
    bool isLocked() const { return m_locked; }

private:
    PDFObjectReference m_reference; ///< Reference to optional content group
    QString m_text; ///< Node display name
    bool m_locked; ///< Node is locked (user can't change it)
};

class PDFFORQTLIBSHARED_EXPORT PDFOptionalContentTreeItemModel : public PDFTreeItemModel
{
public:
    inline explicit PDFOptionalContentTreeItemModel(QObject* parent) :
        PDFTreeItemModel(parent),
        m_activity(nullptr)
    {

    }

    void setActivity(PDFOptionalContentActivity* activity);

    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual void update() override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role) override;

private:
    PDFOptionalContentActivity* m_activity;
};

}   // namespace pdf

#endif // PDFITEMMODELS_H