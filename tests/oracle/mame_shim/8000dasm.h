// Ersatz für MAMEs 8000dasm.h: nur die Schnittstelle, die z8000.h erwartet.
#pragma once
#include "emu.h"
class z8000_disassembler : public util::disasm_interface {
public:
    class config {
    public:
        virtual ~config() = default;
        virtual bool get_segmented_mode() const = 0;
    };
    explicit z8000_disassembler(config*) {}
};
