#pragma once
/**
 * @file IHA.h
 * @brief Home Assistant discovery service interface.
 */

// `absent` (dernier champ, defaut false via l'init par agregat existant) : une
// entree marquee absente publie un payload discovery vide retained (tombstone)
// qui supprime l'entite cote Home Assistant. Sert a masquer les equipements non
// selectionnes (type de desinfection, equipements optionnels).

/** @brief Static Home Assistant sensor discovery registration. */
struct HASensorEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* entityCategory;
    const char* icon;
    const char* unit;
    bool hasEntityName;
    const char* availabilityTemplate;
    bool isText;
    bool absent;
};

/** @brief Static Home Assistant binary sensor discovery registration. */
struct HABinarySensorEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* deviceClass;
    const char* entityCategory;
    const char* icon;
    bool absent;
};

/** @brief Static Home Assistant switch discovery registration. */
struct HASwitchEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* commandTopicSuffix;
    const char* payloadOn;
    const char* payloadOff;
    const char* icon;
    const char* entityCategory;
    bool absent;
};

/** @brief Static Home Assistant number discovery registration. */
struct HANumberEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* commandTopicSuffix;
    const char* commandTemplate;
    float minValue;
    float maxValue;
    float step;
    const char* mode;
    const char* entityCategory;
    const char* icon;
    const char* unit;
    bool absent;
};

/** @brief Static Home Assistant select discovery registration. */
struct HASelectEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* commandTopicSuffix;
    const char* commandTemplate;
    const char* optionsJson;
    const char* icon;
    const char* entityCategory;
    bool absent;
};

/** @brief Static Home Assistant button discovery registration. */
struct HAButtonEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* commandTopicSuffix;
    const char* payloadPress;
    const char* entityCategory;
    const char* icon;
    bool absent;
};

/** @brief Service used by modules to register static HA discovery entries and request refreshes. */
struct HAService {
    bool (*addSensor)(void* ctx, const HASensorEntry* entry);
    bool (*addBinarySensor)(void* ctx, const HABinarySensorEntry* entry);
    bool (*addSwitch)(void* ctx, const HASwitchEntry* entry);
    bool (*addNumber)(void* ctx, const HANumberEntry* entry);
    bool (*addSelect)(void* ctx, const HASelectEntry* entry);
    bool (*addButton)(void* ctx, const HAButtonEntry* entry);
    bool (*requestRefresh)(void* ctx);
    /** Mark a previously registered entity absent (tombstone) or present, by owner+objectSuffix. */
    bool (*setEntityAbsent)(void* ctx, const char* ownerId, const char* objectSuffix, bool absent);
    void* ctx;
};
