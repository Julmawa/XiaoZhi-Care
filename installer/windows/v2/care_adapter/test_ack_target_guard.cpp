#include "ack_target_guard.h"
#include <cassert>

using namespace xiaozhi_care;

int main() {
    // 1. El aviso A sigue pendiente.
    assert(CanConfirmPhysicalAck("A", "A", true));

    // 2. El aviso cambió de A a B.
    assert(!CanConfirmPhysicalAck("A", "B", true));

    // 3. El aviso A ya fue confirmado.
    assert(!CanConfirmPhysicalAck("A", "A", false));

    // 4. No se capturó ningún aviso.
    assert(!CanConfirmPhysicalAck("", "A", true));

    // 5. Se puede limpiar un estado de A.
    assert(BelongsToConfirmedTarget("A", "A"));

    // 6. No se puede limpiar un estado de B.
    assert(!BelongsToConfirmedTarget("B", "A"));

    // 7. Un identificador vacío no autoriza limpieza.
    assert(!BelongsToConfirmedTarget("A", ""));

    return 0;
}
