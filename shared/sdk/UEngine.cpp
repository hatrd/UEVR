#include <spdlog/spdlog.h>
#include <bddisasm.h>

#include <utility/Scan.hpp>
#include <utility/Module.hpp>

#include "CVar.hpp"

#include "EngineModule.hpp"
#include "UEngine.hpp"

namespace sdk {
UEngine* UEngine::get() {
    static auto engine = []() -> UEngine** {
        spdlog::info("Attempting to locate GEngine...");

        const auto module = sdk::get_ue_module(L"Engine");
        const auto calibrate_tilt_fn = utility::find_function_from_string_ref(module, L"CALIBRATEMOTION");

        if (!calibrate_tilt_fn) {
            spdlog::error("Failed to find CalibrateTilt function!");
            return (UEngine**)nullptr;
        }

        spdlog::info("CalibrateTilt function: {:x}", (uintptr_t)*calibrate_tilt_fn);

        // Use bddisasm to find the first ptr mov into a register
        uint8_t* ip = (uint8_t*)*calibrate_tilt_fn;

        for (auto i = 0; i < 50; ++i) {
            INSTRUX ix{};
            const auto status = NdDecodeEx(&ix, (ND_UINT8*)ip, 1000, ND_CODE_64, ND_DATA_64);

            if (!ND_SUCCESS(status)) {
                spdlog::info("Decoding failed with error {:x}!", (uint32_t)status);
                break;
            }

            if (ix.Instruction == ND_INS_MOV && ix.Operands[0].Type == ND_OP_REG && ix.Operands[1].Type == ND_OP_MEM && ix.Operands[1].Info.Memory.IsRipRel) {
                const auto offset = ix.Operands[1].Info.Memory.Disp;
                const auto result = (UEngine**)((uint8_t*)ip + ix.Length + offset);

                spdlog::info("Found GEngine at {:x}", (uintptr_t)result);
                return result;
            }

            ip += ix.Length;
        }

        spdlog::error("Failed to find GEngine!");
        return nullptr;
    }();

    if (engine == nullptr) {
        return nullptr;
    }

    return *engine;
}

void UEngine::initialize_hmd_device() {
    static auto enable_stereo_emulation_cvar = vr::get_enable_stereo_emulation_cvar();

    if (!enable_stereo_emulation_cvar) {
        spdlog::error("Failed to locate r.EnableStereoEmulation cvar, cannot inject stereo rendering device at runtime.");
        return;
    }

    static auto addr = []() -> std::optional<uintptr_t> {
        spdlog::info("Searching for InitializeHMDDevice function...");

        const auto module_within = utility::get_module_within(*enable_stereo_emulation_cvar);

        if (!module_within) {
            spdlog::error("Failed to find module containing r.EnableStereoEmulation cvar!");
            return std::nullopt;
        }

        spdlog::info("Module containing r.EnableStereoEmulation cvar: {:x}", (uintptr_t)*module_within);

        const auto enable_stereo_emulation_cvar_ref = utility::scan_reference(*module_within, *enable_stereo_emulation_cvar);

        if (!enable_stereo_emulation_cvar_ref) {
            spdlog::error("Failed to find r.EnableStereoEmulation cvar reference!");
            return std::nullopt;
        }

        spdlog::info("Found r.EnableStereoEmulation cvar reference at {:x}", (uintptr_t)*enable_stereo_emulation_cvar_ref);

        auto result = utility::find_function_start(*enable_stereo_emulation_cvar_ref);

        // scan backwards for the function start until it's no longer some random label within a function, but the function start itself.
        while (result) {
            // This means it's a valid vtable function, and we have found the function start.
            if (utility::scan_ptr(*utility::get_module_within(*result), *result)) {
                break;
            }

            spdlog::info("result was not really the function, scanning again...");
            result = utility::find_function_start(*result - 1);
        }

        if (result) {
            spdlog::info("Found InitializeHMDDevice at {:x}", (uintptr_t)*result);
        }

        return result;
    }();

    if (!addr) {
        spdlog::error("Failed to locate InitializeHMDDevice function, cannot inject stereo rendering device at runtime.");
        return;
    }

    auto fn = (void(*)(UEngine*))*addr;
    fn(this);
}
}