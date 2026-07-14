#pragma once
/**
 * @file IoRoleDefaults.h
 * @brief Structs partagees des defaults de brochage par role metier.
 *
 * Les DONNEES restent par profil (ports, polarites, momentary different
 * reellement entre cartes) dans les *IoLayout.h ; seules les structs et les
 * finders sont definis ici, une fois.
 */

#include <stdint.h>

#include "Domain/DomainTypes.h"
#include "Modules/IOModule/IOModuleTypes.h"

struct AnalogRoleDefault {
    DomainSlotId domainSlot;    // Besoin fonctionnel de la sonde.
    PhysicalPortId bindingPort; // Port physique d'usine.
    float c0;                   // Calibration offset/intercept.
    float c1;                   // Calibration gain/slope.
    int32_t precision;          // Precision d'affichage (nb de decimales).
};

struct DigitalInputRoleDefault {
    DomainSlotId domainSlot;
    PhysicalPortId bindingPort;
    uint8_t mode;               // Etat ou compteur.
    uint8_t edgeMode;           // Front pris en compte.
    uint32_t debounceUs;        // Debounce en microsecondes.
};

struct DigitalOutputRoleDefault {
    DomainSlotId domainSlot;
    PhysicalPortId bindingPort;
    bool activeHigh;            // Polarite de commande logique.
    bool retainOnWarmReboot;    // Conserve le latch expander sur reboot chaud.
    bool momentary;             // Sortie impulsionnelle.
    uint16_t pulseMs;           // Duree d'impulsion en ms.
};

template <typename T, uint8_t N>
constexpr const T* roleDefaultFor(const T (&entries)[N], DomainSlotId domainSlot)
{
    for (const T& entry : entries) {
        if (entry.domainSlot == domainSlot) return &entry;
    }
    return nullptr;
}

inline const AnalogRoleDefault* roleDefaultIn(const AnalogRoleDefault* entries, uint8_t count, DomainSlotId id)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].domainSlot == id) return &entries[i];
    }
    return nullptr;
}

inline const DigitalInputRoleDefault* roleDefaultIn(const DigitalInputRoleDefault* entries, uint8_t count, DomainSlotId id)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].domainSlot == id) return &entries[i];
    }
    return nullptr;
}

inline const DigitalOutputRoleDefault* roleDefaultIn(const DigitalOutputRoleDefault* entries, uint8_t count, DomainSlotId id)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].domainSlot == id) return &entries[i];
    }
    return nullptr;
}
