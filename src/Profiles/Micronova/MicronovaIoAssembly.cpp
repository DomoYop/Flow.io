#include "Profiles/Micronova/MicronovaIoAssembly.h"

#include "Profiles/Micronova/MicronovaProfile.h"

#include "Core/Services/IIO.h"

#include <stdio.h>

namespace {

constexpr PhysicalPortId kMicronovaAuxOutputPort = 1;
constexpr PhysicalPortId kMicronovaTemperaturePort = 2;

bool copyId_(char* out, size_t outLen, const char* value)
{
    if (!out || outLen == 0U || !value) return false;
    const int wrote = snprintf(out, outLen, "%s", value);
    return wrote > 0 && (size_t)wrote < outLen;
}

}  // namespace

namespace Profiles {
namespace Micronova {

bool configureIoModule(const BoardSpec& board, ModuleInstances& modules)
{
    const IoPointSpec* aux = boardFindIoPoint(board, BoardSignal::Relay1);
    const OneWireBusSpec* temp = boardFindOneWire(board, BoardSignal::TempProbe1);
    if (!aux || aux->capability != IoCapability::DigitalOut) return false;
    if (!temp) return false;

    modules.ioBindingPorts[0] = IOBindingPortSpec{
        kMicronovaAuxOutputPort,
        IO_BACKEND_GPIO,
        aux->pin,
        IO_PORT_DIR_OUT,
        "aux_output"
    };
    modules.ioBindingPorts[1] = IOBindingPortSpec{
        kMicronovaTemperaturePort,
        IO_BACKEND_DS18B20,
        0, // Bus 0 (eau).
        IO_PORT_DIR_IN,
        "DS18 temp"
    };

    modules.ioModule.setBindingPorts(modules.ioBindingPorts, 2);
    modules.ioModule.setOneWireBuses(&modules.oneWireTemperature, nullptr);

    IOEndpointRegistration outReg{};
    if (!copyId_(outReg.id, sizeof(outReg.id), "aux_output")) return false;
    outReg.ioId = (IoId)(IO_ID_DO_BASE + 0);
    IODigitalOutputSlotConfig outCfg{};
    outCfg.bindingPort = kMicronovaAuxOutputPort;
    outCfg.activeHigh = true;
    outCfg.initialOn = false;
    outCfg.momentary = false;
    outCfg.pulseMs = 0;
    if (!modules.ioModule.defineDigitalOutput(outReg, outCfg)) return false;

    IOEndpointRegistration tempReg{};
    if (!copyId_(tempReg.id, sizeof(tempReg.id), "local_temperature")) return false;
    tempReg.ioId = (IoId)(IO_ID_AI_BASE + 0);
    IOAnalogSlotConfig tempCfg{};
    tempCfg.bindingPort = kMicronovaTemperaturePort;
    tempCfg.c0 = 1.0f;
    tempCfg.c1 = 0.0f;
    tempCfg.precision = 1;
    if (!modules.ioModule.defineAnalogInput(tempReg, tempCfg)) return false;

    return true;
}

}  // namespace Micronova
}  // namespace Profiles
