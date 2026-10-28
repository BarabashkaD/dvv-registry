#include "dvv/registry/component_info.h"

namespace dvv {

std::string_view kindName(Kind kind) noexcept {
    switch (kind) {
        case Kind::Source: return "Source";
        case Kind::FormatDetector: return "FormatDetector";
        case Kind::Exporter: return "Exporter";
        case Kind::Tool: return "Tool";
        case Kind::Metric: return "Metric";
        case Kind::Analyser: return "Analyser";
        case Kind::Processor: return "Processor";
    }
    return "Unknown";
}

}  // namespace dvv
