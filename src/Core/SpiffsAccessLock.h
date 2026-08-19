#pragma once
/**
 * @file SpiffsAccessLock.h
 * @brief Verrou partage autour de tout acces a la partition SPIFFS.
 *
 * `SPIFFS.end()` + `Update.write(U_SPIFFS)` reecrivent la partition sous les pieds
 * du driver SPIFFS, mais rien n'empechait jusqu'ici un autre module de continuer a
 * l'utiliser pendant les ~40 s d'ecriture : ActivityLogModule persiste chaque
 * evenement (`SPIFFS.open(..., FILE_APPEND)`) depuis sa propre tache, et
 * WebInterfaceModule sert ses fichiers statiques depuis SPIFFS a chaque requete.
 * Un `SPIFFS.end()` en pleine ecriture d'un fichier ouvert par une autre tache
 * laisse cette operation dans un etat indefini -- c'est ce qui a corrompu le
 * systeme de fichiers a plusieurs reprises le 2026-08-18, avec des vidages de
 * crash qui incriminaient a chaque fois une tache differente (async_tcp, EventBus,
 * mqtt) : pas la vraie coupable, seulement celle qui touchait la structure partagee
 * corrompue au moment du crash. Voir docs/notes/ota-spiffs-gzip-bilan.md.
 *
 * Mutex recursif : un meme appelant peut l'acquerir plusieurs fois sans deadlock
 * (persist_() imbrique deja un appel a rotateIfNeeded_(), qui touche SPIFFS lui
 * aussi).
 */

#include <stdint.h>

namespace SpiffsAccessLock {

/**
 * @brief Acquiert le verrou.
 *
 * Les lecteurs/ecrivains ordinaires (journal d'activite, serveur de fichiers
 * statiques) doivent passer un delai court : en cas d'echec, l'operation SPIFFS
 * est simplement sautee pour cette requete, ce qui est deja le comportement gere
 * par leurs appelants. L'ecrivain OTA passe un delai long, le temps qu'une
 * operation en cours (quelques millisecondes) se termine.
 */
bool acquire(uint32_t timeoutMs);

/** @brief Libere le verrou. A appeler exactement une fois par `acquire` reussi. */
void release();

/** @brief Garde RAII : acquiert au constructeur, libere au destructeur si tenu. */
class Guard {
public:
    explicit Guard(uint32_t timeoutMs) : held_(acquire(timeoutMs)) {}
    ~Guard() {
        if (held_) release();
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

    bool held() const { return held_; }

private:
    bool held_;
};

}  // namespace SpiffsAccessLock
