#pragma once
// Slime - logica pura del rebote (sin dependencias de Geode, se prueba en PC).
//
// Convencion de GD: m_yVelocity es positiva hacia ARRIBA en el mundo, aunque la
// gravedad este invertida (PlayerObject::playerIsFalling lo confirma: con gravedad
// normal cae quien tiene velocidad negativa, con gravedad invertida la positiva).

#include <algorithm>
#include <cmath>
#include <optional>

namespace slime {

struct Params {
    double power = 0.90;     // fraccion de la velocidad de caida que se devuelve
    double minImpact = 3.0;  // caidas mas lentas que esto no rebotan
    double minOut = 6.0;     // velocidad minima de salida cuando rebota
    double maxOut = 22.0;    // velocidad maxima de salida
};

// Velocidad hacia el suelo (positiva = cayendo hacia el suelo, sea cual sea la gravedad).
inline double towardGround(double vy, bool upsideDown) {
    return upsideDown ? vy : -vy;
}

// De dos muestras de velocidad, la que mas cae hacia el suelo.
inline double pickImpact(double a, double b, bool upsideDown) {
    return towardGround(a, upsideDown) >= towardGround(b, upsideDown) ? a : b;
}

// Nueva velocidad vertical (positiva hacia arriba en el mundo) tras aterrizar en slime.
// std::nullopt = no hay rebote.
inline std::optional<double> bounceVelocity(double impactVy, double currentVy,
                                            bool upsideDown, Params const& p) {
    double fall = towardGround(impactVy, upsideDown);
    if (fall < p.minImpact) return std::nullopt;
    double lo = std::min(p.minOut, p.maxOut);
    double hi = std::max(p.minOut, p.maxOut);
    double away = std::clamp(fall * p.power, lo, hi); // modulo, alejandose del suelo
    double dir = upsideDown ? -1.0 : 1.0;             // signo mundial de "alejarse del suelo"
    // Si el juego ya lanzo al jugador mas alto (por ejemplo un salto con el boton apretado), se respeta.
    if (currentVy * dir >= away) return std::nullopt;
    return away * dir;
}

inline bool inGroup(short const* groups, int count, int group) {
    if (!groups || count <= 0) return false;
    for (int i = 0; i < count; ++i) {
        if (groups[i] == group) return true;
    }
    return false;
}

} // namespace slime
