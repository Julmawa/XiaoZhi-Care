#include "physical_ack_button.h"
#include <cassert>
#include <iostream>

using namespace xiaozhi_care;

int main() {
    PhysicalAckButton button;
    PhysicalButtonResult r;

    // 1. Toque corto.
    button.OnPressDown(1000000, "");
    r = button.OnPressUp(1500000, "");
    assert(r.action == PhysicalButtonAction::NormalClick);
    assert(r.target_id.empty());

    // 2. Tres segundos con el mismo aviso.
    button.OnPressDown(2000000, "aviso_A");
    r = button.OnPressUp(5000000, "aviso_A");
    assert(r.action == PhysicalButtonAction::ConfirmAlert);
    assert(r.target_id == "aviso_A");

    // 3. Pulsacion larga sin aviso.
    button.OnPressDown(6000000, "");
    r = button.OnPressUp(10000000, "");
    assert(r.action == PhysicalButtonAction::None);

    // 4. Soltar sin haber presionado.
    r = button.OnPressUp(11000000, "aviso_A");
    assert(r.action == PhysicalButtonAction::None);

    // 5. Tiempo invalido.
    button.OnPressDown(15000000, "aviso_A");
    r = button.OnPressUp(14000000, "aviso_A");
    assert(r.action == PhysicalButtonAction::None);

    // 6. Cambia el aviso mientras se presiona.
    button.OnPressDown(16000000, "aviso_A");
    r = button.OnPressUp(20000000, "aviso_B");
    assert(r.action == PhysicalButtonAction::None);
    assert(r.target_id.empty());

    // 7. Desaparece el aviso.
    button.OnPressDown(21000000, "aviso_A");
    r = button.OnPressUp(25000000, "");
    assert(r.action == PhysicalButtonAction::None);

    std::cout << "7 casos C++ preparados correctamente\n";
    return 0;
}
