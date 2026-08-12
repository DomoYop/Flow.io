#pragma once
/**
 * @file RunningMedianAverageFloat.h
 * @brief Running median average helper for float values.
 */

#include <stdint.h>
#include <RunningMedian.h>

class RunningMedianAverageFloat {
public:
    RunningMedianAverageFloat(uint8_t windowSize = 11, uint8_t avgCount = 5)
        : rm_(windowSize), avgCount_(avgCount) {}

    /**
     * Vide la fenetre. Appele quand la nature de ce qui est mesure change d'un
     * coup (reprise de la circulation) : sans cela les echantillons d'eau
     * immobile resteraient melanges aux premiers echantillons frais.
     */
    void clear() { rm_.clear(); }

    float update(float value) {
        rm_.add(value);
        uint8_t count = rm_.getCount();
        if (count == 0) return value;

        uint8_t n = avgCount_;
        if (n == 0) return value;
        if (n > count) n = count;

        return rm_.getAverage(n);
    }

private:
    RunningMedian rm_;
    uint8_t avgCount_ = 5;
};
