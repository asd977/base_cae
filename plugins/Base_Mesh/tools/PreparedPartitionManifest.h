#pragma once

// Persistent, read-only manifest for a topology-finalized OCC BREP.
// This is an INDEX TRANSLATION for one verified pair of files, not a stable
// geometry identifier and not a replacement for Geometry-module adoption.
// Parse outside NETGEN with no Qt dependency (the bridge DLL intentionally
// does not link Qt). Validate every ID before using it as a TopExp index.

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cmath>
#include <cstddef>
#include <fstream>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace CaePreparedPartition
{
struct Mapping
{
    double fuzzyTolerance = -1.0;
    int sourceSolidCount = 0;
    int preparedSolidCount = 0;
    int sourceFaceCount = 0;
    int preparedFaceCount = 0;
    int sourceEdgeCount = 0;
    int preparedEdgeCount = 0;
    std::vector<int> solidImage; // index 0 unused; old solid index -> new solid index
    std::vector<std::vector<int>> faceImages;
    std::vector<std::vector<int>> edgeImages;
};

inline std::vector<std::vector<int>> readImages(const boost::property_tree::ptree& tree,
                                                const char* property, const int sourceCount,
                                                const int preparedCount)
{
    if (sourceCount < 0 || preparedCount < 0)
        throw std::runtime_error("Invalid CAD topology counts in prepared manifest");
    std::vector<std::vector<int>> images(static_cast<std::size_t>(sourceCount) + 1);
    std::set<int> visited;
    for (const auto& element : tree.get_child(property))
    {
        const auto& item = element.second;
        const int source = item.get<int>("sourceIndex");
        if (source < 1 || source > sourceCount || !visited.insert(source).second)
            throw std::runtime_error("Duplicate/invalid source CAD index in prepared manifest");
        auto& mapped = images[source];
        std::set<int> distinct;
        for (const auto& output : item.get_child("resultIndices"))
        {
            const int index = output.second.get_value<int>();
            if (index < 1 || index > preparedCount || !distinct.insert(index).second)
                throw std::runtime_error("Duplicate/invalid prepared CAD index in manifest");
            mapped.push_back(index);
        }
    }
    if (static_cast<int>(visited.size()) != sourceCount)
        throw std::runtime_error("Incomplete source CAD index coverage in prepared manifest");
    return images;
}

inline Mapping load(const std::filesystem::path& manifestPath)
{
    boost::property_tree::ptree tree;
    std::ifstream input(manifestPath, std::ios::binary);
    if (!input) throw std::runtime_error("Prepared CAD manifest not found");
    boost::property_tree::read_json(input, tree);
    Mapping result;
    if (tree.get<int>("format") != 2)
        throw std::runtime_error("Prepared CAD manifest version is unsupported");
    result.fuzzyTolerance = tree.get<double>("fuzzyTolerance");
    if (!std::isfinite(result.fuzzyTolerance) || result.fuzzyTolerance < 0.0)
        throw std::runtime_error("Invalid prepared CAD manifest identity");
    result.sourceSolidCount = tree.get<int>("inputSolidCount");
    result.preparedSolidCount = tree.get<int>("outputSolidCount");
    result.sourceFaceCount = tree.get<int>("inputFaceCount");
    result.preparedFaceCount = tree.get<int>("outputFaceCount");
    result.sourceEdgeCount = tree.get<int>("inputEdgeCount");
    result.preparedEdgeCount = tree.get<int>("outputEdgeCount");
    if (result.sourceSolidCount <= 0 || result.sourceSolidCount != result.preparedSolidCount ||
        result.sourceFaceCount < 0 || result.sourceEdgeCount < 0 ||
        result.preparedFaceCount < 0 || result.preparedEdgeCount < 0)
        throw std::runtime_error("Invalid prepared CAD topology counts");
    result.solidImage.assign(static_cast<std::size_t>(result.sourceSolidCount) + 1, 0);
    std::set<int> solids;
    for (const auto& element : tree.get_child("solidMap"))
    {
        const int source = element.second.get<int>("sourceIndex");
        const int prepared = element.second.get<int>("resultIndex");
        if (source < 1 || source > result.sourceSolidCount ||
            prepared < 1 || prepared > result.preparedSolidCount ||
            result.solidImage[source] || !solids.insert(prepared).second)
            throw std::runtime_error("Invalid or duplicate CAD Solid map");
        result.solidImage[source] = prepared;
    }
    if (static_cast<int>(solids.size()) != result.sourceSolidCount)
        throw std::runtime_error("Prepared CAD Solid map is incomplete");
    result.faceImages = readImages(tree, "faceMap", result.sourceFaceCount, result.preparedFaceCount);
    result.edgeImages = readImages(tree, "edgeMap", result.sourceEdgeCount, result.preparedEdgeCount);
    return result;
}
} // namespace CaePreparedPartition
