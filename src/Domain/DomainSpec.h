#pragma once

#include "Domain/DomainTypes.h"

struct DomainSpec {
    const char* name;
    const PoolRoleSpec* roles;
    uint8_t roleCount;
    const PoolDevicePreset* poolDevices;
    uint8_t poolDeviceCount;
    const PoolLogicDefaultsSpec* poolLogicDefaults;
    void (*configurationHook)(AppContext&);
};

inline const PoolRoleSpec* domainRoleById(const DomainSpec& domain, DomainSlotId id)
{
    for (uint8_t i = 0; i < domain.roleCount; ++i) {
        if (domain.roles[i].id == id) return &domain.roles[i];
    }
    return nullptr;
}

inline IoSlotId domainIoSlotForRole(const DomainSpec& domain, DomainSlotId id)
{
    const PoolRoleSpec* role = domainRoleById(domain, id);
    return role ? role->ioSlot : IO_SLOT_INVALID;
}

inline const PoolRoleSpec* domainRoleForIoSlot(const DomainSpec& domain, IoSlotId ioSlot)
{
    if (ioSlot == IO_SLOT_INVALID) return nullptr;
    for (uint8_t i = 0; i < domain.roleCount; ++i) {
        if (domain.roles[i].ioSlot == ioSlot) return &domain.roles[i];
    }
    return nullptr;
}
