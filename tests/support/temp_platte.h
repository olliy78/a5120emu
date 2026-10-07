/**
 * @file temp_platte.h
 * @brief Winchester-Abbild für einen Test (Muster `TempDisk`, Entwurf 25 §10.11): legt ein frisches
 *        LBA-Abbild des gewünschten Typs im Temp-Verzeichnis an (`Platte::neu`: E5 + gültiger
 *        PAR/BTT-Sektor Z0/K0/S1) und räumt es wieder weg.  Plattenabbilder werden NIE eingecheckt.
 *
 * Header-only; der Test linkt `k1520_winchester`.
 * @code
 *   k1520test::TempPlatte p;                       // K5504.50, 47 185 920 B
 *   k1520test::TempPlatte q("D5126", "zweite.img");
 *   k1520test::TempPlatte r = TempPlatte::leer("eigen.img");   // nur ein freier Pfad
 * @endcode
 */
#pragma once

#include "core/peripherals/winchester/platte.h"
#include "tests/support/temp_path.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace k1520test {

class TempPlatte {
public:
    explicit TempPlatte(const std::string& typ = "K5504.50", const std::string& name = "platte.img")
        : path_(tempPath("k1520_" + name))
    {
        const auto* t = k1520::winchester::typNachName(typ);
        std::string fehler;
        if (!t || !k1520::winchester::Platte::neu(path_, *t, &fehler))
            throw std::runtime_error("TempPlatte: " + (t ? fehler : "unbekannter Typ " + typ));
    }
    static TempPlatte leer(const std::string& name)
    {
        TempPlatte p{Leer{}};
        p.path_ = tempPath("k1520_" + name);
        std::error_code ec;
        std::filesystem::remove(p.path_, ec);
        return p;
    }
    ~TempPlatte()
    {
        if (path_.empty()) return;
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempPlatte(TempPlatte&& o) noexcept : path_(std::move(o.path_)) { o.path_.clear(); }
    TempPlatte(const TempPlatte&) = delete;
    TempPlatte& operator=(const TempPlatte&) = delete;

    const std::string& path() const { return path_; }
    operator const std::string&() const { return path_; }

private:
    struct Leer {};
    explicit TempPlatte(Leer) {}
    std::string path_;
};

}  // namespace k1520test
