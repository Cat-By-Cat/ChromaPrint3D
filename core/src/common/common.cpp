#include "chromaprint3d/common.h"
#include "chromaprint3d/error.h"

#include <algorithm>
#include <cctype>

namespace ChromaPrint3D {

std::string ToLayerOrderString(LayerOrder order) {
    switch (order) {
    case LayerOrder::Top2Bottom:
        return "Top2Bottom";
    case LayerOrder::Bottom2Top:
        return "Bottom2Top";
    }
    return "Top2Bottom";
}

LayerOrder FromLayerOrderString(const std::string& str) {
    if (str == "Top2Bottom") { return LayerOrder::Top2Bottom; }
    if (str == "Bottom2Top") { return LayerOrder::Bottom2Top; }
    throw FormatError("Invalid layer_order string: " + str);
}

NozzleSize ParseNozzleSize(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "n02" || s == "0.2" || s == "0.2mm") return NozzleSize::N02;
    return NozzleSize::N04;
}

FaceOrientation ParseFaceOrientation(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "facedown" || s == "face_down" || s == "down") return FaceOrientation::FaceDown;
    return FaceOrientation::FaceUp;
}

} // namespace ChromaPrint3D
