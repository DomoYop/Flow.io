#pragma once
/**
 * @file PoolIoAssembly.h
 * @brief Construction commune des endpoints E/S piscine depuis kPoolRoles.
 *
 * Les profils ne fournissent plus que leur brochage : la table des ports
 * (kBindingPorts), les defaults de brochage par role (*IoLayout.h) et les
 * endpoints hors role (DIN libres, sorties COMP...). Toute la mecanique
 * d'enregistrement des slots est ici, partagee entre profils.
 */

#include "Domain/DomainSpec.h"
#include "Domain/IoRoleDefaults.h"

class IOModule;

struct PoolIoExtraEndpoint {
    IoId ioId = IO_ID_INVALID;
    PhysicalPortId port = IO_PORT_INVALID;
};

struct PoolIoProfileSpec {
    const IOBindingPortSpec* ports = nullptr;
    uint8_t portCount = 0;
    const AnalogRoleDefault* analogDefaults = nullptr;
    uint8_t analogDefaultCount = 0;
    const DigitalInputRoleDefault* dinDefaults = nullptr;
    uint8_t dinDefaultCount = 0;
    const DigitalOutputRoleDefault* doutDefaults = nullptr;
    uint8_t doutDefaultCount = 0;
    // Endpoints hors role : entrees DIN restantes, sorties d'extension.
    const PoolIoExtraEndpoint* extraDigitalInputs = nullptr;
    uint8_t extraDigitalInputCount = 0;
    const PoolIoExtraEndpoint* extraDigitalOutputs = nullptr;
    uint8_t extraDigitalOutputCount = 0;
};

namespace PoolIo {

/** Enregistre tous les endpoints (roles + extras). false = erreur de spec. */
bool configure(const DomainSpec& domain, IOModule& io, const PoolIoProfileSpec& spec);

}  // namespace PoolIo
