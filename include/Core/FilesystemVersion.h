#pragma once
/**
 * @file FilesystemVersion.h
 * @brief Version du contenu SPIFFS, lue depuis l'image elle-meme.
 *
 * Pendant de FirmwareVersion : le firmware embarque sa version dans le .bin via
 * les macros FIRMW / FLOW_BUILD_REF, l'image SPIFFS embarque la sienne dans
 * /fsver.j (ecrit par scripts/prepare_spiffs_data.py au moment du build de
 * l'image). Fonctionne donc quelle que soit la voie de flash -- OTA, uploadfs,
 * usine -- contrairement a une version deduite du nom de fichier OTA.
 *
 * Chargement paresseux au premier appel, puis mis en cache.
 * Voir docs/notes/versionnage-firmware-et-spiffs.md
 */

namespace FilesystemVersion {

/** @brief true si l'image SPIFFS montee porte un descripteur de version lisible. */
bool present();

/** @brief Version produit, ex. "4.1.2" ; chaine vide si absente. */
const char* core();

/** @brief Horodatage du build de l'image, ex. "20260808.143512" ; vide si absent. */
const char* buildRef();

/** @brief "core+buildRef", ex. "4.1.2+20260808.143512" ; vide si absent. */
const char* full();

/**
 * @brief Empreinte du contenu reellement flashe, ex. "a1b2c3d4" ; vide si absente.
 *
 * Stable d'un build a l'autre tant que data/ ne change pas, contrairement au
 * buildRef : c'est ce qui distingue deux images que la version produit ne separe
 * pas (un i18n modifie ne fait pas bouger custom_version).
 */
const char* contentHash();

}  // namespace FilesystemVersion
