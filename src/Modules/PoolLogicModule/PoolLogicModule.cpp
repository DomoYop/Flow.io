/**
 * @file PoolLogicModule.cpp
 * @brief Facade translation unit for PoolLogicModule.
 *
 * Architecture: PoolLogicModule keeps a single public facade and splits its
 * implementation across Lifecycle / Scheduler / Control / Runtime / Commands
 * translation units. No definition lives here: including the header alone
 * checks that it still compiles standalone.
 *
 * Cette unite a longtemps porte, sous `#if 0`, une copie des affectations
 * `xxxVar_.moduleName = ...` presentee comme un "config-doc generation
 * anchor". Aucun generateur ne lit de .cpp (tous globbent `*ModuleDataModel.h`,
 * `*Runtime.h` et `text/*.json`), et le bloc avait derive : 16 de ses 78 lignes
 * contredisaient PoolLogicLifecycle.cpp, seul endroit ou ces affectations sont
 * reellement faites. Une de ces contradictions a envoye l'interface web lire
 * `pool_volume_m3` sur la mauvaise branche. Ne pas reintroduire de copie.
 */

#include "PoolLogicModule.h"
