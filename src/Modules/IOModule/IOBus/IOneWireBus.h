#pragma once
/**
 * @file IOneWireBus.h
 * @brief Common interface for 1-Wire buses (bit-bang GPIO or DS2484 I2C bridge).
 *
 * DS18B20 reading in IOModule is written against this interface so the physical
 * transport (direct GPIO OneWire vs DS2484 I2C-to-1-Wire master) can be swapped
 * without touching the temperature logic.
 */

#include <stdint.h>

class IOneWireBus {
public:
    virtual ~IOneWireBus() = default;

    /** Bring the bus up (idempotent). */
    virtual void begin() = 0;
    /** Trigger a temperature conversion on every sensor of the bus. */
    virtual void request() = 0;
    /** Blocking vs non-blocking conversion behavior. */
    virtual void setWaitForConversion(bool enabled) = 0;
    /** Copy the ROM address of the device at @p index. */
    virtual bool getAddress(uint8_t index, uint8_t out[8]) const = 0;
    /** True when @p addr is present on the bus. */
    virtual bool hasAddress(const uint8_t addr[8]) const = 0;
    /** Last converted temperature in Celsius for @p addr. */
    virtual float readC(const uint8_t addr[8]) const = 0;
    /** Number of devices discovered on the bus. */
    virtual uint8_t deviceCount() const = 0;
    /** GPIO pin for bit-bang buses, or -1 for I2C-bridged buses. */
    virtual int pin() const = 0;

    /**
     * Fait avancer la conversion partagee du bus et indique si les
     * scratchpads sont lisibles.
     *
     * `request()` est un broadcast sur tout le bus : si chaque sonde le
     * declenchait pour son compte, une sonde relancerait une conversion
     * pendant qu'une autre lit, et la lecture renverrait la valeur de mise
     * sous tension du DS18B20 (85 degC). L'arbitrage est donc porte par le
     * bus : une seule conversion en vol, et toutes les sondes lisent la meme.
     *
     * Peut etre appelee par plusieurs sondes dans le meme tick.
     *
     * @param pollMs            periode minimale entre deux conversions
     * @param conversionWaitMs  duree d'une conversion
     * @return true quand une mesure convertie est disponible
     */
    bool tickConversion(uint32_t nowMs, uint32_t pollMs, uint32_t conversionWaitMs)
    {
        if (converting_) {
            if ((uint32_t)(nowMs - conversionMs_) < conversionWaitMs) return false;
            converting_ = false;
            conversionMs_ = nowMs;
            ++conversionSeq_;
            return true;
        }
        // Mesure encore fraiche : rien a relancer, les sondes peuvent lire.
        if (conversionSeq_ != 0U && (uint32_t)(nowMs - conversionMs_) < pollMs) return true;

        request();
        converting_ = true;
        conversionMs_ = nowMs;
        return false;
    }

    /** Numero de la derniere conversion terminee (0 = aucune). */
    uint32_t conversionSeq() const { return conversionSeq_; }

private:
    uint32_t conversionMs_ = 0;
    uint32_t conversionSeq_ = 0;
    bool converting_ = false;
};
