#include "chromaprint3d/logging.h"
#include "chromaprint3d/vectorizer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ChromaPrint3D;

namespace {

struct Options {
    std::string image_path;
    std::string out_path;
    int colors            = 16;
    float merge_lambda    = 25.0f;
    int min_region_area   = 10;
    float alpha_max       = 1.0f;
    float opt_tolerance   = 0.2f;
    bool curve_opt        = true;
    float curve_tolerance = 2.0f;
    float corner_thresh   = 135.0f;
    float min_contour     = 10.0f;
    float min_boundary    = 2.0f;
    int morph_kernel      = 3;
    bool svg_stroke       = false;
    float svg_stroke_w    = 0.5f;
    std::string log_level = "info";
};

void PrintUsage(const char* exe) {
    std::printf("Usage: %s --image input.png [--out output.svg] [options]\n"
                "Options:\n"
                "  --colors N          Number of quantization colors (default 32)\n"
                "  --merge-lambda F    Region merge threshold (default 500)\n"
                "  --min-region N      Min region area in pixels (default 10)\n"
                "  --alpha-max F       Corner detection threshold (default 1.0)\n"
                "  --opt-tolerance F   Curve optimization tolerance (default 0.2)\n"
                "  --no-curve-opt      Disable curve optimization\n"
                "  --curve-tolerance F Schneider fitting tolerance (default 2.0)\n"
                "  --corner-thresh F   Corner angle threshold in degrees (default 135)\n"
                "  --min-contour F     Min contour area in pixels (default 10)\n"
                "  --min-boundary F    Min boundary perimeter in pixels (default 2)\n"
                "  --morph-kernel N    Morphological kernel size, 0=disable (default 3)\n"
                "  --svg-stroke        Enable SVG stroke output (default off)\n"
                "  --svg-stroke-w F    SVG stroke width when enabled (default 0.5)\n"
                "  --log-level LEVEL   Log level: trace/debug/info/warn/error/off (default info)\n",
                exe);
}

bool ParseInt(const char* s, int& out) {
    if (!s) return false;
    try {
        size_t idx = 0;
        out        = std::stoi(s, &idx, 10);
        return idx == std::string(s).size();
    } catch (...) { return false; }
}

bool ParseFloat(const char* s, float& out) {
    if (!s) return false;
    try {
        size_t idx = 0;
        out        = std::stof(s, &idx);
        return idx == std::string(s).size();
    } catch (...) { return false; }
}

bool ParseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        }
        if (arg == "--image" && i + 1 < argc) {
            opt.image_path = argv[++i];
            continue;
        }
        if (arg == "--out" && i + 1 < argc) {
            opt.out_path = argv[++i];
            continue;
        }
        if (arg == "--colors" && i + 1 < argc) {
            if (!ParseInt(argv[++i], opt.colors) || opt.colors < 2) {
                std::fprintf(stderr, "Invalid --colors\n");
                return false;
            }
            continue;
        }
        if (arg == "--merge-lambda" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.merge_lambda)) {
                std::fprintf(stderr, "Invalid --merge-lambda\n");
                return false;
            }
            continue;
        }
        if (arg == "--min-region" && i + 1 < argc) {
            if (!ParseInt(argv[++i], opt.min_region_area) || opt.min_region_area < 0) {
                std::fprintf(stderr, "Invalid --min-region\n");
                return false;
            }
            continue;
        }
        if (arg == "--alpha-max" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.alpha_max)) {
                std::fprintf(stderr, "Invalid --alpha-max\n");
                return false;
            }
            continue;
        }
        if (arg == "--opt-tolerance" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.opt_tolerance)) {
                std::fprintf(stderr, "Invalid --opt-tolerance\n");
                return false;
            }
            continue;
        }
        if (arg == "--no-curve-opt") {
            opt.curve_opt = false;
            continue;
        }
        if (arg == "--curve-tolerance" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.curve_tolerance)) {
                std::fprintf(stderr, "Invalid --curve-tolerance\n");
                return false;
            }
            continue;
        }
        if (arg == "--corner-thresh" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.corner_thresh)) {
                std::fprintf(stderr, "Invalid --corner-thresh\n");
                return false;
            }
            continue;
        }
        if (arg == "--min-contour" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.min_contour)) {
                std::fprintf(stderr, "Invalid --min-contour\n");
                return false;
            }
            continue;
        }
        if (arg == "--min-boundary" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.min_boundary) || opt.min_boundary < 0.0f) {
                std::fprintf(stderr, "Invalid --min-boundary\n");
                return false;
            }
            continue;
        }
        if (arg == "--morph-kernel" && i + 1 < argc) {
            if (!ParseInt(argv[++i], opt.morph_kernel) || opt.morph_kernel < 0) {
                std::fprintf(stderr, "Invalid --morph-kernel\n");
                return false;
            }
            continue;
        }
        if (arg == "--svg-stroke") {
            opt.svg_stroke = true;
            continue;
        }
        if (arg == "--svg-stroke-w" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.svg_stroke_w) || opt.svg_stroke_w < 0.0f) {
                std::fprintf(stderr, "Invalid --svg-stroke-w\n");
                return false;
            }
            continue;
        }
        if (arg == "--log-level" && i + 1 < argc) {
            opt.log_level = argv[++i];
            continue;
        }
        std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
        PrintUsage(argv[0]);
        return false;
    }
    return true;
}

std::string DefaultOutPath(const std::string& image_path) {
    std::filesystem::path p(image_path);
    return (p.parent_path() / (p.stem().string() + ".svg")).string();
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!ParseArgs(argc, argv, opt)) return 1;

    InitLogging(ParseLogLevel(opt.log_level));

    if (opt.image_path.empty()) {
        std::fprintf(stderr, "Error: --image is required\n");
        PrintUsage(argv[0]);
        return 1;
    }
    if (opt.out_path.empty()) opt.out_path = DefaultOutPath(opt.image_path);

    try {
        VectorizerConfig cfg;
        cfg.num_colors             = opt.colors;
        cfg.merge_lambda           = opt.merge_lambda;
        cfg.min_region_area        = opt.min_region_area;
        cfg.alpha_max              = opt.alpha_max;
        cfg.opt_tolerance          = opt.opt_tolerance;
        cfg.enable_curve_opt       = opt.curve_opt;
        cfg.curve_tolerance        = opt.curve_tolerance;
        cfg.corner_threshold       = opt.corner_thresh;
        cfg.min_contour_area       = opt.min_contour;
        cfg.min_boundary_perimeter = opt.min_boundary;
        cfg.morph_kernel_size      = opt.morph_kernel;
        cfg.svg_enable_stroke      = opt.svg_stroke;
        cfg.svg_stroke_width       = opt.svg_stroke_w;

        spdlog::info("Vectorizing {} -> {}", opt.image_path, opt.out_path);
        spdlog::info("Colors={}, merge_lambda={:.0f}, alpha_max={:.2f}", cfg.num_colors,
                     cfg.merge_lambda, cfg.alpha_max);

        auto result = Vectorize(opt.image_path, cfg);

        std::ofstream ofs(opt.out_path);
        if (!ofs) throw std::runtime_error("Cannot open output file: " + opt.out_path);
        ofs << result.svg_content;
        ofs.close();

        spdlog::info("Done: {}x{}, {} shapes, palette size {}", result.width, result.height,
                     result.num_shapes, static_cast<int>(result.palette.size()));
        spdlog::info("Saved SVG to {}", opt.out_path);
    } catch (const std::exception& e) {
        spdlog::error("Failed: {}", e.what());
        return 1;
    }

    return 0;
}
