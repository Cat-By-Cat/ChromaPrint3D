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
    int colors               = 16;
    int min_region_area      = 10;
    float min_contour        = 10.0f;
    float min_hole_area      = 4.0f;
    float contour_simplify   = 0.45f;
    float topology_cleanup   = 0.15f;
    bool enable_coverage_fix = true;
    float min_coverage_ratio = 0.998f;
    bool svg_stroke          = true;
    float svg_stroke_w       = 0.5f;
    std::string log_level    = "info";
};

void PrintUsage(const char* exe) {
    std::printf("Usage: %s --image input.png [--out output.svg] [options]\n"
                "Options:\n"
                "  --colors N          Number of quantization colors (default 16)\n"
                "  --min-region N      Min region area in pixels (default 10)\n"
                "  --min-contour F     Min contour area in pixels (default 10)\n"
                "  --min-hole-area F   Minimum kept hole area in pixels^2 (default 4.0)\n"
                "  --contour-simplify F  Contour simplification strength (default 0.45)\n"
                "  --topology-cleanup F  Topology cleanup simplification (default 0.15)\n"
                "  --disable-coverage-fix Disable coverage patching\n"
                "  --min-coverage-ratio F Coverage fix trigger ratio (default 0.998)\n"
                "  --no-svg-stroke     Disable SVG stroke output (default on)\n"
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
        if (arg == "--min-region" && i + 1 < argc) {
            if (!ParseInt(argv[++i], opt.min_region_area) || opt.min_region_area < 0) {
                std::fprintf(stderr, "Invalid --min-region\n");
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
        if (arg == "--min-hole-area" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.min_hole_area) || opt.min_hole_area < 0.0f) {
                std::fprintf(stderr, "Invalid --min-hole-area\n");
                return false;
            }
            continue;
        }
        if (arg == "--contour-simplify" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.contour_simplify) || opt.contour_simplify < 0.0f) {
                std::fprintf(stderr, "Invalid --contour-simplify\n");
                return false;
            }
            continue;
        }
        if (arg == "--topology-cleanup" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.topology_cleanup) || opt.topology_cleanup < 0.0f) {
                std::fprintf(stderr, "Invalid --topology-cleanup\n");
                return false;
            }
            continue;
        }
        if (arg == "--disable-coverage-fix") {
            opt.enable_coverage_fix = false;
            continue;
        }
        if (arg == "--min-coverage-ratio" && i + 1 < argc) {
            if (!ParseFloat(argv[++i], opt.min_coverage_ratio) || opt.min_coverage_ratio < 0.0f ||
                opt.min_coverage_ratio > 1.0f) {
                std::fprintf(stderr, "Invalid --min-coverage-ratio\n");
                return false;
            }
            continue;
        }
        if (arg == "--no-svg-stroke") {
            opt.svg_stroke = false;
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
        cfg.num_colors          = opt.colors;
        cfg.min_region_area     = opt.min_region_area;
        cfg.min_contour_area    = opt.min_contour;
        cfg.min_hole_area       = opt.min_hole_area;
        cfg.contour_simplify    = opt.contour_simplify;
        cfg.topology_cleanup    = opt.topology_cleanup;
        cfg.enable_coverage_fix = opt.enable_coverage_fix;
        cfg.min_coverage_ratio  = opt.min_coverage_ratio;
        cfg.svg_enable_stroke   = opt.svg_stroke;
        cfg.svg_stroke_width    = opt.svg_stroke_w;

        spdlog::info("Vectorizing {} -> {}", opt.image_path, opt.out_path);
        spdlog::info("Colors={}, contour_simplify={:.2f}, topology_cleanup={:.2f}", cfg.num_colors,
                     cfg.contour_simplify, cfg.topology_cleanup);

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
