#include <unity.h>

#include "Modules/Network/MQTTModule/MqttCommandPolicy.h"

// Les commandes qui declenchent une ecriture de flash doivent etre refusees sur
// le canal MQTT : un seul message publie sur le topic cmd suffirait a lancer un
// telechargement depuis une URL arbitraire.
void test_firmware_update_commands_are_denied(void)
{
    TEST_ASSERT_TRUE(mqttCommandDenied("fw.update.waveshare"));
    TEST_ASSERT_TRUE(mqttCommandDenied("fw.update.nextion"));
    TEST_ASSERT_TRUE(mqttCommandDenied("fw.update.spiffs"));
}

// Le nom sans sous-commande est refuse aussi : si une commande `fw.update` etait
// ajoutee un jour, elle serait couverte sans modifier la politique.
void test_bare_fw_update_is_denied(void)
{
    TEST_ASSERT_TRUE(mqttCommandDenied("fw.update"));
}

// La lecture d'etat reste ouverte : Home Assistant doit pouvoir afficher la
// progression d'une mise a jour lancee depuis l'interface web.
void test_status_command_stays_allowed(void)
{
    TEST_ASSERT_FALSE(mqttCommandDenied("fw.update.status"));
}

// Hors perimetre : le redemarrage de l'ecran n'ecrit aucune flash, et les autres
// familles de commandes ne sont pas concernees.
void test_unrelated_commands_stay_allowed(void)
{
    TEST_ASSERT_FALSE(mqttCommandDenied("fw.nextion.reboot"));
    TEST_ASSERT_FALSE(mqttCommandDenied("alarms.reset_all"));
    TEST_ASSERT_FALSE(mqttCommandDenied("poollogic.filtration.write"));
    TEST_ASSERT_FALSE(mqttCommandDenied("system.reboot"));
}

// Le prefixe ne doit pas mordre sur un nom qui commence par les memes lettres
// sans etre une sous-commande.
void test_prefix_does_not_overmatch(void)
{
    TEST_ASSERT_FALSE(mqttCommandDenied("fw.updates_metrics"));
    TEST_ASSERT_FALSE(mqttCommandDenied("fw.updated"));
    TEST_ASSERT_FALSE(mqttCommandDenied("fw.upd"));
}

// Entrees degenerees : ni deni, ni dereferencement.
void test_edge_cases(void)
{
    TEST_ASSERT_FALSE(mqttCommandDenied(nullptr));
    TEST_ASSERT_FALSE(mqttCommandDenied(""));
}

// Une sous-commande inconnue est refusee par defaut : la politique liste ce qui
// est autorise, pas ce qui est interdit.
void test_unknown_subcommand_is_denied_by_default(void)
{
    TEST_ASSERT_TRUE(mqttCommandDenied("fw.update.future_target"));
    TEST_ASSERT_TRUE(mqttCommandDenied("fw.update."));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_firmware_update_commands_are_denied);
    RUN_TEST(test_bare_fw_update_is_denied);
    RUN_TEST(test_status_command_stays_allowed);
    RUN_TEST(test_unrelated_commands_stay_allowed);
    RUN_TEST(test_prefix_does_not_overmatch);
    RUN_TEST(test_edge_cases);
    RUN_TEST(test_unknown_subcommand_is_denied_by_default);
    return UNITY_END();
}
