#include "vkroots.h"

template <>
struct std::formatter<VkResult> : std::formatter<string> {
    auto format(const VkResult format, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{} ({})", std::to_underlying(format), vkroots::helpers::enumString(format));
    }
};

template <>
struct std::formatter<XrResult> : std::formatter<string> {
    auto format(const XrResult format, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", std::to_underlying(format));
    }
};

template <>
struct std::formatter<VkFormat> : std::formatter<string> {
    auto format(const VkFormat format, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{} ({})", std::to_underlying(format), vkroots::helpers::enumString(format));
    }
};

template <>
struct std::formatter<DXGI_FORMAT> : std::formatter<string> {
    auto format(const DXGI_FORMAT format, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", std::to_underlying(format));
    }
};

template <>
struct std::formatter<glm::fmat3> : std::formatter<string> {
    auto format(const glm::fmat3 mtx, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "row0: [{}, {}, {}] row1: [{}, {}, {}] row2: [{}, {}, {}]",
            mtx[0][0], mtx[0][1], mtx[0][2],
            mtx[1][0], mtx[1][1], mtx[1][2],
            mtx[2][0], mtx[2][1], mtx[2][2]
        );
    }
};

template <>
struct std::formatter<glm::fvec3> : std::formatter<string> {
    auto format(const glm::fvec3 mtx, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "[x={}, y={}, z={}]", mtx.x, mtx.y, mtx.z);
    }
};

template <>
struct std::formatter<glm::fquat> : std::formatter<string> {
    auto format(const glm::fquat quat, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "[w={}, x={}, y={}, z={}] (euler: x={}, y={}, z={})", quat.w, quat.x, quat.y, quat.z, glm::degrees(glm::eulerAngles(quat)).x, glm::degrees(glm::eulerAngles(quat)).y, glm::degrees(glm::eulerAngles(quat)).z);
    }
};

template <>
struct std::formatter<BEMatrix34> : std::formatter<string> {
    auto format(const BEMatrix34 mtx, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "[x_x={}, y_x={}, z_x={}, pos_x={}] [x_y={}, x_y={}, z_y={}, pos_y={}] [x_z={}, y_z={}, z_z={}, pos_z={}]",
            mtx.x_x.getLE(), mtx.y_x.getLE(), mtx.z_x.getLE(), mtx.pos_x.getLE(),
            mtx.x_y.getLE(), mtx.y_y.getLE(), mtx.z_y.getLE(), mtx.pos_y.getLE(),
            mtx.x_z.getLE(), mtx.y_z.getLE(), mtx.z_z.getLE(), mtx.pos_z.getLE()
        );
    }
};


template <>
struct std::formatter<D3D_FEATURE_LEVEL> : std::formatter<string> {
    auto format(const D3D_FEATURE_LEVEL featureLevel, std::format_context& ctx) const {
        switch (featureLevel) {
            case D3D_FEATURE_LEVEL_1_0_CORE:
                return std::format_to(ctx.out(), "1.0");
            case D3D_FEATURE_LEVEL_9_1:
                return std::format_to(ctx.out(), "9.1");
            case D3D_FEATURE_LEVEL_9_2:
                return std::format_to(ctx.out(), "9.2");
            case D3D_FEATURE_LEVEL_9_3:
                return std::format_to(ctx.out(), "9.3");
            case D3D_FEATURE_LEVEL_10_0:
                return std::format_to(ctx.out(), "10.0");
            case D3D_FEATURE_LEVEL_10_1:
                return std::format_to(ctx.out(), "10.1");
            case D3D_FEATURE_LEVEL_11_0:
                return std::format_to(ctx.out(), "11.0");
            case D3D_FEATURE_LEVEL_11_1:
                return std::format_to(ctx.out(), "11.1");
            case D3D_FEATURE_LEVEL_12_0:
                return std::format_to(ctx.out(), "12.0");
            case D3D_FEATURE_LEVEL_12_1:
                return std::format_to(ctx.out(), "12.1");
            default:
                break;
        }
        return std::format_to(ctx.out(), "{:X}", std::to_underlying(featureLevel));
    }
};

class Log {
public:
    Log();
    ~Log();

    static void print(const char* message);

    template <class... Args>
    static void print(const char* format, Args&&... args) {
#ifdef _DEBUG
        Log::print(std::vformat(format, std::make_format_args(args...)).c_str());
#endif
    }

    static void printTimeElapsed(const char* message_prefix, LARGE_INTEGER time);
};

static void checkXRResult(const XrResult result, const char* errorMessage) {
    if (XR_FAILED(result)) {
        if (errorMessage == nullptr) {
            Log::print("[Error] An unknown error (result was {}) has occurred!", result);
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, std::format("An unknown error {} has occurred which caused a fatal crash!", result).c_str(), "An error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error("Undescribed error occurred!");
        }
        else {
            Log::print("[Error] Error {}: {}", result, errorMessage);
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, errorMessage, "A fatal error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error(errorMessage);
        }
    }
}

static void checkHResult(const HRESULT result, const char* errorMessage) {
    if (FAILED(result)) {
        if (errorMessage == nullptr) {
            Log::print("[Error] An unknown error (result was {}) has occurred!", result);
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, std::format("An unknown error {} has occurred which caused a fatal crash!", result).c_str(), "A fatal error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error("Undescribed error occurred!");
        }
        else {
            Log::print("[Error] Error {}: {}", result, errorMessage);
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, errorMessage, "A fatal error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error(errorMessage);
        }
    }
}

static void checkVkResult(const VkResult result, const char* errorMessage) {
    if (result != VK_SUCCESS) {
        if (errorMessage == nullptr) {
            Log::print("[Error] An unknown error (result was {}) has occurred!", (std::underlying_type_t<VkResult>)result);
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, std::format("An unknown error {} has occurred which caused a fatal crash!", (std::underlying_type_t<VkResult>)result).c_str(), "A fatal error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error("Undescribed error occurred!");
        }
        else {
            Log::print("[Error] Error {}: {}", (std::underlying_type_t<VkResult>)result, errorMessage);
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, errorMessage, "A fatal error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error(errorMessage);
        }
    }
}

static void checkAssert(const bool assert, const char* errorMessage) {
    if (!assert) {
        if (errorMessage == nullptr) {
            Log::print("[Error] Something unexpected happened that prevents further execution!");
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, "Something unexpected happened that prevents further execution!", "A fatal error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error("Unexpected assertion occurred!");
        }
        else {
            Log::print("[Error] {}", errorMessage);
#ifdef _DEBUG
            __debugbreak();
#endif
            MessageBoxA(NULL, errorMessage, "A fatal error occurred!", MB_OK | MB_ICONERROR);
            throw std::runtime_error(errorMessage);
        }
    }
}