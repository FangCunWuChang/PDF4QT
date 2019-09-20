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

#include "pdfpattern.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfutils.h"
#include "pdfcolorspaces.h"

#include <QPainter>

#include <execution>

namespace pdf
{

PatternType PDFShadingPattern::getType() const
{
    return PatternType::Shading;
}

QMatrix PDFShadingPattern::getPatternSpaceToDeviceSpaceMatrix(const PDFMeshQualitySettings& settings) const
{
    return m_matrix * settings.userSpaceToDeviceSpaceMatrix;
}

ShadingType PDFAxialShading::getShadingType() const
{
    return ShadingType::Axial;
}

PDFPatternPtr PDFPattern::createPattern(const PDFDictionary* colorSpaceDictionary, const PDFDocument* document, const PDFObject& object)
{
    const PDFObject& dereferencedObject = document->getObject(object);
    if (dereferencedObject.isDictionary())
    {
        PDFPatternPtr result;

        const PDFDictionary* patternDictionary = dereferencedObject.getDictionary();
        PDFDocumentDataLoaderDecorator loader(document);

        const PatternType patternType = static_cast<PatternType>(loader.readIntegerFromDictionary(patternDictionary, "PatternType", static_cast<PDFInteger>(PatternType::Invalid)));
        switch (patternType)
        {
            case PatternType::Tiling:
            {
                // TODO: Implement tiling pattern
                throw PDFParserException(PDFTranslationContext::tr("Tiling pattern not implemented."));
                break;
            }

            case PatternType::Shading:
            {
                PDFObject patternGraphicState = document->getObject(patternDictionary->get("ExtGState"));
                QMatrix matrix = loader.readMatrixFromDictionary(patternDictionary, "Matrix", QMatrix());
                return createShadingPattern(colorSpaceDictionary, document, patternDictionary->get("Shading"), matrix, patternGraphicState, false);
            }

            default:
                throw PDFParserException(PDFTranslationContext::tr("Invalid pattern."));
        }

        return result;
    }

    throw PDFParserException(PDFTranslationContext::tr("Invalid pattern."));
    return PDFPatternPtr();
}

PDFPatternPtr PDFPattern::createShadingPattern(const PDFDictionary* colorSpaceDictionary,
                                               const PDFDocument* document,
                                               const PDFObject& shadingObject,
                                               const QMatrix& matrix,
                                               const PDFObject& patternGraphicState,
                                               bool ignoreBackgroundColor)
{
    const PDFObject& dereferencedShadingObject = document->getObject(shadingObject);
    if (!dereferencedShadingObject.isDictionary() && !dereferencedShadingObject.isStream())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid shading."));
    }

    PDFDocumentDataLoaderDecorator loader(document);
    const PDFDictionary* shadingDictionary = nullptr;
    const PDFStream* stream = nullptr;

    if (dereferencedShadingObject.isDictionary())
    {
        shadingDictionary = dereferencedShadingObject.getDictionary();
    }
    else if (dereferencedShadingObject.isStream())
    {
        stream = dereferencedShadingObject.getStream();
        shadingDictionary = stream->getDictionary();
    }

    // Parse common data for all shadings
    PDFColorSpacePointer colorSpace = PDFAbstractColorSpace::createColorSpace(colorSpaceDictionary, document, document->getObject(shadingDictionary->get("ColorSpace")));

    if (colorSpace->getPattern())
    {
        throw PDFParserException(PDFTranslationContext::tr("Pattern color space is not valid for shading patterns."));
    }

    QColor backgroundColor;
    if (!ignoreBackgroundColor)
    {
        std::vector<PDFReal> backgroundColorValues = loader.readNumberArrayFromDictionary(shadingDictionary, "Background");
        if (!backgroundColorValues.empty())
        {
            backgroundColor = colorSpace->getCheckedColor(PDFAbstractColorSpace::convertToColor(backgroundColorValues));
        }
    }
    QRectF boundingBox = loader.readRectangle(shadingDictionary->get("BBox"), QRectF());
    bool antialias = loader.readBooleanFromDictionary(shadingDictionary, "AntiAlias", false);
    const PDFObject& extendObject = document->getObject(shadingDictionary->get("Extend"));
    bool extendStart = false;
    bool extendEnd = false;
    if (extendObject.isArray())
    {
        const PDFArray* array = extendObject.getArray();
        if (array->getCount() != 2)
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid shading pattern extends. Expected 2, but %1 provided.").arg(array->getCount()));
        }

        extendStart = loader.readBoolean(array->getItem(0), false);
        extendEnd = loader.readBoolean(array->getItem(1), false);
    }
    std::vector<PDFFunctionPtr> functions;
    const PDFObject& functionsObject = document->getObject(shadingDictionary->get("Function"));
    if (functionsObject.isArray())
    {
        const PDFArray* functionsArray = functionsObject.getArray();
        functions.reserve(functionsArray->getCount());
        for (size_t i = 0, functionCount = functionsArray->getCount(); i < functionCount; ++i)
        {
            functions.push_back(PDFFunction::createFunction(document, functionsArray->getItem(i)));
        }
    }
    else if (!functionsObject.isNull())
    {
        functions.push_back(PDFFunction::createFunction(document, functionsObject));
    }

    const ShadingType shadingType = static_cast<ShadingType>(loader.readIntegerFromDictionary(shadingDictionary, "ShadingType", static_cast<PDFInteger>(ShadingType::Invalid)));
    switch (shadingType)
    {
        case ShadingType::Function:
        {
            PDFFunctionShading* functionShading = new PDFFunctionShading();
            PDFPatternPtr result(functionShading);

            std::vector<PDFReal> functionDomain = loader.readNumberArrayFromDictionary(shadingDictionary, "Domain", { 0.0, 1.0, 0.0, 1.0 });
            if (functionDomain.size() != 4)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid function shading pattern domain. Expected 4 values, but %1 provided.").arg(functionDomain.size()));
            }
            if (functionDomain[1] < functionDomain[0] || functionDomain[3] < functionDomain[2])
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid function shading pattern domain. Invalid domain ranges."));
            }

            QMatrix domainToTargetTransform = loader.readMatrixFromDictionary(shadingDictionary, "Matrix", QMatrix());

            size_t colorComponentCount = colorSpace->getColorComponentCount();
            if (functions.size() > 1 && colorComponentCount != functions.size())
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid axial shading pattern color functions. Expected %1 functions, but %2 provided.").arg(int(colorComponentCount)).arg(int(functions.size())));
            }

            // Load items for function shading
            functionShading->m_antiAlias = antialias;
            functionShading->m_backgroundColor = backgroundColor;
            functionShading->m_colorSpace = colorSpace;
            functionShading->m_boundingBox = boundingBox;
            functionShading->m_domain = QRectF(functionDomain[0], functionDomain[2], functionDomain[1] - functionDomain[0], functionDomain[3] - functionDomain[2]);
            functionShading->m_domainToTargetTransform = domainToTargetTransform;
            functionShading->m_functions = qMove(functions);
            functionShading->m_matrix = matrix;
            functionShading->m_patternGraphicState = patternGraphicState;

            return result;
        }

        case ShadingType::Axial:
        {
            PDFAxialShading* axialShading = new PDFAxialShading();
            PDFPatternPtr result(axialShading);

            std::vector<PDFReal> coordinates = loader.readNumberArrayFromDictionary(shadingDictionary, "Coords");
            if (coordinates.size() != 4)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid axial shading pattern coordinates. Expected 4, but %1 provided.").arg(coordinates.size()));
            }

            std::vector<PDFReal> domain = loader.readNumberArrayFromDictionary(shadingDictionary, "Domain");
            if (domain.empty())
            {
                domain = { 0.0, 1.0 };
            }
            if (domain.size() != 2)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid axial shading pattern domain. Expected 2, but %1 provided.").arg(domain.size()));
            }

            size_t colorComponentCount = colorSpace->getColorComponentCount();
            if (functions.size() > 1 && colorComponentCount != functions.size())
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid axial shading pattern color functions. Expected %1 functions, but %2 provided.").arg(int(colorComponentCount)).arg(int(functions.size())));
            }

            // Load items for axial shading
            axialShading->m_antiAlias = antialias;
            axialShading->m_backgroundColor = backgroundColor;
            axialShading->m_colorSpace = colorSpace;
            axialShading->m_boundingBox = boundingBox;
            axialShading->m_domainStart = domain[0];
            axialShading->m_domainEnd = domain[1];
            axialShading->m_startPoint = QPointF(coordinates[0], coordinates[1]);
            axialShading->m_endPoint = QPointF(coordinates[2], coordinates[3]);
            axialShading->m_extendStart = extendStart;
            axialShading->m_extendEnd = extendEnd;
            axialShading->m_functions = qMove(functions);
            axialShading->m_matrix = matrix;
            axialShading->m_patternGraphicState = patternGraphicState;

            return result;
        }

        case ShadingType::Radial:
        {
            PDFRadialShading* radialShading = new PDFRadialShading();
            PDFPatternPtr result(radialShading);

            std::vector<PDFReal> coordinates = loader.readNumberArrayFromDictionary(shadingDictionary, "Coords");
            if (coordinates.size() != 6)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid radial shading pattern coordinates. Expected 6, but %1 provided.").arg(coordinates.size()));
            }

            std::vector<PDFReal> domain = loader.readNumberArrayFromDictionary(shadingDictionary, "Domain");
            if (domain.empty())
            {
                domain = { 0.0, 1.0 };
            }
            if (domain.size() != 2)
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid radial shading pattern domain. Expected 2, but %1 provided.").arg(domain.size()));
            }

            size_t colorComponentCount = colorSpace->getColorComponentCount();
            if (functions.size() > 1 && colorComponentCount != functions.size())
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid radial shading pattern color functions. Expected %1 functions, but %2 provided.").arg(int(colorComponentCount)).arg(int(functions.size())));
            }

            if (coordinates[2] < 0.0 || coordinates[5] < 0.0)
            {
                throw PDFParserException(PDFTranslationContext::tr("Radial shading cannot have negative circle radius."));
            }

            // Load items for axial shading
            radialShading->m_antiAlias = antialias;
            radialShading->m_backgroundColor = backgroundColor;
            radialShading->m_colorSpace = colorSpace;
            radialShading->m_boundingBox = boundingBox;
            radialShading->m_domainStart = domain[0];
            radialShading->m_domainEnd = domain[1];
            radialShading->m_startPoint = QPointF(coordinates[0], coordinates[1]);
            radialShading->m_r0 = coordinates[2];
            radialShading->m_endPoint = QPointF(coordinates[3], coordinates[4]);
            radialShading->m_r1 = coordinates[5];
            radialShading->m_extendStart = extendStart;
            radialShading->m_extendEnd = extendEnd;
            radialShading->m_functions = qMove(functions);
            radialShading->m_matrix = matrix;
            radialShading->m_patternGraphicState = patternGraphicState;

            return result;
        }

        case ShadingType::FreeFormGouradTriangle:
        case ShadingType::LatticeFormGouradTriangle:
        case ShadingType::TensorProductPatchMesh:
        {
            PDFLatticeFormGouradTriangleShading* latticeFormGouradTriangleShading = nullptr;
            PDFType4567Shading* type4567Shading = nullptr;

            switch (shadingType)
            {
                case ShadingType::FreeFormGouradTriangle:
                {
                    type4567Shading = new PDFFreeFormGouradTriangleShading();
                    break;
                }

                case ShadingType::LatticeFormGouradTriangle:
                {
                    latticeFormGouradTriangleShading = new PDFLatticeFormGouradTriangleShading();
                    type4567Shading = latticeFormGouradTriangleShading;
                    break;
                }

                case ShadingType::TensorProductPatchMesh:
                {
                    type4567Shading = new PDFTensorProductPatchShading;
                    break;
                }

                default:
                {
                    Q_ASSERT(false);
                    break;
                }
            }

            PDFPatternPtr result(type4567Shading);

            PDFInteger bitsPerCoordinate = loader.readIntegerFromDictionary(shadingDictionary, "BitsPerCoordinate", -1);
            if (!contains(bitsPerCoordinate, std::initializer_list<PDFInteger>{ 1, 2, 4, 8, 12, 16, 24, 32 }))
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid bits per coordinate (%1) for shading.").arg(bitsPerCoordinate));
            }
            PDFInteger bitsPerComponent = loader.readIntegerFromDictionary(shadingDictionary, "BitsPerComponent", -1);
            if (!contains(bitsPerComponent, std::initializer_list<PDFInteger>{ 1, 2, 4, 8, 12, 16 }))
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid bits per component (%1) for shading.").arg(bitsPerComponent));
            }

            std::vector<PDFReal> decode = loader.readNumberArrayFromDictionary(shadingDictionary, "Decode");
            if (!functions.empty())
            {
                if (decode.size() != 6)
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid domain for shading. Expected size is 6, actual size is %1.").arg(decode.size()));
                }
            }
            else
            {
                const size_t expectedSize = colorSpace->getColorComponentCount() * 2 + 4;
                if (decode.size() != expectedSize)
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid domain for shading. Expected size is %1, actual size is %2.").arg(expectedSize).arg(decode.size()));
                }
            }

            type4567Shading->m_antiAlias = antialias;
            type4567Shading->m_backgroundColor = backgroundColor;
            type4567Shading->m_colorSpace = colorSpace;
            type4567Shading->m_matrix = matrix;
            type4567Shading->m_patternGraphicState = patternGraphicState;
            type4567Shading->m_bitsPerCoordinate = static_cast<uint8_t>(bitsPerCoordinate);
            type4567Shading->m_bitsPerComponent = static_cast<uint8_t>(bitsPerComponent);
            type4567Shading->m_xmin = decode[0];
            type4567Shading->m_xmax = decode[1];
            type4567Shading->m_ymin = decode[2];
            type4567Shading->m_ymax = decode[3];
            type4567Shading->m_limits = std::vector<PDFReal>(std::next(decode.cbegin(), 4), decode.cend());
            type4567Shading->m_colorComponentCount = !functions.empty() ? 1 : colorSpace->getColorComponentCount();
            type4567Shading->m_functions = qMove(functions);
            type4567Shading->m_data = document->getDecodedStream(stream);

            switch (shadingType)
            {
                case ShadingType::FreeFormGouradTriangle:
                case ShadingType::CoonsPatchMesh:
                case ShadingType::TensorProductPatchMesh:
                {
                    PDFInteger bitsPerFlag = loader.readIntegerFromDictionary(shadingDictionary, "BitsPerFlag", -1);
                    if (!contains(bitsPerFlag, std::initializer_list<PDFInteger>{ 2, 4, 8 }))
                    {
                        throw PDFParserException(PDFTranslationContext::tr("Invalid bits per flag (%1) for shading.").arg(bitsPerFlag));
                    }
                    type4567Shading->m_bitsPerFlag = bitsPerFlag;
                    break;
                }

                case ShadingType::LatticeFormGouradTriangle:
                {
                    latticeFormGouradTriangleShading->m_verticesPerRow = loader.readIntegerFromDictionary(shadingDictionary, "VerticesPerRow", -1);
                    if (latticeFormGouradTriangleShading->m_verticesPerRow < 2)
                    {
                        throw PDFParserException(PDFTranslationContext::tr("Invalid vertices per row (%1) for lattice-form gourad triangle meshing.").arg(latticeFormGouradTriangleShading->m_verticesPerRow));
                    }
                    break;
                }

                default:
                    break;
            }

            return result;
        }

        default:
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid shading pattern type (%1).").arg(static_cast<PDFInteger>(shadingType)));
        }
    }

    throw PDFParserException(PDFTranslationContext::tr("Invalid shading."));
    return PDFPatternPtr();
}

ShadingType PDFFunctionShading::getShadingType() const
{
    return ShadingType::Function;
}

PDFMesh PDFFunctionShading::createMesh(const PDFMeshQualitySettings& settings) const
{
    PDFMesh mesh;

    QMatrix patternSpaceToDeviceSpaceMatrix = getPatternSpaceToDeviceSpaceMatrix(settings);
    QMatrix domainToDeviceSpaceMatrix = m_domainToTargetTransform * patternSpaceToDeviceSpaceMatrix;
    QLineF topLine(m_domain.topLeft(), m_domain.topRight());
    QLineF leftLine(m_domain.topLeft(), m_domain.bottomLeft());

    Q_ASSERT(domainToDeviceSpaceMatrix.isInvertible());
    QMatrix deviceSpaceToDomainMatrix = domainToDeviceSpaceMatrix.inverted();

    QLineF topLineDS = domainToDeviceSpaceMatrix.map(topLine);
    QLineF leftLineDS = domainToDeviceSpaceMatrix.map(leftLine);

    const size_t colorComponents = m_colorSpace->getColorComponentCount();
    auto resolutions = { settings.preferredMeshResolution,
                         interpolate(0.25, 0.0, 1.0, settings.preferredMeshResolution, settings.minimalMeshResolution),
                         interpolate(0.50, 0.0, 1.0, settings.preferredMeshResolution, settings.minimalMeshResolution),
                         interpolate(0.75, 0.0, 1.0, settings.preferredMeshResolution, settings.minimalMeshResolution),
                         settings.minimalMeshResolution
                       };

    for (PDFReal resolution : resolutions)
    {
        const PDFReal xSteps = qMax(std::floor(topLineDS.length() / resolution), 2.0);
        const PDFReal ySteps = qMax(std::floor(leftLineDS.length() / resolution), 2.0);
        const PDFReal xStep = 1.0 / xSteps;
        const PDFReal yStep = 1.0 / ySteps;

        // Prepare x/y ordinates array for given resolution
        std::vector<PDFReal> xOrdinates;
        std::vector<PDFReal> yOrdinates;
        xOrdinates.reserve(xSteps + 1);
        yOrdinates.reserve(ySteps + 1);

        for (PDFReal x = 0.0; x <= 1.0; x += xStep)
        {
            xOrdinates.push_back(x);
        }
        if (xOrdinates.back() + PDF_EPSILON >= 1.0)
        {
            xOrdinates.pop_back();
        }
        xOrdinates.push_back(1.0);

        for (PDFReal y = 0.0; y <= 1.0; y += yStep)
        {
            yOrdinates.push_back(y);
        }
        if (yOrdinates.back() + PDF_EPSILON >= 1.0)
        {
            yOrdinates.pop_back();
        }
        yOrdinates.push_back(1.0);

        // We have determined x/y ordinates. Now we must create result array with colors,
        // which for each x/y ordinate tells us, what color in the given position is.
        const size_t rowCount = yOrdinates.size();
        const size_t columnCount = xOrdinates.size();
        const size_t nodesCount = rowCount * columnCount;
        const size_t stride = columnCount * colorComponents;

        std::vector<size_t> indices;
        indices.resize(nodesCount, 0);
        std::iota(indices.begin(), indices.end(), 0);

        auto indexToRowColumn = [columnCount](size_t index) -> std::pair<size_t, size_t>
        {
            return std::make_pair(index / columnCount, index % columnCount);
        };

        auto rowColumnToIndex = [columnCount](size_t row, size_t column) -> size_t
        {
            return row * columnCount + column;
        };

        auto rowColumnToFirstColorComponent = [stride, colorComponents](size_t row, size_t column) -> size_t
        {
            return row * stride + column * colorComponents;
        };

        const bool isSingleFunction = m_functions.size() == 1;
        std::vector<PDFReal> sourceColorBuffer;
        sourceColorBuffer.resize(indices.size() * colorComponents, 0.0);

        std::vector<QPointF> gridPoints;
        gridPoints.resize(nodesCount);

        QMutex functionErrorMutex;
        PDFFunction::FunctionResult functionError(true);

        auto setColor = [&](size_t index)
        {
            auto [row, column] = indexToRowColumn(index);
            QPointF nodeDS = topLineDS.pointAt(xOrdinates[column]) + leftLineDS.pointAt(yOrdinates[row]) - topLineDS.p1();
            QPointF node = deviceSpaceToDomainMatrix.map(nodeDS);
            const size_t colorComponentIndex = rowColumnToFirstColorComponent(row, column);
            Q_ASSERT(colorComponentIndex <= sourceColorBuffer.size());

            gridPoints[index] = nodeDS;

            PDFReal* sourceColorBegin = sourceColorBuffer.data() + colorComponentIndex;
            PDFReal* sourceColorEnd = sourceColorBegin + colorComponents;

            std::array<PDFReal, 2> uv = { node.x(), node.y() };

            if (isSingleFunction)
            {
                PDFFunction::FunctionResult result = m_functions.front()->apply(uv.data(), uv.data() + uv.size(), sourceColorBegin, sourceColorEnd);
                if (!result)
                {
                    QMutexLocker lock(&functionErrorMutex);
                    if (!functionError)
                    {
                        functionError = result;
                    }
                }
            }
            else
            {
                for (size_t i = 0, count = colorComponents; i < count; ++i)
                {
                    PDFFunction::FunctionResult result = m_functions[i]->apply(uv.data(), uv.data() + uv.size(), sourceColorBegin + i, sourceColorBegin + i + 1);
                    if (!result)
                    {
                        QMutexLocker lock(&functionErrorMutex);
                        if (!functionError)
                        {
                            functionError = result;
                        }
                    }
                }
            }
        };

        std::for_each(std::execution::parallel_policy(), indices.cbegin(), indices.cend(), setColor);

        if (!functionError)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh generation of shading: %1").arg(functionError.errorMessage));
        }

        // Check the colors, if mesh is bad, then refine it
        std::atomic_bool isMeshOK = true;
        auto validateMesh = [&](size_t index)
        {
            if (!isMeshOK.load(std::memory_order_relaxed))
            {
                return;
            }

            auto [row, column] = indexToRowColumn(index);
            const size_t colorComponentIndex = rowColumnToFirstColorComponent(row, column);

            // Check, if color left doesn't differ too much
            if (column > 0)
            {
                const size_t colorOtherComponentIndex = rowColumnToFirstColorComponent(row, column - 1);
                for (size_t i = 0; i < colorComponents; ++i)
                {
                    if (std::fabs(sourceColorBuffer[colorComponentIndex + i] - sourceColorBuffer[colorOtherComponentIndex + i]) > settings.tolerance)
                    {
                        isMeshOK.store(std::memory_order_relaxed);
                        return;
                    }
                }
            }

            if (row > 0)
            {
                const size_t colorOtherComponentIndex = rowColumnToFirstColorComponent(row - 1, column);
                for (size_t i = 0; i < colorComponents; ++i)
                {
                    if (std::fabs(sourceColorBuffer[colorComponentIndex + i] - sourceColorBuffer[colorOtherComponentIndex + i]) > settings.tolerance)
                    {
                        isMeshOK.store(std::memory_order_relaxed);
                        return;
                    }
                }
            }
        };
        std::for_each(std::execution::parallel_policy(), indices.cbegin(), indices.cend(), validateMesh);
        if (!isMeshOK && resolution != settings.minimalMeshResolution)
        {
            continue;
        }

        // Now, we are ready to generate the mesh
        std::vector<QRgb> colors;
        colors.resize(rowCount * columnCount, QRgb());

        mesh.setVertices(qMove(gridPoints));
        std::vector<PDFMesh::Triangle> triangles;
        triangles.resize((rowCount - 1) * (columnCount - 1) * 2);

        auto generateTriangle = [&](size_t index)
        {
            auto [row, column] = indexToRowColumn(index);
                    if (row == 0 || column == 0)
            {
                return;
            }

            Q_ASSERT(index == rowColumnToIndex(row, column));

            const size_t triangleIndex1 = ((row - 1) * (columnCount - 1) + column - 1) * 2;
            const size_t triangleIndex2 = triangleIndex1 + 1;
            const size_t v1 = rowColumnToIndex(row - 1, column - 1);
            const size_t v2 = rowColumnToIndex(row - 1, column);
            const size_t v3 = index;
            const size_t v4 = rowColumnToIndex(row, column - 1);
            std::vector<PDFReal> colorBuffer;
            colorBuffer.resize(colorComponents, 0.0);

            auto calculateColor = [&](const PDFMesh::Triangle& triangle)
            {
                QPointF centerDS = mesh.getTriangleCenter(triangle);
                QPointF center = deviceSpaceToDomainMatrix.map(centerDS);

                std::array<PDFReal, 2> uv = { center.x(), center.y() };

                if (isSingleFunction)
                {
                    PDFFunction::FunctionResult result = m_functions.front()->apply(uv.data(), uv.data() + uv.size(), colorBuffer.data(), colorBuffer.data() + colorBuffer.size());
                    if (!result)
                    {
                        QMutexLocker lock(&functionErrorMutex);
                        if (!functionError)
                        {
                            functionError = result;
                        }
                    }
                }
                else
                {
                    for (size_t i = 0, count = colorComponents; i < count; ++i)
                    {
                        PDFFunction::FunctionResult result = m_functions[i]->apply(uv.data(), uv.data() + uv.size(), colorBuffer.data() + i, colorBuffer.data() + i + 1);
                        if (!result)
                        {
                            QMutexLocker lock(&functionErrorMutex);
                            if (!functionError)
                            {
                                functionError = result;
                            }
                        }
                    }
                }

                return m_colorSpace->getColor(PDFAbstractColorSpace::convertToColor(colorBuffer));
            };

            PDFMesh::Triangle triangle1;
            triangle1.v1 = static_cast<uint32_t>(v1);
            triangle1.v2 = static_cast<uint32_t>(v2);
            triangle1.v3 = static_cast<uint32_t>(v3);
            triangle1.color = calculateColor(triangle1).rgb();

            PDFMesh::Triangle triangle2;
            triangle2.v1 = static_cast<uint32_t>(v3);
            triangle2.v2 = static_cast<uint32_t>(v4);
            triangle2.v3 = static_cast<uint32_t>(v1);
            triangle2.color = calculateColor(triangle2).rgb();

            triangles[triangleIndex1] = triangle1;
            triangles[triangleIndex2] = triangle2;
        };
        std::for_each(std::execution::parallel_policy(), indices.cbegin(), indices.cend(), generateTriangle);
        mesh.setTriangles(qMove(triangles));

        if (!functionError)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh generation of shading: %1").arg(functionError.errorMessage));
        }

        break;
    }

    if (m_backgroundColor.isValid())
    {
        QPainterPath backgroundPath;
        backgroundPath.addRect(settings.deviceSpaceMeshingArea);

        QPainterPath paintedPath;
        paintedPath.addPolygon(domainToDeviceSpaceMatrix.map(m_domain));

        backgroundPath = backgroundPath.subtracted(paintedPath);
        mesh.setBackgroundPath(backgroundPath);
        mesh.setBackgroundColor(m_backgroundColor);
    }

    // Create bounding path
    if (m_boundingBox.isValid())
    {
        QPainterPath boundingPath;
        boundingPath.addPolygon(patternSpaceToDeviceSpaceMatrix.map(m_boundingBox));
        mesh.setBoundingPath(boundingPath);
    }

    return mesh;
}

PDFMesh PDFAxialShading::createMesh(const PDFMeshQualitySettings& settings) const
{
    PDFMesh mesh;

    QMatrix patternSpaceToDeviceSpaceMatrix = getPatternSpaceToDeviceSpaceMatrix(settings);
    QPointF p1 = patternSpaceToDeviceSpaceMatrix.map(m_startPoint);
    QPointF p2 = patternSpaceToDeviceSpaceMatrix.map(m_endPoint);

    // Strategy: for simplification, we rotate the line clockwise so we will
    // get the shading axis equal to the x-axis. Then we will determine the shading
    // area and create mesh according the settings.
    QLineF line(p1, p2);
    const double angle = line.angleTo(QLineF(0, 0, 1, 0));

    // Matrix p1p2LCS is local coordinate system of line p1-p2. It transforms
    // points on the line to the global coordinate system. So, point (0, 0) will
    // map onto p1 and point (length(p1-p2), 0) will map onto p2.
    QMatrix p1p2LCS;
    p1p2LCS.translate(p1.x(), p1.y());
    p1p2LCS.rotate(angle);
    QMatrix p1p2GCS = p1p2LCS.inverted();

    QPointF p1m = p1p2GCS.map(p1);
    QPointF p2m = p1p2GCS.map(p2);

    Q_ASSERT(isZero(p1m.y()));
    Q_ASSERT(isZero(p2m.y()));
    Q_ASSERT(p1m.x() <= p2m.x());

    QPainterPath meshingArea;
    meshingArea.addPolygon(p1p2GCS.map(settings.deviceSpaceMeshingArea));
    meshingArea.addRect(p1m.x(), p1m.y() - settings.preferredMeshResolution * 0.5, p2m.x() - p1m.x(), settings.preferredMeshResolution);
    QRectF meshingRectangle = meshingArea.boundingRect();

    PDFReal xl = meshingRectangle.left();
    PDFReal xr = meshingRectangle.right();
    PDFReal yt = meshingRectangle.top();
    PDFReal yb = meshingRectangle.bottom();

    // Create coordinate array filled with stops, where we will determine the color
    std::vector<PDFReal> xCoords;
    xCoords.reserve((xr - xl) / settings.minimalMeshResolution + 3);
    xCoords.push_back(xl);
    for (PDFReal x = p1m.x(); x <= p2m.x(); x += settings.minimalMeshResolution)
    {
        if (!qFuzzyCompare(xCoords.back(), x))
        {
            xCoords.push_back(x);
        }
    }

    if (xCoords.back() + PDF_EPSILON < p2m.x())
    {
        xCoords.push_back(p2m.x());
    }

    if (!qFuzzyCompare(xCoords.back(), xr))
    {
        xCoords.push_back(xr);
    }

    const PDFReal tAtStart = m_domainStart;
    const PDFReal tAtEnd = m_domainEnd;
    const PDFReal tMin = qMin(tAtStart, tAtEnd);
    const PDFReal tMax = qMax(tAtStart, tAtEnd);

    const bool isSingleFunction = m_functions.size() == 1;
    std::vector<PDFReal> colorBuffer(m_colorSpace->getColorComponentCount(), 0.0);
    auto getColor = [this, isSingleFunction, &colorBuffer](PDFReal t) -> PDFColor
    {
        if (isSingleFunction)
        {
            PDFFunction::FunctionResult result = m_functions.front()->apply(&t, &t + 1, colorBuffer.data(), colorBuffer.data() + colorBuffer.size());
            if (!result)
            {
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
            }
        }
        else
        {
            for (size_t i = 0, count = colorBuffer.size(); i < count; ++i)
            {
                PDFFunction::FunctionResult result = m_functions[i]->apply(&t, &t + 1, colorBuffer.data() + i, colorBuffer.data() + i + 1);
                if (!result)
                {
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
                }
            }
        }

        return PDFAbstractColorSpace::convertToColor(colorBuffer);
    };

    // Determine color of each coordinate
    std::vector<std::pair<PDFReal, PDFColor>> coloredCoordinates;
    coloredCoordinates.reserve(xCoords.size());

    for (PDFReal x : xCoords)
    {
        if (x < p1m.x() - PDF_EPSILON && !m_extendStart)
        {
            // Move to the next coordinate, this is skipped
            continue;
        }

        if (x > p2m.x() + PDF_EPSILON && !m_extendEnd)
        {
            // We are finished no more triangles will occur
            break;
        }

        // Determine current parameter t
        const PDFReal t = interpolate(x, p1m.x(), p2m.x(), tAtStart, tAtEnd);
        const PDFReal tBounded = qBound(tMin, t, tMax);
        const PDFColor color = getColor(tBounded);
        coloredCoordinates.emplace_back(x, color);
    }

    // Filter coordinates according the meshing criteria
    std::vector<std::pair<PDFReal, PDFColor>> filteredCoordinates;
    filteredCoordinates.reserve(coloredCoordinates.size());

    for (auto it = coloredCoordinates.cbegin(); it != coloredCoordinates.cend(); ++it)
    {
        // We will skip this coordinate, if both of meshing criteria have been met:
        //  1) Color difference is small (lesser than tolerance)
        //  2) Distance from previous and next point is less than preffered meshing resolution OR colors are equal

        if (it != coloredCoordinates.cbegin() && std::next(it) != coloredCoordinates.cend())
        {
            auto itNext = std::next(it);

            const std::pair<PDFReal, PDFColor>& prevItem = filteredCoordinates.back();
            const std::pair<PDFReal, PDFColor>& currentItem = *it;
            const std::pair<PDFReal, PDFColor>& nextItem = *itNext;

            if (currentItem.first != p1m.x() && currentItem.first != p2m.x())
            {
                if (prevItem.second == currentItem.second && currentItem.second == nextItem.second)
                {
                    // Colors are same, skip the test
                    continue;
                }

                if (PDFAbstractColorSpace::isColorEqual(prevItem.second, currentItem.second, settings.tolerance) &&
                        PDFAbstractColorSpace::isColorEqual(currentItem.second, nextItem.second, settings.tolerance) &&
                        PDFAbstractColorSpace::isColorEqual(prevItem.second, nextItem.second, settings.tolerance) &&
                        (nextItem.first - prevItem.first < settings.preferredMeshResolution))
                {
                    continue;
                }
            }
        }

        filteredCoordinates.push_back(*it);
    }

    if (!filteredCoordinates.empty())
    {
        size_t vertexCount = filteredCoordinates.size() * 2;
        size_t triangleCount = filteredCoordinates.size() * 2 - 2;

        if (m_backgroundColor.isValid())
        {
            vertexCount += 8;
            triangleCount += 4;
        }
        mesh.reserve(vertexCount, triangleCount);

        PDFColor previousColor = filteredCoordinates.front().second;
        uint32_t topLeft = mesh.addVertex(QPointF(filteredCoordinates.front().first, yt));
        uint32_t bottomLeft = mesh.addVertex(QPointF(filteredCoordinates.front().first, yb));
        for (auto it = std::next(filteredCoordinates.cbegin()); it != filteredCoordinates.cend(); ++it)
        {
            const std::pair<PDFReal, PDFColor>& item = *it;

            uint32_t topRight = mesh.addVertex(QPointF(item.first, yt));
            uint32_t bottomRight = mesh.addVertex(QPointF(item.first, yb));

            PDFColor mixedColor = PDFAbstractColorSpace::mixColors(previousColor, item.second, 0.5);
            QColor color = m_colorSpace->getColor(mixedColor);
            mesh.addQuad(topLeft, topRight, bottomRight, bottomLeft, color.rgb());

            topLeft = topRight;
            bottomLeft = bottomRight;
            previousColor = item.second;
        }
    }

    // Create background color triangles
    if (m_backgroundColor.isValid() && (!m_extendStart || !m_extendEnd))
    {
        if (!m_extendStart && xl + PDF_EPSILON < p1m.x())
        {
            uint32_t topLeft = mesh.addVertex(QPointF(xl, yt));
            uint32_t topRight = mesh.addVertex(QPointF(p1m.x(), yt));
            uint32_t bottomLeft = mesh.addVertex(QPointF(xl, yb));
            uint32_t bottomRight = mesh.addVertex(QPointF(p1m.x(), yb));
            mesh.addQuad(topLeft, topRight, bottomRight, bottomLeft, m_backgroundColor.rgb());
        }

        if (!m_extendEnd && p2m.x() + PDF_EPSILON < xr)
        {
            uint32_t topRight = mesh.addVertex(QPointF(xr, yt));
            uint32_t topLeft = mesh.addVertex(QPointF(p2m.x(), yt));
            uint32_t bottomRight = mesh.addVertex(QPointF(xr, yb));
            uint32_t bottomLeft = mesh.addVertex(QPointF(p2m.x(), yb));
            mesh.addQuad(topLeft, topRight, bottomRight, bottomLeft, m_backgroundColor.rgb());
        }
    }

    // Transform mesh to the device space coordinates
    mesh.transform(p1p2LCS);

    // Create bounding path
    if (m_boundingBox.isValid())
    {
        QPainterPath boundingPath;
        boundingPath.addPolygon(patternSpaceToDeviceSpaceMatrix.map(m_boundingBox));
        mesh.setBoundingPath(boundingPath);
    }

    return mesh;
}

void PDFMesh::paint(QPainter* painter) const
{
    if (m_triangles.empty())
    {
        return;
    }

    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Set the clipping area, if we have it
    if (!m_boundingPath.isEmpty())
    {
        painter->setClipPath(m_boundingPath, Qt::IntersectClip);
    }

    if (!m_backgroundPath.isEmpty() && m_backgroundColor.isValid())
    {
        painter->setBrush(QBrush(m_backgroundColor, Qt::SolidPattern));
        painter->drawPath(m_backgroundPath);
    }

    QColor color;

    // Draw all triangles
    for (const Triangle& triangle : m_triangles)
    {
        if (color != triangle.color)
        {
            painter->setPen(QColor(triangle.color));
            painter->setBrush(QBrush(triangle.color, Qt::SolidPattern));
            color = triangle.color;
        }

        std::array<QPointF, 3> triangleCorners = { m_vertices[triangle.v1], m_vertices[triangle.v2], m_vertices[triangle.v3] };
        painter->drawConvexPolygon(triangleCorners.data(), static_cast<int>(triangleCorners.size()));
    }

    painter->restore();
}

void PDFMesh::transform(const QMatrix& matrix)
{
    for (QPointF& vertex : m_vertices)
    {
        vertex = matrix.map(vertex);
    }

    m_boundingPath = matrix.map(m_boundingPath);
    m_backgroundPath = matrix.map(m_backgroundPath);
}

QPointF PDFMesh::getTriangleCenter(const PDFMesh::Triangle& triangle) const
{
    return (m_vertices[triangle.v1] + m_vertices[triangle.v2] + m_vertices[triangle.v3]) / 3.0;
}

void PDFMeshQualitySettings::initDefaultResolution()
{
    // We will take 0.5% percent of device space meshing area as minimal resolution (it is ~1.5 mm for
    // A4 page) and default resolution 4x number of that.

    Q_ASSERT(deviceSpaceMeshingArea.isValid());
    PDFReal size = qMax(deviceSpaceMeshingArea.width(), deviceSpaceMeshingArea.height());
    minimalMeshResolution = size * 0.005;
    preferredMeshResolution = minimalMeshResolution * 4;
}

ShadingType PDFRadialShading::getShadingType() const
{
    return ShadingType::Radial;
}

PDFMesh PDFRadialShading::createMesh(const PDFMeshQualitySettings& settings) const
{
    PDFMesh mesh;

    QMatrix patternSpaceToDeviceSpaceMatrix = getPatternSpaceToDeviceSpaceMatrix(settings);
    QPointF p1 = patternSpaceToDeviceSpaceMatrix.map(m_startPoint);
    QPointF p2 = patternSpaceToDeviceSpaceMatrix.map(m_endPoint);

    QPointF r1TestPoint = patternSpaceToDeviceSpaceMatrix.map(QPointF(m_startPoint.x(), m_startPoint.y() + m_r0));
    QPointF r2TestPoint = patternSpaceToDeviceSpaceMatrix.map(QPointF(m_endPoint.x(), m_endPoint.y() + m_r1));

    const PDFReal r1 = QLineF(p1, r1TestPoint).length();
    const PDFReal r2 = QLineF(p2, r2TestPoint).length();

    // Strategy: for simplification, we rotate the line clockwise so we will
    // get the shading axis equal to the x-axis. Then we will determine the shading
    // area and create mesh according the settings.
    QLineF line(p1, p2);
    const double angle = line.angleTo(QLineF(0, 0, 1, 0));

    // Matrix p1p2LCS is local coordinate system of line p1-p2. It transforms
    // points on the line to the global coordinate system. So, point (0, 0) will
    // map onto p1 and point (length(p1-p2), 0) will map onto p2.
    QMatrix p1p2LCS;
    p1p2LCS.translate(p1.x(), p1.y());
    p1p2LCS.rotate(angle);
    QMatrix p1p2GCS = p1p2LCS.inverted();

    QPointF p1m = p1p2GCS.map(p1);
    QPointF p2m = p1p2GCS.map(p2);

    Q_ASSERT(isZero(p1m.y()));
    Q_ASSERT(isZero(p2m.y()));
    Q_ASSERT(p1m.x() <= p2m.x());

    QPainterPath meshingArea;
    meshingArea.addPolygon(p1p2GCS.map(settings.deviceSpaceMeshingArea));
    QRectF meshingRectangle = meshingArea.boundingRect();

    PDFReal xl = p1m.x();
    PDFReal xr = p2m.x();

    if (m_extendStart)
    {
        // Well, we must calculate the "zero" point, i.e. when starting radius become zero.
        // It will happen, when r1 < r2, if r1 >= r2, then radius never become zero. We also
        // bound the start by target draw area. We have line between points:
        //
        //  Line: (x1, r1) to (x2, r2)
        // and we will calculate intersection with x axis. If we found intersection points, which
        // is on the left side, then we

        if (r1 > r2)
        {
            xl = meshingRectangle.left() - 2 * r1;
        }
        else
        {
            QLineF radiusInterpolationLine(p1m.x(), r1, p2m.x(), r2);
            QLineF xAxisLine(p1m.x(), 0, p2m.x(), 0);

            QPointF intersectionPoint;
            if (radiusInterpolationLine.intersect(xAxisLine, &intersectionPoint) != QLineF::NoIntersection)
            {
                xl = qBound(meshingRectangle.left() - r1, intersectionPoint.x(), xl);
            }
            else
            {
                xl = meshingRectangle.left() - 2 * r1;
            }
        }
    }

    if (m_extendEnd)
    {
        // Similar as in previous case, find the "zero" point, i.e. when ending radius become zero.

        if (r1 < r2)
        {
            xr = meshingRectangle.right() + 2 * r2;
        }
        else
        {
            QLineF radiusInterpolationLine(p1m.x(), r1, p2m.x(), r2);
            QLineF xAxisLine(p1m.x(), 0, p2m.x(), 0);

            QPointF intersectionPoint;
            if (radiusInterpolationLine.intersect(xAxisLine, &intersectionPoint) != QLineF::NoIntersection)
            {
                xr = qBound(xr, intersectionPoint.x(), meshingRectangle.right() + r2);
            }
            else
            {
                xr = meshingRectangle.right() + 2 * r2;
            }
        }
    }

    // Create coordinate array filled with stops, where we will determine the color
    std::vector<PDFReal> xCoords;
    xCoords.reserve((xr - xl) / settings.minimalMeshResolution + 3);
    xCoords.push_back(xl);
    for (PDFReal x = p1m.x(); x <= p2m.x(); x += settings.minimalMeshResolution)
    {
        if (!qFuzzyCompare(xCoords.back(), x))
        {
            xCoords.push_back(x);
        }
    }

    if (xCoords.back() + PDF_EPSILON < p2m.x())
    {
        xCoords.push_back(p2m.x());
    }

    if (!qFuzzyCompare(xCoords.back(), xr))
    {
        xCoords.push_back(xr);
    }

    const PDFReal tAtStart = m_domainStart;
    const PDFReal tAtEnd = m_domainEnd;
    const PDFReal tMin = qMin(tAtStart, tAtEnd);
    const PDFReal tMax = qMax(tAtStart, tAtEnd);

    const bool isSingleFunction = m_functions.size() == 1;
    std::vector<PDFReal> colorBuffer(m_colorSpace->getColorComponentCount(), 0.0);
    auto getColor = [this, isSingleFunction, &colorBuffer](PDFReal t) -> PDFColor
    {
        if (isSingleFunction)
        {
            PDFFunction::FunctionResult result = m_functions.front()->apply(&t, &t + 1, colorBuffer.data(), colorBuffer.data() + colorBuffer.size());
            if (!result)
            {
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
            }
        }
        else
        {
            for (size_t i = 0, count = colorBuffer.size(); i < count; ++i)
            {
                PDFFunction::FunctionResult result = m_functions[i]->apply(&t, &t + 1, colorBuffer.data() + i, colorBuffer.data() + i + 1);
                if (!result)
                {
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
                }
            }
        }

        return PDFAbstractColorSpace::convertToColor(colorBuffer);
    };

    // Determine color of each coordinate
    std::vector<std::pair<PDFReal, PDFColor>> coloredCoordinates;
    coloredCoordinates.reserve(xCoords.size());

    for (PDFReal x : xCoords)
    {
        // Determine current parameter t
        const PDFReal t = interpolate(x, p1m.x(), p2m.x(), tAtStart, tAtEnd);
        const PDFReal tBounded = qBound(tMin, t, tMax);
        const PDFColor color = getColor(tBounded);
        coloredCoordinates.emplace_back(x, color);
    }

    // Filter coordinates according the meshing criteria
    std::vector<std::pair<PDFReal, PDFColor>> filteredCoordinates;
    filteredCoordinates.reserve(coloredCoordinates.size());

    for (auto it = coloredCoordinates.cbegin(); it != coloredCoordinates.cend(); ++it)
    {
        // We will skip this coordinate, if both of meshing criteria have been met:
        //  1) Color difference is small (lesser than tolerance)
        //  2) Distance from previous and next point is less than preffered meshing resolution OR colors are equal

        if (it != coloredCoordinates.cbegin() && std::next(it) != coloredCoordinates.cend())
        {
            auto itNext = std::next(it);

            const std::pair<PDFReal, PDFColor>& prevItem = filteredCoordinates.back();
            const std::pair<PDFReal, PDFColor>& currentItem = *it;
            const std::pair<PDFReal, PDFColor>& nextItem = *itNext;

            if (currentItem.first != p1m.x() && currentItem.first != p2m.x())
            {
                if (prevItem.second == currentItem.second && currentItem.second == nextItem.second)
                {
                    // Colors are same, skip the test
                    continue;
                }

                if (PDFAbstractColorSpace::isColorEqual(prevItem.second, currentItem.second, settings.tolerance) &&
                        PDFAbstractColorSpace::isColorEqual(currentItem.second, nextItem.second, settings.tolerance) &&
                        PDFAbstractColorSpace::isColorEqual(prevItem.second, nextItem.second, settings.tolerance) &&
                        (nextItem.first - prevItem.first < settings.preferredMeshResolution))
                {
                    continue;
                }
            }
        }

        filteredCoordinates.push_back(*it);
    }

    if (!filteredCoordinates.empty())
    {
        constexpr const int SLICES = 120;

        size_t vertexCount = filteredCoordinates.size() * SLICES * 4;
        size_t triangleCount = filteredCoordinates.size() * SLICES * 2;

        if (m_backgroundColor.isValid())
        {
            vertexCount += 4;
            triangleCount += 2;
        }
        mesh.reserve(vertexCount, triangleCount);

        // Create background color triangles
        if (m_backgroundColor.isValid())
        {
            uint32_t topLeft = mesh.addVertex(meshingRectangle.topLeft());
            uint32_t topRight = mesh.addVertex(meshingRectangle.topRight());
            uint32_t bottomLeft = mesh.addVertex(meshingRectangle.bottomRight());
            uint32_t bottomRight = mesh.addVertex(meshingRectangle.bottomLeft());
            mesh.addQuad(topLeft, topRight, bottomRight, bottomLeft, m_backgroundColor.rgb());
        }

        // Create radial shading triangles
        QLineF rLine(QPointF(p1m.x(), r1), QPointF(p2m.x(), r2));
        const PDFReal rlength = rLine.length();

        for (auto it = std::next(filteredCoordinates.cbegin()); it != filteredCoordinates.cend(); ++it)
        {
            const std::pair<PDFReal, PDFColor>& leftItem = *std::prev(it);
            const std::pair<PDFReal, PDFColor>& rightItem = *it;

            const PDFReal x0 = leftItem.first;
            const PDFReal x1 = rightItem.first;
            const PDFColor mixedColor = PDFAbstractColorSpace::mixColors(leftItem.second, rightItem.second, 0.5);
            const PDFReal angleStep = 2 * M_PI / SLICES;
            const PDFReal r0 = rLine.pointAt((x0 - p1m.x()) / rlength).y();
            const PDFReal r1 = rLine.pointAt((x1 - p1m.x()) / rlength).y();

            PDFReal angle0 = 0;
            for (int i = 0; i < SLICES; ++i)
            {
                const PDFReal angle1 = angle0 + angleStep;
                const PDFReal cos0 = std::cos(angle0);
                const PDFReal sin0 = std::sin(angle0);
                const PDFReal cos1 = std::cos(angle1);
                const PDFReal sin1 = std::sin(angle1);

                QPointF p1(x0 + cos0 * r0, sin0 * r0);
                QPointF p2(x1 + cos0 * r1, sin0 * r1);
                QPointF p3(x1 + cos1 * r1, sin1 * r1);
                QPointF p4(x0 + cos1 * r0, sin1 * r0);

                uint32_t v1 = mesh.addVertex(p1);
                uint32_t v2 = mesh.addVertex(p2);
                uint32_t v3 = mesh.addVertex(p3);
                uint32_t v4 = mesh.addVertex(p4);

                QColor color = m_colorSpace->getColor(mixedColor);
                mesh.addQuad(v1, v2, v3, v4, color.rgb());

                angle0 = angle1;
            }
        }
    }

    // Transform mesh to the device space coordinates
    mesh.transform(p1p2LCS);

    // Create bounding path
    if (m_boundingBox.isValid())
    {
        QPainterPath boundingPath;
        boundingPath.addPolygon(patternSpaceToDeviceSpaceMatrix.map(m_boundingBox));
        mesh.setBoundingPath(boundingPath);
    }

    return mesh;
}

ShadingType PDFFreeFormGouradTriangleShading::getShadingType() const
{
    return ShadingType::FreeFormGouradTriangle;
}

PDFMesh PDFFreeFormGouradTriangleShading::createMesh(const PDFMeshQualitySettings& settings) const
{
    PDFMesh mesh;

    QMatrix patternSpaceToDeviceSpaceMatrix = getPatternSpaceToDeviceSpaceMatrix(settings);
    size_t bitsPerVertex = m_bitsPerFlag + 2 * m_bitsPerCoordinate + m_colorComponentCount * m_bitsPerComponent;
    size_t remainder = (8 - (bitsPerVertex % 8)) % 8;
    bitsPerVertex += remainder;
    size_t bytesPerVertex = bitsPerVertex / 8;
    size_t vertexCount = m_data.size() / bytesPerVertex;

    if (vertexCount < 3)
    {
        // No mesh produced
        return mesh;
    }

    // We have 3 vertices for start triangle, then for each new vertex, we get
    // a new triangle, or, based on flags, no triangle (if new triangle is processed)
    size_t triangleCount = vertexCount - 2;
    mesh.reserve(0, triangleCount);

    struct VertexData
    {
        uint32_t index = 0;
        uint8_t flags = 0;
        QPointF position;
        PDFColor color;
    };

    const PDFReal vertexScaleRatio = 1.0 / double((static_cast<uint64_t>(1) << m_bitsPerCoordinate) - 1);
    const PDFReal xScaleRatio = (m_xmax - m_xmin) * vertexScaleRatio;
    const PDFReal yScaleRatio = (m_ymax - m_ymin) * vertexScaleRatio;
    const PDFReal colorScaleRatio = 1.0 / double((static_cast<uint64_t>(1) << m_bitsPerComponent) - 1);

    std::vector<VertexData> vertices;
    vertices.resize(vertexCount);
    std::vector<size_t> indices(vertexCount, 0);
    std::iota(indices.begin(), indices.end(), static_cast<size_t>(0));
    std::vector<QPointF> meshVertices;
    meshVertices.resize(vertexCount);

    auto readVertex = [this, &vertices, &patternSpaceToDeviceSpaceMatrix, &meshVertices, bytesPerVertex, xScaleRatio, yScaleRatio, colorScaleRatio](size_t index)
    {
        PDFBitReader reader(&m_data, 8);
        reader.seek(index * bytesPerVertex);

        VertexData data;
        data.index = static_cast<uint32_t>(index);
        data.flags = reader.read(m_bitsPerFlag);
        const PDFReal x = m_xmin + (reader.read(m_bitsPerCoordinate)) * xScaleRatio;
        const PDFReal y = m_ymin + (reader.read(m_bitsPerCoordinate)) * yScaleRatio;
        data.position = patternSpaceToDeviceSpaceMatrix.map(QPointF(x, y));
        data.color.resize(m_colorComponentCount);
        meshVertices[index] = data.position;

        for (size_t i = 0; i < m_colorComponentCount; ++i)
        {
            const double cMin = m_limits[2 * i + 0];
            const double cMax = m_limits[2 * i + 1];
            data.color[i] = cMin + (reader.read(m_bitsPerComponent)) * (cMax - cMin) * colorScaleRatio;
        }

        data.color = getColor(data.color);

        vertices[index] = qMove(data);
    };

    std::for_each(std::execution::parallel_policy(), indices.begin(), indices.end(), readVertex);
    mesh.setVertices(qMove(meshVertices));

    vertices.front().flags = 0;

    const VertexData* va = nullptr;
    const VertexData* vb = nullptr;
    const VertexData* vc = nullptr;
    const VertexData* vd = nullptr;

    auto addTriangle = [this, &settings, &mesh, &vertices](const VertexData* va, const VertexData* vb, const VertexData* vc)
    {
        const uint32_t via = va->index;
        const uint32_t vib = vb->index;
        const uint32_t vic = vc->index;

        addSubdividedTriangles(settings, mesh, via, vib, vic, va->color, vb->color, vc->color);
    };

    for (size_t i = 0; i < vertexCount;)
    {
        vd = &vertices[i];

        switch (vd->flags)
        {
            case 0:
            {
                if (i + 2 >= vertexCount)
                {
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid free form gourad triangle data stream - not enough vertices."));
                }
                va = vd;
                vb = &vertices[i + 1];
                vc = &vertices[i + 2];
                i += 3;
                addTriangle(va, vb, vc);
                break;
            }

            case 1:
            {
                // Triangle vb, vc, vd
                va = vb;
                vb = vc;
                vc = vd;
                ++i;
                addTriangle(va, vb, vc);
                break;
            }

            case 2:
            {
                // Triangle va, vc, vd
                vb = vc;
                vc = vd;
                ++i;
                addTriangle(va, vb, vc);
                break;
            }

            default:
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid free form gourad triangle data stream - invalid vertex flag %1.").arg(vd->flags));
                break;
        }
    }

    if (m_backgroundColor.isValid())
    {
        QPainterPath path;
        path.addRect(settings.deviceSpaceMeshingArea);
        mesh.setBackgroundPath(path);
        mesh.setBackgroundColor(m_backgroundColor);
    }

    return mesh;
}

ShadingType PDFLatticeFormGouradTriangleShading::getShadingType() const
{
    return ShadingType::LatticeFormGouradTriangle;
}

PDFMesh PDFLatticeFormGouradTriangleShading::createMesh(const PDFMeshQualitySettings& settings) const
{
    PDFMesh mesh;

    QMatrix patternSpaceToDeviceSpaceMatrix = getPatternSpaceToDeviceSpaceMatrix(settings);
    size_t bitsPerVertex = 2 * m_bitsPerCoordinate + m_colorComponentCount * m_bitsPerComponent;
    size_t remainder = (8 - (bitsPerVertex % 8)) % 8;
    bitsPerVertex += remainder;
    size_t bytesPerVertex = bitsPerVertex / 8;
    size_t vertexCount = m_data.size() / bytesPerVertex;
    size_t columnCount = static_cast<size_t>(m_verticesPerRow);
    size_t rowCount = vertexCount / columnCount;

    if (rowCount < 2)
    {
        // No mesh produced
        return mesh;
    }

    // We have 2 triangles for each quad. We have (columnCount - 1) quads
    // in single line and we have (rowCount - 1) lines.
    size_t triangleCount = (rowCount - 1) * (columnCount - 1) * 2;
    mesh.reserve(0, triangleCount);

    struct VertexData
    {
        uint32_t index = 0;
        QPointF position;
        PDFColor color;
    };

    const PDFReal vertexScaleRatio = 1.0 / double((static_cast<uint64_t>(1) << m_bitsPerCoordinate) - 1);
    const PDFReal xScaleRatio = (m_xmax - m_xmin) * vertexScaleRatio;
    const PDFReal yScaleRatio = (m_ymax - m_ymin) * vertexScaleRatio;
    const PDFReal colorScaleRatio = 1.0 / double((static_cast<uint64_t>(1) << m_bitsPerComponent) - 1);

    std::vector<VertexData> vertices;
    vertices.resize(vertexCount);
    std::vector<size_t> indices(vertexCount, 0);
    std::iota(indices.begin(), indices.end(), static_cast<size_t>(0));
    std::vector<QPointF> meshVertices;
    meshVertices.resize(vertexCount);

    auto readVertex = [this, &vertices, &patternSpaceToDeviceSpaceMatrix, &meshVertices, bytesPerVertex, xScaleRatio, yScaleRatio, colorScaleRatio](size_t index)
    {
        PDFBitReader reader(&m_data, 8);
        reader.seek(index * bytesPerVertex);

        VertexData data;
        data.index = static_cast<uint32_t>(index);
        const PDFReal x = m_xmin + (reader.read(m_bitsPerCoordinate)) * xScaleRatio;
        const PDFReal y = m_ymin + (reader.read(m_bitsPerCoordinate)) * yScaleRatio;
        data.position = patternSpaceToDeviceSpaceMatrix.map(QPointF(x, y));
        data.color.resize(m_colorComponentCount);
        meshVertices[index] = data.position;

        for (size_t i = 0; i < m_colorComponentCount; ++i)
        {
            const double cMin = m_limits[2 * i + 0];
            const double cMax = m_limits[2 * i + 1];
            data.color[i] = cMin + (reader.read(m_bitsPerComponent)) * (cMax - cMin) * colorScaleRatio;
        }

        data.color = getColor(data.color);

        vertices[index] = qMove(data);
    };

    std::for_each(std::execution::parallel_policy(), indices.begin(), indices.end(), readVertex);
    mesh.setVertices(qMove(meshVertices));

    auto getVertexIndex = [columnCount](size_t row, size_t column) -> size_t
    {
        return row * columnCount + column;
    };

    for (size_t row = 1; row < rowCount; ++row)
    {
        for (size_t column = 1; column < columnCount; ++column)
        {
            const size_t vTopLeft = getVertexIndex(row - 1, column - 1);
            const size_t vTopRight = getVertexIndex(row - 1, column);
            const size_t vBottomRight = getVertexIndex(row, column);
            const size_t vBottomLeft = getVertexIndex(row, column - 1);

            const VertexData& vertexTopLeft = vertices[vTopLeft];
            const VertexData& vertexTopRight = vertices[vTopRight];
            const VertexData& vertexBottomRight = vertices[vBottomRight];
            const VertexData& vertexBottomLeft = vertices[vBottomLeft];

            addSubdividedTriangles(settings, mesh, vertexTopLeft.index, vertexTopRight.index, vertexBottomRight.index, vertexTopLeft.color, vertexTopRight.color, vertexBottomRight.color);
            addSubdividedTriangles(settings, mesh, vertexBottomRight.index, vertexBottomLeft.index, vertexTopLeft.index, vertexBottomRight.color, vertexBottomLeft.color, vertexTopLeft.color);
        }
    }

    if (m_backgroundColor.isValid())
    {
        QPainterPath path;
        path.addRect(settings.deviceSpaceMeshingArea);
        mesh.setBackgroundPath(path);
        mesh.setBackgroundColor(m_backgroundColor);
    }

    return mesh;
}

PDFColor PDFType4567Shading::getColor(PDFColor colorOrFunctionParameter) const
{
    if (!m_functions.empty())
    {
        const PDFReal t = colorOrFunctionParameter[0];

        Q_ASSERT(m_colorComponentCount == 1);
        std::vector<PDFReal> colorBuffer(m_colorSpace->getColorComponentCount(), 0.0);
        if (m_functions.size() == 1)
        {
            PDFFunction::FunctionResult result = m_functions.front()->apply(&t, &t + 1, colorBuffer.data(), colorBuffer.data() + colorBuffer.size());
            if (!result)
            {
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
            }
        }
        else
        {
            for (size_t i = 0, count = colorBuffer.size(); i < count; ++i)
            {
                PDFFunction::FunctionResult result = m_functions[i]->apply(&t, &t + 1, colorBuffer.data() + i, colorBuffer.data() + i + 1);
                if (!result)
                {
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Error occured during mesh creation of shading: %1").arg(result.errorMessage));
                }
            }
        }

        return PDFAbstractColorSpace::convertToColor(colorBuffer);
    }

    return colorOrFunctionParameter;
}

void PDFType4567Shading::addSubdividedTriangles(const PDFMeshQualitySettings& settings,
                                                      PDFMesh& mesh, uint32_t v1, uint32_t v2, uint32_t v3,
                                                      PDFColor c1, PDFColor c2, PDFColor c3) const
{
    // First, verify, if we can subdivide the triangle
    QLineF v12(mesh.getVertex(v1), mesh.getVertex(v2));
    QLineF v13(mesh.getVertex(v1), mesh.getVertex(v3));
    QLineF v23(mesh.getVertex(v2), mesh.getVertex(v3));

    const qreal length12 = v12.length();
    const qreal length13 = v13.length();
    const qreal length23 = v23.length();
    const qreal maxLength = qMax(length12, qMax(length13, length23));

    const bool isColorEqual = PDFAbstractColorSpace::isColorEqual(c1, c2, settings.tolerance) &&
                              PDFAbstractColorSpace::isColorEqual(c1, c3, settings.tolerance) &&
                              PDFAbstractColorSpace::isColorEqual(c2, c3, settings.tolerance);
    const bool canSubdivide = maxLength >= settings.minimalMeshResolution * 2.0; // If we subdivide, we will have length at least settings.minimalMeshResolution


    if (!isColorEqual && canSubdivide)
    {
        if (length23 == maxLength)
        {
            // We split line (v2, v3), create two triangles, (v1, v2, vx) and (v1, v3, vx), where
            // vx is centerpoint of line (v2, v3). We also interpolate colors.
            QPointF x = v23.center();
            PDFColor cx = PDFAbstractColorSpace::mixColors(c2, c3, 0.5);
            const uint32_t vx = mesh.addVertex(x);

            addSubdividedTriangles(settings, mesh, v1, v2, vx, c1, c2, cx);
            addSubdividedTriangles(settings, mesh, v1, v3, vx, c1, c3, cx);
        }
        else if (length13 == maxLength)
        {
            // We split line (v1, v3), create two triangles, (v1, v2, vx) and (v2, v3, vx), where
            // vx is centerpoint of line (v1, v3). We also interpolate colors.
            QPointF x = v13.center();
            PDFColor cx = PDFAbstractColorSpace::mixColors(c1, c3, 0.5);
            const uint32_t vx = mesh.addVertex(x);

            addSubdividedTriangles(settings, mesh, v1, v2, vx, c1, c2, cx);
            addSubdividedTriangles(settings, mesh, v2, v3, vx, c2, c3, cx);
        }
        else
        {
            Q_ASSERT(length12 == maxLength);

            // We split line (v1, v2), create two triangles, (v1, v3, vx) and (v2, v3, vx), where
            // vx is centerpoint of line (v1, v2). We also interpolate colors.
            QPointF x = v12.center();
            PDFColor cx = PDFAbstractColorSpace::mixColors(c1, c2, 0.5);
            const uint32_t vx = mesh.addVertex(x);

            addSubdividedTriangles(settings, mesh, v1, v3, vx, c1, c3, cx);
            addSubdividedTriangles(settings, mesh, v2, v3, vx, c2, c3, cx);
        }
    }
    else
    {
        const size_t colorComponents = c1.size();

        // Calculate color - interpolate 3 vertex colors
        PDFColor color;
        color.resize(colorComponents);

        constexpr const PDFReal coefficient = 1.0 / 3.0;
        for (size_t i = 0; i < colorComponents; ++i)
        {
            color[i] = (c1[i] + c2[i] + c3[i]) * coefficient;
        }
        Q_ASSERT(colorComponents == m_colorSpace->getColorComponentCount());
        QColor transformedColor = m_colorSpace->getColor(color);

        PDFMesh::Triangle triangle;
        triangle.v1 = v1;
        triangle.v2 = v2;
        triangle.v3 = v3;
        triangle.color = transformedColor.rgb();
        mesh.addTriangle(triangle);
    }
}

QPointF PDFTensorPatch::getValue(PDFReal u, PDFReal v) const
{
    return getValue(u, v, 0, 0);
}

QPointF PDFTensorPatch::getValue(PDFReal u, PDFReal v, int derivativeOrderU, int derivativeOrderV) const
{
    QPointF result(0.0, 0.0);

    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            result += m_P[i][j] * B(i, u, derivativeOrderU) * B(j, v, derivativeOrderV);
        }
    }

    return result;
}

PDFReal PDFTensorPatch::getCurvature_u(PDFReal u, PDFReal v) const
{
    QPointF dSdu = getDerivative_u(u, v);
    QPointF dSduu = getDerivative_uu(u, v);

    PDFReal squaredLengthOfdSdu = QPointF::dotProduct(dSdu, dSdu);

    if (qFuzzyIsNull(squaredLengthOfdSdu))
    {
        // We assume, that curvature, due to zero length of the tangent vector, is also zero
        return 0.0;
    }

    // Well known formula, how to compute curvature of curve f(x):
    //  K = ( df/dx * df/dyy - df/dxx * df/dy ) / ( (df/dx)^2 + (df/dy)^2 ) ^ (3/2)
    PDFReal curvature = std::fabs(dSdu.x() * dSduu.y() - dSdu.y() * dSduu.x()) / std::pow(squaredLengthOfdSdu, 1.5);
    return curvature;
}

PDFReal PDFTensorPatch::getCurvature_v(PDFReal u, PDFReal v) const
{
    QPointF dSdv = getDerivative_v(u, v);
    QPointF dSdvv = getDerivative_vv(u, v);

    PDFReal squaredLengthOfdSdv = QPointF::dotProduct(dSdv, dSdv);

    if (qFuzzyIsNull(squaredLengthOfdSdv))
    {
        // We assume, that curvature, due to zero length of the tangent vector, is also zero
        return 0.0;
    }

    // Well known formula, how to compute curvature of curve f(x):
    //  K = ( df/dx * df/dyy - df/dxx * df/dy ) / ( (df/dx)^2 + (df/dy)^2 ) ^ (3/2)
    PDFReal curvature = std::fabs(dSdv.x() * dSdvv.y() - dSdv.y() * dSdvv.x()) / std::pow(squaredLengthOfdSdv, 1.5);
    return curvature;
}

constexpr PDFReal PDFTensorPatch::B(int index, PDFReal t, int derivativeOrder)
{
    switch (index)
    {
        case 0:
            return B0(t, derivativeOrder);

        case 1:
            return B1(t, derivativeOrder);

        case 2:
            return B2(t, derivativeOrder);

        case 3:
            return B3(t, derivativeOrder);

        default:
            break;
    }

    return std::numeric_limits<PDFReal>::signaling_NaN();
}

constexpr PDFReal PDFTensorPatch::B0(PDFReal t, int derivative)
{
    switch (derivative)
    {
        case 0:
            return pow3(1.0 - t);

        case 1:
            return -3.0 * pow2(1.0 - t);

        case 2:
            return 6.0 * (1.0 - t);

        case 3:
            return -6.0;

        default:
            break;
    }

    return std::numeric_limits<PDFReal>::signaling_NaN();
}

constexpr PDFReal PDFTensorPatch::B1(PDFReal t, int derivative)
{
    switch (derivative)
    {
        case 0:
            return 3.0 * t * pow2(1.0 - t);

        case 1:
            return 9.0 * pow2(t) - 12.0 * t + 3.0;

        case 2:
            return 18.0 * t - 12.0;

        case 3:
            return 18.0;
    }

    return std::numeric_limits<PDFReal>::signaling_NaN();
}

constexpr PDFReal PDFTensorPatch::B2(PDFReal t, int derivative)
{
    switch (derivative)
    {
        case 0:
            return 3.0 * pow2(t) * (1.0 - t);

        case 1:
            return -9.0 * pow2(t) + 6.0 * t;

        case 2:
            return -18.0 * t + 6.0;

        case 3:
            return -18.0;
    }

    return std::numeric_limits<PDFReal>::signaling_NaN();
}

constexpr PDFReal PDFTensorPatch::B3(PDFReal t, int derivative)
{
    switch (derivative)
    {
        case 0:
            return pow3(t);

        case 1:
            return 3.0 * pow2(t);

        case 2:
            return 6.0 * t;

        case 3:
            return 6.0;
    }

    return std::numeric_limits<PDFReal>::signaling_NaN();
}

ShadingType PDFTensorProductPatchShading::getShadingType() const
{
    return ShadingType::TensorProductPatchMesh;
}

PDFMesh PDFTensorProductPatchShading::createMesh(const PDFMeshQualitySettings& settings) const
{
    PDFMesh mesh;

    QMatrix patternSpaceToDeviceSpaceMatrix = getPatternSpaceToDeviceSpaceMatrix(settings);

    size_t bitsPerPatch = m_bitsPerFlag + 16 * 2 * m_bitsPerCoordinate + 4 * m_colorComponentCount * m_bitsPerComponent;
    size_t remainder = (8 - (bitsPerPatch % 8)) % 8;
    bitsPerPatch += remainder;
    size_t bytesPerPatch = bitsPerPatch / 8;
    size_t patchCount = m_data.size() / bytesPerPatch;

    const PDFReal vertexScaleRatio = 1.0 / double((static_cast<uint64_t>(1) << m_bitsPerCoordinate) - 1);
    const PDFReal xScaleRatio = (m_xmax - m_xmin) * vertexScaleRatio;
    const PDFReal yScaleRatio = (m_ymax - m_ymin) * vertexScaleRatio;
    const PDFReal colorScaleRatio = 1.0 / double((static_cast<uint64_t>(1) << m_bitsPerComponent) - 1);

    PDFTensorPatches patches;
    patches.reserve(patchCount);

    PDFBitReader reader(&m_data, 8);

    auto readFlags = [this, &reader]() -> uint8_t
    {
        return reader.read(m_bitsPerFlag);
    };

    auto readPoint = [this, &reader, &patternSpaceToDeviceSpaceMatrix, xScaleRatio, yScaleRatio]() -> QPointF
    {
        const PDFReal x = m_xmin + (reader.read(m_bitsPerCoordinate)) * xScaleRatio;
        const PDFReal y = m_ymin + (reader.read(m_bitsPerCoordinate)) * yScaleRatio;
        return patternSpaceToDeviceSpaceMatrix.map(QPointF(x, y));
    };

    auto readColor = [this, &reader, colorScaleRatio]() -> PDFColor
    {
        PDFColor color;
        color.resize(m_colorComponentCount);

        for (size_t i = 0; i < m_colorComponentCount; ++i)
        {
            const double cMin = m_limits[2 * i + 0];
            const double cMax = m_limits[2 * i + 1];
            color[i] = cMin + (reader.read(m_bitsPerComponent)) * (cMax - cMin) * colorScaleRatio;
        }

        return getColor(color);
    };

    for (size_t i = 0; i < patchCount; ++i)
    {
        const uint8_t flags = readFlags();
        switch (flags)
        {
            case 0:
            {
                PDFTensorPatch::PointMatrix P = { };
                PDFTensorPatch::Colors colors = { };

                P[0][0] = readPoint();
                P[0][1] = readPoint();
                P[0][2] = readPoint();
                P[0][3] = readPoint();
                P[1][3] = readPoint();
                P[2][3] = readPoint();
                P[3][3] = readPoint();
                P[3][2] = readPoint();
                P[3][1] = readPoint();
                P[3][0] = readPoint();
                P[2][0] = readPoint();
                P[1][0] = readPoint();
                P[1][1] = readPoint();
                P[1][2] = readPoint();
                P[2][2] = readPoint();
                P[2][1] = readPoint();

                colors[PDFTensorPatch::C_00] = readColor();
                colors[PDFTensorPatch::C_03] = readColor();
                colors[PDFTensorPatch::C_33] = readColor();
                colors[PDFTensorPatch::C_30] = readColor();

                patches.emplace_back(P, colors);
                break;
            }

            case 1:
            {
                if (patches.empty())
                {
                    throw PDFParserException(PDFTranslationContext::tr("Nonzero flag for first patch (flags = %1).").arg(flags));
                }

                const PDFTensorPatch& previousPatch = patches.back();
                const PDFTensorPatch::PointMatrix& PPrevious = previousPatch.getP();
                const PDFTensorPatch::Colors& colorsPrevious = previousPatch.getColors();

                PDFTensorPatch::PointMatrix P = { };
                PDFTensorPatch::Colors colors = { };

                P[1][3] = readPoint();
                P[2][3] = readPoint();
                P[3][3] = readPoint();
                P[3][2] = readPoint();
                P[3][1] = readPoint();
                P[3][0] = readPoint();
                P[2][0] = readPoint();
                P[1][0] = readPoint();
                P[1][1] = readPoint();
                P[1][2] = readPoint();
                P[2][2] = readPoint();
                P[2][1] = readPoint();

                colors[PDFTensorPatch::C_33] = readColor();
                colors[PDFTensorPatch::C_30] = readColor();

                // Copy data from previous patch according the PDF specification:
                P[0][0] = PPrevious[0][3];
                P[0][1] = PPrevious[1][3];
                P[0][2] = PPrevious[2][3];
                P[0][3] = PPrevious[3][3];

                colors[PDFTensorPatch::C_00] = colorsPrevious[PDFTensorPatch::C_03];
                colors[PDFTensorPatch::C_03] = colorsPrevious[PDFTensorPatch::C_33];

                patches.emplace_back(P, colors);
                break;
            }

            case 2:
            {
                if (patches.empty())
                {
                    throw PDFParserException(PDFTranslationContext::tr("Nonzero flag for first patch (flags = %1).").arg(flags));
                }

                const PDFTensorPatch& previousPatch = patches.back();
                const PDFTensorPatch::PointMatrix& PPrevious = previousPatch.getP();
                const PDFTensorPatch::Colors& colorsPrevious = previousPatch.getColors();

                PDFTensorPatch::PointMatrix P = { };
                PDFTensorPatch::Colors colors = { };

                P[1][3] = readPoint();
                P[2][3] = readPoint();
                P[3][3] = readPoint();
                P[3][2] = readPoint();
                P[3][1] = readPoint();
                P[3][0] = readPoint();
                P[2][0] = readPoint();
                P[1][0] = readPoint();
                P[1][1] = readPoint();
                P[1][2] = readPoint();
                P[2][2] = readPoint();
                P[2][1] = readPoint();

                colors[PDFTensorPatch::C_33] = readColor();
                colors[PDFTensorPatch::C_30] = readColor();

                // Copy data from previous patch according the PDF specification:
                P[0][0] = PPrevious[3][3];
                P[0][1] = PPrevious[3][2];
                P[0][2] = PPrevious[3][1];
                P[0][3] = PPrevious[3][0];

                colors[PDFTensorPatch::C_00] = colorsPrevious[PDFTensorPatch::C_33];
                colors[PDFTensorPatch::C_03] = colorsPrevious[PDFTensorPatch::C_30];

                patches.emplace_back(P, colors);
                break;
            }

            case 3:
            {
                if (patches.empty())
                {
                    throw PDFParserException(PDFTranslationContext::tr("Nonzero flag for first patch (flags = %1).").arg(flags));
                }

                const PDFTensorPatch& previousPatch = patches.back();
                const PDFTensorPatch::PointMatrix& PPrevious = previousPatch.getP();
                const PDFTensorPatch::Colors& colorsPrevious = previousPatch.getColors();

                PDFTensorPatch::PointMatrix P = { };
                PDFTensorPatch::Colors colors = { };

                P[1][3] = readPoint();
                P[2][3] = readPoint();
                P[3][3] = readPoint();
                P[3][2] = readPoint();
                P[3][1] = readPoint();
                P[3][0] = readPoint();
                P[2][0] = readPoint();
                P[1][0] = readPoint();
                P[1][1] = readPoint();
                P[1][2] = readPoint();
                P[2][2] = readPoint();
                P[2][1] = readPoint();

                colors[PDFTensorPatch::C_33] = readColor();
                colors[PDFTensorPatch::C_30] = readColor();

                // Copy data from previous patch according the PDF specification:
                P[0][0] = PPrevious[3][0];
                P[0][1] = PPrevious[2][0];
                P[0][2] = PPrevious[1][0];
                P[0][3] = PPrevious[0][0];

                colors[PDFTensorPatch::C_00] = colorsPrevious[PDFTensorPatch::C_30];
                colors[PDFTensorPatch::C_03] = colorsPrevious[PDFTensorPatch::C_00];

                patches.emplace_back(P, colors);
                break;
            }

            default:
                throw PDFParserException(PDFTranslationContext::tr("Invalid data in tensor product patch shading (flags = %1).").arg(flags));
        }
    }

    fillMesh(mesh, settings, patches);
    return mesh;
}

void PDFTensorProductPatchShadingBase::fillMesh(PDFMesh& mesh,
                                                const PDFMeshQualitySettings& settings,
                                                const PDFTensorPatch& patch) const
{
    // We implement algorithm similar to Ruppert's algorithm (see https://en.wikipedia.org/wiki/Ruppert%27s_algorithm), but
    // we do not need a mesh for FEM calculation, so we do not care about quality of the triangles (we can have triangles with
    // very small angles). We just need to meet these conditions:
    //
    //      1) Mesh is dense enough (to satisfy at least preferred mesh resolution)
    //      2) Mesh is more dense, where it is deformed (high surface curvature)
    //      3) Mesh will also correctly consider color interpolation
    //
    // We will determine maximum surface curvature of the surface (by evaluating test points - this is not reliable, but
    // it will suffice), then start meshing. We must also handle case, when surface maximal curvature is almost zero - then it is
    // probably a rectangle (or something like that). We cannot assume, that directions u,v are principal directions of the surface.
    // So, we will use the sum of curvatures in two perpendicular directions - u,v and we will hope, that it will be OK and will be
    // around mean surface curvature.

    std::atomic<PDFReal> maximalCurvature(0.0);

    Q_ASSERT(settings.patchTestPoints > 2);
    const PDFReal testPointScale = 1.0 / (settings.patchTestPoints - 1.0);
    PDFIntegerRange<PDFInteger> range(0, settings.patchTestPoints * settings.patchTestPoints);
    auto updateCurvature = [&](PDFInteger i)
    {
        PDFInteger row = i / settings.patchTestPoints;
        PDFInteger column = i % settings.patchTestPoints;

        const PDFReal u = column * testPointScale;
        const PDFReal v = row * testPointScale;

        const PDFReal curvature = patch.getCurvature_u(u, v) + patch.getCurvature_v(u, v);

        // Atomically update the maximum curvature
        PDFReal previousCurvature = maximalCurvature;
        while (previousCurvature < curvature && !maximalCurvature.compare_exchange_weak(previousCurvature, curvature)) { }
    };
    std::for_each(std::execution::parallel_policy(), range.begin(), range.end(), updateCurvature);

    struct Triangle
    {
        std::array<QPointF, 3> uvCoordinates;
        std::array<QPointF, 3> devicePoints;

        QPointF getCenter() const
        {
            constexpr PDFReal coefficient = 1.0 / 3.0;
            return (uvCoordinates[0] + uvCoordinates[1] + uvCoordinates[2]) * coefficient;
        }

        PDFReal getCurvature(const PDFTensorPatch& patch) const
        {
            QPointF uv = getCenter();
            return patch.getCurvature_u(uv.x(), uv.y()) + patch.getCurvature_v(uv.x(), uv.y());
        }

        void fillTriangleDevicePoints(const PDFTensorPatch& patch)
        {
            Q_ASSERT(uvCoordinates.size() == devicePoints.size());
            for (size_t i = 0; i < uvCoordinates.size(); ++i)
            {
                devicePoints[i] = patch.getValue(uvCoordinates[i].x(), uvCoordinates[i].y());
            }
        }
    };

    auto getColorForUV = [&](PDFReal u, PDFReal v)
    {
        // Perform bilinear interpolation of colors, u is column, v is row
        const PDFTensorPatch::Colors& colors = patch.getColors();

        const PDFColor& topLeft = colors[PDFTensorPatch::C_00];
        const PDFColor& topRight = colors[PDFTensorPatch::C_30];
        const PDFColor& bottomLeft = colors[PDFTensorPatch::C_03];
        const PDFColor& bottomRight = colors[PDFTensorPatch::C_33];

        PDFColor top = PDFAbstractColorSpace::mixColors(topLeft, topRight, u);
        PDFColor bottom = PDFAbstractColorSpace::mixColors(bottomLeft, bottomRight, u);
        PDFColor interpolated = PDFAbstractColorSpace::mixColors(top, bottom, v);

        return interpolated;
    };

    Triangle workStartA;
    workStartA.uvCoordinates = { QPointF(0.0, 0.0), QPointF(1.0, 0.0), QPointF(0.0, 1.0) };
    workStartA.fillTriangleDevicePoints(patch);
    Triangle workStartB;
    workStartB.uvCoordinates = { QPointF(1.0, 0.0), QPointF(1.0, 1.0), QPointF(0.0, 1.0) };
    workStartB.fillTriangleDevicePoints(patch);

    std::vector<Triangle> unfinishedTriangles = { workStartA, workStartB };
    std::vector<Triangle> finishedTriangles;

    auto addTriangles = [&](std::array<QPointF, 3> uvCoordinates)
    {
        Triangle triangle;
        triangle.uvCoordinates = uvCoordinates;
        triangle.fillTriangleDevicePoints(patch);
        unfinishedTriangles.push_back(triangle);
    };

    while (!unfinishedTriangles.empty())
    {
        Triangle triangle = unfinishedTriangles.back();
        unfinishedTriangles.pop_back();

        // Should we divide the triangle? These conditions should be verified:
        //  1) Largest edge of triangle in device space exceeds preferred size of mesh
        //  2) Curvature of the triangle is too high (and largest edge doesn't exceed minimal size of mesh)
        //  3) Color difference is too high (and largest edge doesn't exceed minimal size of mesh)

        // First, verify, if we can subdivide the triangle
        QLineF v01(triangle.devicePoints[0], triangle.devicePoints[1]);
        QLineF v02(triangle.devicePoints[0], triangle.devicePoints[2]);
        QLineF v12(triangle.devicePoints[1], triangle.devicePoints[2]);

        const qreal length01 = v01.length();
        const qreal length02 = v02.length();
        const qreal length12 = v12.length();
        const qreal maxLength = qMax(length01, qMax(length02, length12));

        const PDFReal curvature = triangle.getCurvature(patch);
        const PDFReal curvatureRatio = curvature / maximalCurvature;

        // Calculate target length
        PDFReal targetLength = settings.preferredMeshResolution;
        if (curvatureRatio <= settings.patchResolutionMappingRatioLow)
        {
            Q_ASSERT(targetLength == settings.preferredMeshResolution);
        }
        else if (curvatureRatio >= settings.patchResolutionMappingRatioHigh)
        {
            targetLength = settings.minimalMeshResolution;
        }
        else
        {
            targetLength = interpolate(curvatureRatio, settings.patchResolutionMappingRatioLow, settings.patchResolutionMappingRatioHigh, settings.preferredMeshResolution, settings.minimalMeshResolution);
        }

        const PDFColor c0 = getColorForUV(triangle.uvCoordinates[0].x(), triangle.uvCoordinates[0].y());
        const PDFColor c1 = getColorForUV(triangle.uvCoordinates[1].x(), triangle.uvCoordinates[1].y());
        const PDFColor c2 = getColorForUV(triangle.uvCoordinates[2].x(), triangle.uvCoordinates[2].y());

        const bool isColorEqual = PDFAbstractColorSpace::isColorEqual(c0, c1, settings.tolerance) &&
                                  PDFAbstractColorSpace::isColorEqual(c0, c2, settings.tolerance) &&
                                  PDFAbstractColorSpace::isColorEqual(c1, c2, settings.tolerance);
        const bool canSubdivide = maxLength >= settings.minimalMeshResolution * 2.0; // If we subdivide, we will have length at least settings.minimalMeshResolution
        const bool shouldSubdivide = maxLength >= targetLength;

        if ((!isColorEqual || shouldSubdivide) && canSubdivide)
        {
            QPointF v0 = triangle.uvCoordinates[0];
            QPointF v1 = triangle.uvCoordinates[1];
            QPointF v2 = triangle.uvCoordinates[2];

            if (length12 == maxLength)
            {
                // We split line (v1, v2), create two triangles, (v0, v1, vx) and (v0, v2, vx), where
                // vx is centerpoint of line (v1, v2).

                QLineF v12(v1, v2);
                QPointF vx = v12.center();

                addTriangles({ v0, v1, vx });
                addTriangles({ v0, v2, vx });
            }
            else if (length02 == maxLength)
            {
                // We split line (v0, v2), create two triangles, (v0, v1, vx) and (v1, v2, vx), where
                // vx is centerpoint of line (v0, v2).

                QLineF v02(v0, v2);
                QPointF vx = v02.center();

                addTriangles({ v0, v1, vx });
                addTriangles({ v1, v2, vx });
            }
            else
            {
                Q_ASSERT(length01 == maxLength);

                // We split line (v0, v1), create two triangles, (v0, v2, vx) and (v1, v2, vx), where
                // vx is centerpoint of line (v0, v1).

                QLineF v01(v0, v1);
                QPointF vx = v01.center();

                addTriangles({ v0, v2, vx });
                addTriangles({ v1, v2, vx });
            }
        }
        else
        {
            finishedTriangles.emplace_back(qMove(triangle));
        }
    }

    Q_ASSERT(unfinishedTriangles.empty());

    std::vector<QPointF> vertices;
    std::vector<PDFMesh::Triangle> triangles;

    vertices.reserve(finishedTriangles.size() * 3);
    triangles.reserve(finishedTriangles.size());

    size_t vertexIndex = 0;
    for (const Triangle& triangle : finishedTriangles)
    {
        vertices.push_back(triangle.devicePoints[0]);
        vertices.push_back(triangle.devicePoints[1]);
        vertices.push_back(triangle.devicePoints[2]);

        QPointF center = triangle.getCenter();
        PDFColor color = getColorForUV(center.x(), center.y());
        QRgb rgbColor = m_colorSpace->getColor(color).rgb();

        PDFMesh::Triangle meshTriangle;
        meshTriangle.v1 = static_cast<uint32_t>(vertexIndex++);
        meshTriangle.v2 = static_cast<uint32_t>(vertexIndex++);
        meshTriangle.v3 = static_cast<uint32_t>(vertexIndex++);
        meshTriangle.color = rgbColor;
        triangles.push_back(meshTriangle);
    }

    mesh.setVertices(qMove(vertices));
    mesh.setTriangles(qMove(triangles));
}

void PDFTensorProductPatchShadingBase::fillMesh(PDFMesh& mesh,
                                                const PDFMeshQualitySettings& settings,
                                                const PDFTensorPatches& patches) const
{
    for (const auto& patch : patches)
    {
        fillMesh(mesh, settings, patch);
    }
}

// TODO: Apply graphic state of the pattern
// TODO: Implement settings of meshing in the settings dialog
// TODO: iota - replace for PDFIntegerRange

}   // namespace pdf