#include "care_web_page.h"

namespace xiaozhi_care {

const char kCareWebPage[] = R"HTML(<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>XiaoZhi Care</title>
<style>
:root{
  --ink:#0f2147;--muted:#5f6f8f;--line:#dce8f7;--soft:#f4f9ff;--card:#ffffff;
  --blue:#2f80ed;--blue2:#61c5ff;--navy:#112c5f;--green:#16a56d;--mint:#eafaf3;
  --pink:#fff0f7;--pink2:#ff6fa7;--yellow:#fff8df;--lav:#f4efff;--shadow:0 14px 40px rgba(17,44,95,.08);
  font-family:Inter,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color:var(--ink);background:linear-gradient(180deg,#eef7ff 0,#f7fbff 38%,#f5f9ff 100%)
}
*{box-sizing:border-box}body{margin:0;padding:0;background:radial-gradient(circle at 15% -10%,#dff3ff 0,transparent 34%),radial-gradient(circle at 86% 0,#fff2dc 0,transparent 30%),var(--soft)}
button,input,textarea,select{font:inherit}button{cursor:pointer}.shell{max-width:1340px;margin:auto;padding:18px 20px 26px}.top{display:flex;gap:18px;align-items:center;justify-content:space-between;margin:0 -20px 18px;padding:16px 20px;background:rgba(255,255,255,.88);backdrop-filter:blur(14px);border-bottom:1px solid rgba(220,232,247,.9);box-shadow:0 4px 18px rgba(18,44,95,.04);position:sticky;top:0;z-index:5}.brand{display:flex;align-items:center;gap:12px}.brand:before{content:'';display:block;width:48px;height:48px;background:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Cdefs%3E%3ClinearGradient id='a' x1='7' y1='7' x2='57' y2='56' gradientUnits='userSpaceOnUse'%3E%3Cstop offset='0' stop-color='%236BD5FF'/%3E%3Cstop offset='.45' stop-color='%232F80ED'/%3E%3Cstop offset='1' stop-color='%231B5FD6'/%3E%3C/linearGradient%3E%3ClinearGradient id='b' x1='22' y1='22' x2='47' y2='51' gradientUnits='userSpaceOnUse'%3E%3Cstop offset='0' stop-color='%239AE8FF'/%3E%3Cstop offset='1' stop-color='%233A8BFF'/%3E%3C/linearGradient%3E%3C/defs%3E%3Cpath fill='url(%23a)' d='M32 56C17 44 6 35 6 21 6 12 13 6 21 6c5 0 9 2 11 6 3-4 7-6 12-6 8 0 14 6 14 15 0 14-11 23-26 35z'/%3E%3Cpath fill='url(%23b)' d='M32 51C21 42 15 36 15 28c0-6 5-11 11-11 3 0 6 2 8 5 2-3 5-5 9-5 6 0 11 5 11 11 0 8-7 14-22 23z' opacity='.95'/%3E%3Cpath fill='%23fff' d='M32 43c-7-5-10-9-10-14 0-4 3-7 7-7 2 0 4 1 5 3 1-2 3-3 6-3 4 0 7 3 7 7 0 5-4 9-15 14z' opacity='.97'/%3E%3C/svg%3E") center/contain no-repeat;filter:drop-shadow(0 10px 18px rgba(47,128,237,.24))}.brand h1{margin:0;font-size:26px;letter-spacing:-.03em;line-height:1}.brand p{margin:3px 0 0;color:var(--muted);font-size:14px}.status{font-size:13px;padding:8px 12px;border-radius:999px;background:var(--mint);color:#08744c;font-weight:800;border:1px solid #c8f0df}.topActions{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.card{background:rgba(255,255,255,.92);border:1px solid var(--line);border-radius:22px;padding:20px;box-shadow:var(--shadow)}.auth{max-width:480px;margin:80px auto}.auth h2{margin-top:0}.hidden{display:none!important}.msg{display:none;margin:0 0 14px;padding:12px 14px;border-radius:16px;background:#e8fbf2;color:#09603f;border:1px solid #c8f0df;font-weight:750}.msg.error{background:#fff0f1;color:#9b2727;border-color:#ffd4db}.field{display:flex;flex-direction:column;gap:7px;margin-bottom:13px}.field label{font-size:13px;font-weight:800;color:#3d4e70}.field input,.field textarea,.field select{width:100%;border:1px solid #cfe0f5;border-radius:14px;padding:11px 12px;background:rgba(255,255,255,.95);color:var(--ink);outline:none;transition:.15s}.field input:focus,.field textarea:focus,.field select:focus{border-color:#69c7ff;box-shadow:0 0 0 4px rgba(97,197,255,.18)}.field textarea{min-height:88px;resize:vertical}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.check{display:flex;align-items:center;gap:9px;margin:10px 0 14px;color:#405273;font-size:14px}.check input{width:18px;height:18px;accent-color:var(--blue)}.primary,.secondary,.danger{border:0;border-radius:14px;padding:10px 15px;font-weight:850;transition:.15s}.primary{background:linear-gradient(135deg,#2f80ed,#4aa3ff);color:white;box-shadow:0 8px 18px rgba(47,128,237,.22)}.primary:hover{transform:translateY(-1px)}.secondary{background:#eef6ff;color:#1a3d75;border:1px solid #d6e7fa}.danger{background:#fff1f3;color:#b42336;border:1px solid #ffd5dc}.small{padding:7px 10px;font-size:13px}.actions{display:flex;gap:8px;flex-wrap:wrap}.tabs{display:flex;gap:10px;overflow:auto;padding:4px 0 14px;margin-bottom:10px}.tab{white-space:nowrap;border:1px solid #d3e2f4;background:rgba(255,255,255,.96);color:#193966;border-radius:999px;padding:10px 17px;font-weight:850;box-shadow:0 5px 14px rgba(17,44,95,.04)}.tab.active{background:var(--navy);color:white;border-color:var(--navy);box-shadow:0 10px 22px rgba(17,44,95,.20)}.section{display:none}.section.active{display:block}.grid{display:grid;grid-template-columns:minmax(320px,390px) minmax(0,1fr);gap:18px;align-items:start}.panelTitle{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:14px}.panelTitle h2,.panelTitle h3{margin:0;letter-spacing:-.03em}.muted{color:var(--muted);font-size:13px;line-height:1.45}.list{display:flex;flex-direction:column;gap:12px}.item{border:1px solid #dceafa;border-radius:18px;padding:15px;background:linear-gradient(180deg,#ffffff,#fafdff);box-shadow:0 6px 18px rgba(17,44,95,.045)}.itemTop{display:flex;gap:10px;justify-content:space-between;align-items:flex-start}.itemName{font-weight:900;font-size:16px;letter-spacing:-.01em}.itemMeta{margin-top:5px;color:var(--muted);font-size:13px;line-height:1.35}.itemNotes{margin-top:8px;font-size:13px;color:#405273;white-space:pre-wrap;line-height:1.35}.empty{padding:30px;text-align:center;color:var(--muted);border:1px dashed #c9def6;border-radius:18px;background:#fbfdff}.notice{padding:13px 15px;border-radius:16px;background:linear-gradient(90deg,#fff8df,#fff2f8);color:#765500;font-size:13px;margin-bottom:14px;border:1px solid #ffedb5}.counter{font-size:12px;color:var(--muted);font-weight:800}.profileCard{max-width:760px}.passwordRow{display:flex;align-items:center;gap:8px;margin:4px 0 12px}.passwordRow input{width:18px;height:18px}.prefSummary{margin-top:8px;font-size:13px;color:#344054}.prefPanelHead{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.prefPanelHead h2{margin:0}.fullRow{grid-column:1/-1}.pillboxGroupTitle{font-weight:950;font-size:18px;letter-spacing:-.02em}.pillboxBadge{display:inline-block;background:#eaf4ff;border-radius:999px;padding:5px 9px;font-size:12px;font-weight:900;margin-left:7px;color:#173967}.pillboxLine{border-top:1px solid #edf3fb;margin-top:11px;padding-top:11px}.pillboxLineName{font-weight:900}.pillboxLineMeta{font-size:13px;color:var(--muted);margin-top:3px}.pillboxLineNotes{font-size:13px;color:#405273;margin-top:4px;white-space:pre-wrap}.card:nth-child(2n) .item:nth-child(3n+1){background:linear-gradient(180deg,#ffffff,#eefaff)}.card:nth-child(2n) .item:nth-child(3n+2){background:linear-gradient(180deg,#ffffff,#fff3f8)}.card:nth-child(2n) .item:nth-child(3n){background:linear-gradient(180deg,#ffffff,#f2ffe9)}.appFoot{display:flex;justify-content:space-between;gap:12px;align-items:center;color:var(--muted);font-size:12px;margin-top:24px;padding:14px 4px}.appFoot b{color:var(--navy)}
@media(max-width:900px){.shell{padding:12px}.top{margin:0 -12px 14px;padding:13px 12px;align-items:flex-start;flex-direction:column}.grid,.row{grid-template-columns:1fr}.card{padding:16px}.brand h1{font-size:24px}.brand:before{width:42px;height:42px;border-radius:14px}.appFoot{flex-direction:column;align-items:flex-start}}

#section-maintenance{
  max-width:1340px;
  margin:0 auto;
}
#section-maintenance > .grid,
#section-maintenance > .card,
#section-maintenance > .notice{
  max-width:100%;
}
#section-maintenance .grid{
  grid-template-columns:minmax(320px,390px) minmax(0,1fr);
}


#maintenanceAudioArea,
#maintenanceLowerArea{
  max-width:1340px;
  margin-left:auto!important;
  margin-right:auto!important;
}
#maintenanceAudioArea .grid,
#maintenanceLowerArea .grid{
  width:100%;
}


.checkline{display:flex;align-items:center;gap:8px;margin-top:8px;font-weight:800;color:#17345f}
.checkline input[type=checkbox]{width:auto;height:auto;margin:0;transform:scale(1.15)}
</style>
</head>
<body>
<div class="shell">
  <div class="top">
    <div class="brand"><div><h1>XiaoZhi Care</h1><p>Cuidado que conecta · DP-018-r2 · Audio automático por recordatorio · Perfil con preferencias · Logo corazón azul</p></div></div>
    <div class="topActions"><span id="status" class="status">Iniciando...</span><button id="changePasswordBtn" class="secondary hidden">Cambiar clave</button><button id="logoutBtn" class="secondary hidden">Cerrar sesión</button></div>
  </div>
  <div id="message" class="msg"></div>
  <div class="notice">DP-018-r2 sobre DP-018-r1 · El audio asignado se reproduce automáticamente cuando vence el recordatorio · Si XiaoZhi está ocupado, se reintenta durante una ventana breve.</div>

  <section id="setupView" class="card auth hidden">
    <h2>Crear contraseña de administrador</h2>
    <p class="muted">Protege el panel local. La contraseña no se guarda en texto plano.</p>
    <form id="setupForm">
      <div class="field"><label>Contraseña</label><input id="setupPassword" type="password" minlength="8" maxlength="64" required autocomplete="new-password"></div>
      <div class="field"><label>Repetir contraseña</label><input id="setupConfirm" type="password" minlength="8" maxlength="64" required autocomplete="new-password"></div>
      <label class="passwordRow"><input id="showSetupPassword" type="checkbox"> Mostrar contraseña</label>
      <button class="primary" type="submit">Crear contraseña</button>
    </form>
  </section>

  <section id="loginView" class="card auth hidden">
    <h2>Acceso de administrador</h2>
    <form id="loginForm">
      <div class="field"><label>Contraseña</label><input id="loginPassword" type="password" maxlength="64" required autocomplete="current-password"></div>
      <label class="passwordRow"><input id="showLoginPassword" type="checkbox"> Mostrar contraseña</label>
      <button class="primary" type="submit">Ingresar</button>
    </form>
  </section>

  <section id="appView" class="hidden">
    <div id="passwordPanel" class="card hidden" style="margin-bottom:14px;max-width:560px">
      <div class="panelTitle"><h3>Cambiar contraseña</h3></div>
      <form id="passwordForm">
        <div class="field"><label>Contraseña actual</label><input id="currentPassword" type="password" required maxlength="64" autocomplete="current-password"></div>
        <div class="field"><label>Nueva contraseña</label><input id="newPassword" type="password" required minlength="8" maxlength="64" autocomplete="new-password"></div>
        <div class="field"><label>Repetir nueva contraseña</label><input id="newPasswordConfirm" type="password" required minlength="8" maxlength="64" autocomplete="new-password"></div>
        <label class="passwordRow"><input id="showChangePassword" type="checkbox"> Mostrar contraseña</label>
        <div class="actions"><button class="primary" type="submit">Guardar nueva clave</button><button id="cancelPassword" class="secondary" type="button">Cancelar</button></div>
      </form>
    </div>

    <nav class="tabs" aria-label="Secciones de memoria">
      <button class="tab active" data-tab="people" type="button">Personas</button>
      <button class="tab" data-tab="profile" type="button">Perfil</button>
      <button class="tab" data-tab="routines" type="button">Cuidados</button>
      <button class="tab" data-tab="pillbox" type="button">Pastillero</button>
      <button class="tab" data-tab="reminders" type="button">Recordatorios</button>
      <button class="tab" data-tab="maintenance" type="button">Mantenimiento</button>
    </nav>

    <section id="section-people" class="section active">
      <div class="grid">
        <div class="card"><div class="panelTitle"><h2 id="personFormTitle">Agregar persona</h2></div>
          <form id="personForm"><input id="personId" type="hidden">
            <div class="field"><label>Nombre *</label><input id="personName" maxlength="64" required></div>
            <div class="row"><div class="field"><label>Apodo</label><input id="personNickname" maxlength="32"></div><div class="field"><label>Relación</label><input id="personRelationship" maxlength="32" placeholder="hija, nieto, vecina..."></div></div>
            <div class="field"><label>Teléfono</label><input id="personPhone" maxlength="32"></div>
            <div class="field"><label>Dirección</label><input id="personAddress" maxlength="128"></div>
            <div class="field"><label>Cumpleaños</label><input id="personBirthday" type="date"></div>
            <div class="field"><label>Alias separados por coma</label><input id="personAliases" maxlength="390"></div>
            <div class="field"><label>Notas privadas de administración</label><textarea id="personNotes" maxlength="256"></textarea></div>
            <label class="check"><input id="personEnabled" type="checkbox" checked> Registro activo</label>
            <div class="actions"><button class="primary" type="submit">Guardar</button><button id="personCancel" class="secondary" type="button">Cancelar</button></div>
          </form>
        </div>
        <div class="card"><div class="panelTitle"><h2>Personas guardadas</h2><span id="peopleCount" class="counter"></span></div><div id="peopleList" class="list"></div></div>
      </div>
      <div id="personPreferencesPanel" class="card hidden" style="margin-top:18px">
        <div class="prefPanelHead">
          <div><h2 id="personPreferencesTitle">Preferencias de la persona</h2><div id="personPreferencesMeta" class="muted"></div></div>
          <button id="closePersonPreferences" class="secondary" type="button">Cerrar</button>
        </div>
        <div class="grid" style="margin-top:16px">
          <div>
            <h3 id="personPreferenceFormTitle" style="margin-top:0">Agregar dato o preferencia</h3>
            <form id="personPreferenceForm"><input id="personPreferenceId" type="hidden">
              <div class="field"><label>Categoría *</label><select id="personPreferenceCategory" required>
                  <option value="pets">Mascotas</option>
                  <option value="food">Comidas</option>
                  <option value="music">Música</option>
                  <option value="hobby">Hobbies</option>
                  <option value="tv">Televisión</option>
                  <option value="habits">Costumbres</option>
                  <option value="care">Cuidados generales</option>
                  <option value="other">Otro</option>
                </select></div>
              <div class="field"><label>Dato o preferencia *</label><input id="personPreferenceValue" maxlength="96" required placeholder="Tiene un perro llamado CAYO."></div>
              <div class="field"><label>Notas</label><textarea id="personPreferenceNotes" maxlength="256"></textarea></div>
              <label class="check"><input id="personPreferenceEnabled" type="checkbox" checked> Preferencia activa</label>
              <div class="actions"><button class="primary" type="submit">Guardar</button><button id="personPreferenceCancel" class="secondary" type="button">Cancelar</button></div>
            </form>
          </div>
          <div><div class="panelTitle"><h3>Datos y preferencias guardadas</h3><span id="personPreferencesCount" class="counter"></span></div><div id="personPreferencesList" class="list"></div></div>
        </div>
      </div>
      <div class="notice" style="margin-top:18px">Relación familiar: vinculá las personas guardadas para que XiaoZhi Care entienda quién es hija, pareja, nieta, cuidador u otro vínculo.</div>
      <div class="grid">
        <div class="card"><div class="panelTitle"><h2 id="familyFormTitle">Agregar relación familiar</h2></div>
          <form id="familyForm"><input id="familyId" type="hidden">
            <div class="field"><label>Persona principal o usuaria *</label><select id="familyFrom" required></select></div>
            <div class="field"><label>Relación *</label><select id="familyType"><option value="child">es hijo/a de</option><option value="parent">es padre/madre de</option><option value="spouse">es pareja/esposo/a de</option><option value="sibling">es hermano/a de</option><option value="grandchild">es nieto/a de</option><option value="grandparent">es abuelo/a de</option><option value="in_law">es familiar político de</option><option value="caregiver">cuida o acompaña a</option><option value="other">otra relación con</option></select></div>
            <div class="field"><label>Persona relacionada o usuaria *</label><select id="familyTo" required></select></div>
            <div class="field"><label>Etiqueta visible</label><input id="familyLabel" maxlength="48" placeholder="hija, esposa, nieta, nuera..."></div>
            <div class="field"><label>Notas privadas de administración</label><textarea id="familyNotes" maxlength="256" placeholder="Ej.: Martina es hija de Ale y Aldana."></textarea></div>
            <label class="check"><input id="familyEnabled" type="checkbox" checked> Relación activa</label>
            <div class="actions"><button class="primary" type="submit">Guardar relación</button><button id="familyCancel" class="secondary" type="button">Cancelar</button></div>
          </form>
        </div>
        <div class="card"><div class="panelTitle"><h2>Relaciones familiares</h2><span id="familyCount" class="counter"></span></div><div id="familyList" class="list"></div></div>
      </div>
    </section>

    <section id="section-profile" class="section">
      <div class="notice">Perfil y preferencias de la persona usuaria. Las preferencias ayudan a XiaoZhi Care a acompañar mejor con gustos, costumbres y datos cotidianos.</div>
      <div class="grid">
        <div class="card"><div class="panelTitle"><h2>Perfil de la persona usuaria</h2><span id="profileState" class="counter"></span></div>
          <form id="profileForm">
            <div class="field"><label>Nombre *</label><input id="profileName" maxlength="64" required></div>
            <div class="row"><div class="field"><label>Apodo</label><input id="profileNickname" maxlength="32"></div><div class="field"><label>Cumpleaños</label><input id="profileBirthday" type="date"></div></div>
            <div class="field"><label>Ciudad</label><input id="profileCity" maxlength="64"></div>
            <div class="field"><label>Zona horaria</label><input id="profileTimezone" maxlength="64" placeholder="America/Argentina/Buenos_Aires"></div>
            <div class="field"><label>Notas privadas de administración</label><textarea id="profileNotes" maxlength="256"></textarea></div>
            <button class="primary" type="submit">Guardar perfil</button>
          </form>
        </div>
        <div class="card"><div class="panelTitle"><h2 id="preferencesTitle">Preferencias de la persona usuaria</h2><span id="preferencesCount" class="counter"></span></div><div id="preferencesList" class="list"></div></div>
        <div class="card fullRow"><div class="panelTitle"><h2 id="preferenceFormTitle">Agregar preferencia</h2></div>
          <form id="preferenceForm"><input id="preferenceId" type="hidden">
            <div class="row"><div class="field"><label>Categoría *</label><select id="preferenceCategory" required>
                  <option value="pets">Mascotas</option>
                  <option value="food">Comidas</option>
                  <option value="music">Música</option>
                  <option value="hobby">Hobbies</option>
                  <option value="tv">Televisión</option>
                  <option value="habits">Costumbres</option>
                  <option value="care">Cuidados generales</option>
                  <option value="other">Otro</option>
                </select></div><div class="field"><label>Preferencia *</label><input id="preferenceValue" maxlength="96" required placeholder="tango, milanesa, jardinería..."></div></div>
            <div class="field"><label>Notas</label><textarea id="preferenceNotes" maxlength="256"></textarea></div>
            <label class="check"><input id="preferenceEnabled" type="checkbox" checked> Preferencia activa</label>
            <div class="actions"><button class="primary" type="submit">Guardar preferencia</button><button id="preferenceCancel" class="secondary" type="button">Cancelar</button></div>
          </form>
        </div>
      </div>
    </section>


    <section id="section-routines" class="section">
      <div class="notice">Cuidados diarios no medicinales: hidratación, ejercicio, mediciones y otras cosas para hacer. La medicación se carga y se edita solamente desde la pestaña Pastillero.</div>
      <div class="grid">
        <div class="card">
          <div class="panelTitle"><h2 id="routineFormTitle">Agregar cuidado</h2></div>
          <form id="routineForm"><input id="routineId" type="hidden">

            <div class="field">
              <label>Título *</label>
              <input id="routineTitle" maxlength="96" required placeholder="Ej.: Tomar un vaso de agua">
            </div>

            <div class="row">
              <div class="field">
                <label>Tipo de cuidado</label>
                <select id="routineType">
                  <option value="hydration">Hidratación</option>
                  <option value="exercise">Ejercicio / caminar</option>
                  <option value="health_measurement">Medición de salud</option>
                  <option value="reminder">Cuidado / tarea</option>
                  <option value="birthday">Cumpleaños</option>
                  <option value="custom">Otro</option>
                </select>
              </div>
              <div class="field">
                <label>Estado</label>
                <select id="routineState">
                  <option value="active">Activo</option>
                  <option value="paused">Pausado</option>
                  <option value="archived">Archivado</option>
                </select>
              </div>
            </div>

            <div class="row">
              <div class="field">
                <label>Hora *</label>
                <input id="routineTime" type="time" required>
              </div>
              <div class="field">
                <label>Momento del día</label>
                <select id="routineMoment">
                  <option value="anytime">Sin indicación especial</option>
                  <option value="fasting">En ayunas</option>
                  <option value="before_breakfast">Antes del desayuno</option>
                  <option value="with_breakfast">Con el desayuno</option>
                  <option value="after_breakfast">Después del desayuno</option>
                  <option value="before_lunch">Antes del almuerzo</option>
                  <option value="with_lunch">Con el almuerzo</option>
                  <option value="after_lunch">Después del almuerzo</option>
                  <option value="before_dinner">Antes de la cena</option>
                  <option value="with_dinner">Con la cena</option>
                  <option value="after_dinner">Después de la cena</option>
                  <option value="bedtime">Antes de dormir</option>
                </select>
              </div>
            </div>

            <div class="row">
              <div class="field">
                <label>Repetición</label>
                <select id="routineRepeat">
                  <option value="daily">Todos los días</option>
                  <option value="specific_weekdays">Días específicos</option>
                  <option value="every_n_days">Cada N días</option>
                  <option value="as_needed">Según necesidad</option>
                </select>
              </div>
              <div class="field">
                <label>Empezar a recordar</label>
                <select id="routineWindow">
                  <option value="5">5 minutos antes</option>
                  <option value="15">15 minutos antes</option>
                  <option value="30" selected>30 minutos antes</option>
                  <option value="60">1 hora antes</option>
                  <option value="120">2 horas antes</option>
                  <option value="240">4 horas antes</option>
                </select>
              </div>
            </div>

            <div id="routineWeekdaysWrap" class="field hidden">
              <label>Días activos</label>
              <div class="actions">
                <label><input class="routineWeekday" type="checkbox" checked data-day="0"> Lun</label>
                <label><input class="routineWeekday" type="checkbox" checked data-day="1"> Mar</label>
                <label><input class="routineWeekday" type="checkbox" checked data-day="2"> Mié</label>
                <label><input class="routineWeekday" type="checkbox" checked data-day="3"> Jue</label>
                <label><input class="routineWeekday" type="checkbox" checked data-day="4"> Vie</label>
                <label><input class="routineWeekday" type="checkbox" checked data-day="5"> Sáb</label>
                <label><input class="routineWeekday" type="checkbox" checked data-day="6"> Dom</label>
              </div>
            </div>

            <div id="routineIntervalWrap" class="field hidden">
              <label>Cada cuántos días</label>
              <select id="routineInterval">
                <option value="1">Cada 1 día</option>
                <option value="2">Cada 2 días</option>
                <option value="3">Cada 3 días</option>
                <option value="4">Cada 4 días</option>
                <option value="5">Cada 5 días</option>
                <option value="7">Cada 7 días</option>
                <option value="14">Cada 14 días</option>
                <option value="30">Cada 30 días</option>
                <option value="60">Cada 60 días</option>
                <option value="90">Cada 90 días</option>
                <option value="180">Cada 180 días</option>
                <option value="365">Cada 365 días</option>
              </select>
            </div>

            <div class="row">
              <div class="field">
                <label>Aviso</label>
                <select id="routineAlertMode">
                  <option value="disabled">Sin aviso</option>
                  <option value="visual" selected>Solo visual</option>
                  <option value="sound">Solo sonoro</option>
                  <option value="visual_sound">Visual + sonoro</option>
                </select>
              </div>
              <div class="field">
                <label class="check"><input id="routineAlertEnabled" type="checkbox" checked> Avisos activos</label>
                <small>Desmarcá esta opción para guardar el cuidado sin avisos.</small>
              </div>
            </div>

            <div id="routineAlertVisualWrap" class="row">
              <div class="field">
                <label>Forma visual</label>
                <select id="routineAlertVisualPattern">
                  <option value="breathing">Respiración suave</option>
                  <option value="solid">Luz fija</option>
                  <option value="soft_blink">Parpadeo suave</option>
                  <option value="progress_bar">Barra de progreso</option>
                  <option value="pillbox_slot">Color por casillero</option>
                  <option value="none">Sin visual</option>
                </select>
              </div>
              <div class="field">
                <label>Color visual</label>
                <select id="routineAlertVisualColor">
                  <option value="auto">Automático</option>
                  <option value="green">Verde</option>
                  <option value="blue">Azul</option>
                  <option value="yellow">Amarillo</option>
                  <option value="soft_red">Rojo suave</option>
                  <option value="violet">Violeta</option>
                  <option value="white">Blanco</option>
                </select>
              </div>
            </div>

            <div id="routineAlertSoundWrap" class="row hidden">
              <div class="field">
                <label>Sonido</label>
                <select id="routineAlertSoundPattern">
                  <option value="soft_beep">Bip suave</option>
                  <option value="chime">Campanita</option>
                  <option value="friendly_reminder">Recordatorio amable</option>
                  <option value="progressive">Progresivo</option>
                  <option value="none">Sin sonido</option>
                </select>
              </div>
              <div class="field">
                <label class="check"><input id="routineAlertVoice" type="checkbox"> Aviso hablado</label>
                <small>XiaoZhi dice una frase corta cuando corresponde.</small>
              </div>
            </div>

            <div id="routineAlertCadenceWrap" class="row">
              <div class="field">
                <label>Repetir aviso cada</label>
                <select id="routineAlertRepeat">
                  <option value="5">5 minutos</option>
                  <option value="10" selected>10 minutos</option>
                  <option value="15">15 minutos</option>
                  <option value="30">30 minutos</option>
                  <option value="60">1 hora</option>
                  <option value="120">2 horas</option>
                </select>
              </div>
              <div class="field">
                <label>Máximo de avisos</label>
                <select id="routineAlertMax">
                  <option value="1">1 aviso</option>
                  <option value="2">2 avisos</option>
                  <option value="3" selected>3 avisos</option>
                  <option value="5">5 avisos</option>
                  <option value="10">10 avisos</option>
                </select>
              </div>
            </div>

            <div class="field">
              <label>Notas</label>
              <textarea id="routineDescription" maxlength="256" placeholder="Información útil para la familia"></textarea>
            </div>

            <input id="routinePlacementType" type="hidden" value="none">
            <input id="routineCompartment" type="hidden" value="">
            <input id="routinePlacementDescription" type="hidden" value="">

            <details style="margin:14px 0;border:1px solid #d8e5f5;border-radius:12px;padding:10px 12px;background:#f8fbff">
              <summary style="cursor:pointer;font-weight:800;color:#17345f">Opciones avanzadas</summary>
              <div style="margin-top:12px">
                <div class="row">
                  <div class="field">
                    <label>Slot BLE futuro</label>
                    <input id="routineBleSlot" type="number" min="0" max="255" value="0">
                  </div>
                  <div class="field">
                    <label>LED futuro</label>
                    <input id="routineLedNumber" type="number" min="0" max="255" value="0">
                  </div>
                </div>
                <label class="check"><input id="routineMonitored" type="checkbox"> Vinculada a hardware futuro</label>
                <div class="field">
                  <label>Datos internos opcionales</label>
                  <textarea id="routineData" maxlength="512" placeholder="Reservado para futuras versiones"></textarea>
                </div>
              </div>
            </details>

            <div class="actions">
              <button class="primary" type="submit">Guardar cuidado</button>
              <button id="routineCancel" class="secondary" type="button">Cancelar</button>
            </div>
          </form>
        </div>

        <div class="card">
          <div class="panelTitle"><h2>Cuidados guardados</h2><span id="routinesCount" class="counter"></span></div>
          <div id="routinesList" class="list"></div>
        </div>
      </div>
    </section>

    <section id="section-pillbox" class="section">
      <div class="notice">Pastillero es la sección central para medicación. Muestra qué va en cada casillero y cuántas pastillas debe tener. Los datos médicos son visibles para administración familiar; la voz de XiaoZhi mantiene mensajes seguros.</div>
      <div class="grid">
        <div class="card"><div class="panelTitle"><h2 id="pillboxFormTitle">Agregar medicación al pastillero</h2></div>
          <form id="pillboxForm"><input id="pillboxId" type="hidden">
            <div class="field"><label>Nombre o referencia del medicamento *</label><input id="pillboxTitle" maxlength="96" required placeholder="Ej.: T4 Levotiroxina sódica 50mg"></div>
            <div class="row"><div class="field"><label>Hora *</label><input id="pillboxTime" type="time" required></div><div class="field"><label>Momento</label><select id="pillboxMoment"><option value="anytime">En cualquier momento</option><option value="fasting">En ayunas</option><option value="before_breakfast">Antes del desayuno</option><option value="with_breakfast">Con el desayuno</option><option value="after_breakfast">Después del desayuno</option><option value="before_lunch">Antes del almuerzo</option><option value="with_lunch">Con el almuerzo</option><option value="after_lunch">Después del almuerzo</option><option value="before_dinner">Antes de la cena</option><option value="with_dinner">Con la cena</option><option value="after_dinner">Después de la cena</option><option value="bedtime">Antes de dormir</option></select></div></div>
            <div class="row"><div class="field"><label>Estado</label><select id="pillboxState"><option value="active">Activa</option><option value="paused">Pausada</option><option value="archived">Archivada</option></select></div><div class="field"><label>Ventana de aviso, minutos</label><input id="pillboxWindow" type="number" min="1" max="240" value="30"></div></div>
            <div class="row"><div class="field"><label class="check"><input id="pillboxAlertEnabled" type="checkbox" checked> Esta medicación necesita aviso</label></div><div class="field"><label>Tipo de aviso</label><select id="pillboxAlertMode"><option value="disabled">Sin aviso</option><option value="visual">Solo visual</option><option value="sound">Solo sonoro</option><option value="visual_sound">Visual + sonoro</option></select></div></div>
            <div class="row"><div class="field"><label>Forma visual</label><select id="pillboxAlertVisualPattern"><option value="pillbox_slot">Color por casillero</option><option value="breathing">Respiración suave</option><option value="solid">Luz fija</option><option value="soft_blink">Parpadeo suave</option><option value="progress_bar">Barra de progreso</option><option value="none">Sin visual</option></select></div><div class="field"><label>Color visual</label><select id="pillboxAlertVisualColor"><option value="auto">Automático</option><option value="yellow">Amarillo</option><option value="green">Verde</option><option value="blue">Azul</option><option value="violet">Violeta</option><option value="soft_red">Rojo suave</option><option value="white">Blanco</option></select></div></div>
            <div class="row"><div class="field"><label>Sonido</label><select id="pillboxAlertSoundPattern"><option value="soft_beep">Bip suave</option><option value="chime">Campanita</option><option value="friendly_reminder">Recordatorio amable</option><option value="progressive">Progresivo</option><option value="none">Sin sonido</option></select></div><div class="field"><label class="check"><input id="pillboxAlertVoice" type="checkbox" checked> Aviso hablado seguro</label><small>No menciona medicamentos ni dosis; solo recuerda el casillero o lo pendiente.</small></div></div><div class="row"><div class="field"><label>Repetir aviso cada, minutos</label><input id="pillboxAlertRepeat" type="number" min="1" max="240" value="10"></div></div>
            <div class="row"><div class="field"><label>Máximo de avisos</label><input id="pillboxAlertMax" type="number" min="1" max="10" value="3"></div></div>
            <div class="field"><label>Días activos</label><div class="actions"><label><input class="pillboxWeekday" type="checkbox" checked data-day="0"> Lun</label><label><input class="pillboxWeekday" type="checkbox" checked data-day="1"> Mar</label><label><input class="pillboxWeekday" type="checkbox" checked data-day="2"> Mié</label><label><input class="pillboxWeekday" type="checkbox" checked data-day="3"> Jue</label><label><input class="pillboxWeekday" type="checkbox" checked data-day="4"> Vie</label><label><input class="pillboxWeekday" type="checkbox" checked data-day="5"> Sáb</label><label><input class="pillboxWeekday" type="checkbox" checked data-day="6"> Dom</label></div></div>
            <div class="field"><label>Casillero *</label><select id="pillboxCompartment" required><option value="Primero">Primero</option><option value="Segundo">Segundo</option><option value="Tercero">Tercero</option></select></div>
            <div class="row"><div class="field"><label>Cantidad de pastillas *</label><input id="pillboxQuantity" type="number" min="1" max="20" value="1" required></div><div class="field"><label>Descripción visual</label><input id="pillboxVisualDescription" maxlength="160" placeholder="1 pastilla blanca chica, 1 celeste..."></div></div>
            <div class="field"><label>Dónde está el pastillero</label><input id="pillboxPlacementDescription" maxlength="128" placeholder="Ej.: cocina, al lado de la cafetera"></div>
            <div class="field"><label>Descripción privada / dosis / referencia familiar</label><textarea id="pillboxDescription" maxlength="256" placeholder="Dato visible solo en consola administrativa"></textarea></div>
            <div class="field"><label>Datos internos opcionales</label><textarea id="pillboxData" maxlength="512" placeholder="Notas internas de carga o referencia familiar"></textarea></div>
            <div class="actions"><button class="primary" type="submit">Guardar en pastillero</button><button id="pillboxCancel" class="secondary" type="button">Cancelar</button></div>
          </form>
        </div>
        <div class="card"><div class="panelTitle"><h2>Pastillero agrupado</h2><span id="pillboxCount" class="counter"></span></div><div id="pillboxList" class="list"></div></div>
      </div>
    </section>

    <section id="section-reminders" class="section">
      <div class="grid">
        <div class="card"><div class="panelTitle"><h2 id="reminderFormTitle">Agregar recordatorio</h2></div>
          <form id="reminderForm"><input id="reminderId" type="hidden">
            <div class="field"><label>Título *</label><input id="reminderTitle" maxlength="96" required></div>
            <div class="field"><label>Tipo de recordatorio *</label><select id="reminderMode"><option value="date">Fecha exacta</option><option value="weekly">Día semanal</option></select></div>
            <div class="row"><div class="field" id="reminderDateBox"><label>Fecha *</label><input id="reminderDate" type="date"></div><div class="field"><label>Hora *</label><input id="reminderTime" type="time" required></div></div>
            <div class="field" id="reminderWeekdayBox" style="display:none"><label>Día de la semana *</label><select id="reminderWeekday"><option value="">Seleccionar día</option><option value="0">Todos los días</option><option value="1">Lunes</option><option value="2">Martes</option><option value="3">Miércoles</option><option value="4">Jueves</option><option value="5">Viernes</option><option value="6">Sábado</option><option value="7">Domingo</option></select><small>Si elegís día semanal, la fecha queda deshabilitada.</small></div>
            <div class="field"><label>Empezar a recordar</label><select id="reminderRememberBefore"><option value="0">En el momento</option><option value="60">1 hora antes</option><option value="360">6 horas antes</option><option value="1440">1 día antes</option><option value="2880">2 días antes</option><option value="10080">1 semana antes</option></select><small>Define desde cuándo CARA debe tener presente este evento.</small></div>
            <div class="field"><label>Repetición</label><select id="reminderRecurrence"><option value="none">No repetir</option><option value="daily">Diaria</option><option value="weekly">Todas las semanas</option><option value="monthly">Mensual</option><option value="yearly">Anual</option></select></div>
            <div class="field"><label>Persona relacionada</label><select id="reminderPerson"><option value="">Sin persona vinculada</option></select></div>
            <div class="field"><label>Notas</label><textarea id="reminderNotes" maxlength="256"></textarea></div>
            <label class="check"><input id="reminderEnabled" type="checkbox" checked> Recordatorio activo</label>
            <div class="actions"><button class="primary" type="submit">Guardar</button><button id="reminderCancel" class="secondary" type="button">Cancelar</button></div>
          </form>
        </div>
        <div class="card"><div class="panelTitle"><h2>Recordatorios</h2><span id="remindersCount" class="counter"></span></div><div id="remindersList" class="list"></div></div>
      </div>
    </section>


    <section id="section-maintenance" class="section">
      <div class="notice">Herramientas de mantenimiento para pruebas. No borra WiFi ni configuración de XiaoZhi. Usar con cuidado.</div>
      <div class="grid">
        <div class="card">
          <div class="panelTitle"><h2>Estado de memoria</h2></div>
          <p class="muted">Muestra uso de registros Care y estado general de NVS.</p>
          <div class="actions"><button id="maintenanceRefresh" class="primary" type="button">Actualizar estado</button></div>
          <div id="maintenanceNvs" class="itemNotes" style="margin-top:14px"></div>
        </div>
        <div class="card">
          <div class="panelTitle"><h2>Uso de XiaoZhi Care</h2><span id="maintenanceCount" class="counter"></span></div>
          <div class="item">
            <div class="itemName" id="preparedAudioSummary">AUDIOS DISPONIBLES: 0</div>
          </div>
          <div id="maintenanceList" class="list"></div>
        </div>
        <div class="card">
          <div class="panelTitle"><h2>Backup completo</h2><span class="counter">Seguro</span></div>
          <p class="muted">Descarga o restaura un archivo JSON con perfil, personas, preferencias, familia, recordatorios, pastillero y cuidados. No incluye audios.</p>
          <div class="actions">
            <button id="fullBackupDownload" class="primary" type="button">Descargar backup completo</button>
          </div>
          <div style="margin-top:14px"></div>
          <input id="fullBackupFile" type="file" accept=".json,application/json">
          <div class="actions" style="margin-top:10px">
            <button id="fullBackupValidate" class="secondary" type="button">Validar backup</button>
            <button id="fullBackupRestore" class="danger" type="button">Restaurar backup</button>
          </div>
          <p class="muted">La restauración reemplaza los datos actuales de Care por los del archivo. No restaura audios, WiFi ni configuración de XiaoZhi.</p>
          <div id="fullBackupStatus" class="itemNotes" style="margin-top:14px"></div>
        </div>
      </div>
      <div style="margin-top:18px"></div>
      <div class="notice">Audios de recordatorios y pastillero: cada archivo queda vinculado a un recordatorio o casillero. Podés escucharlo en este navegador antes de cargarlo; todavía no se modifica el reproductor interno de XiaoZhi.</div>
<input id="preparedAudioFolder" type="file" accept=".ogg,audio/ogg" webkitdirectory multiple style="display:none">

      <div id="maintenanceAudioArea" style="max-width:1340px;margin:0 auto;">
      <div class="grid">
        <div class="card">
          <div class="panelTitle"><h2>Agregar audio</h2><span id="recordingsCount" class="counter"></span></div>
          <form id="recordingForm">
            <div class="field"><label>Nombre del audio *</label><input id="recordingLabel" maxlength="48" required placeholder="Ej.: Recordatorio del almuerzo"></div>
            <div class="field"><label>Recordatorio *</label><select id="recordingReminder" required><option value="">Seleccionar recordatorio</option></select></div>
            <div class="field"><label>Descripción opcional</label><input id="recordingText" maxlength="120" placeholder="Ej.: Mensaje grabado por la familia"></div>
            <div class="field"><label>Archivo OGG / Opus *</label><input id="recordingFile" type="file" accept=".ogg,audio/ogg"></div>
            <p class="muted">Límite inicial: 32 KB y 10 segundos por audio. Máximo 12 audios.</p>
            <div class="actions"><button id="recordingSaveBtn" class="primary" type="submit">Cargar y asignar</button></div>
          </form>
        </div>
        <div class="card">
          <div class="panelTitle"><h2>Audios asignados</h2><span class="counter">Mantenimiento</span></div>
          <p class="muted">Podés cambiar la asignación o desasignar un audio. Si eliminás un audio asignado, primero se desasigna automáticamente.</p>
          <div id="recordingsList" class="list"></div>
        </div>
      </div>
      </div>
      <div id="maintenanceLowerArea" style="max-width:1340px;margin:0 auto;">
      <div class="card" style="margin-top:18px">
        <div class="panelTitle"><h2>Limpieza segura</h2></div>
        <p class="muted">Estas acciones son para liberar espacio durante las pruebas. No modifican personas, familia, preferencias, cuidados ni recordatorios actuales.</p>
        <div class="actions">
          <button id="clearExecutionsBtn" class="danger" type="button">Borrar historial de cosas hechas</button>
          <button id="clearLegacyPillboxBtn" class="danger" type="button">Borrar pastillero legacy</button>
          <button id="cleanDuplicatesBtn" class="secondary" type="button">Limpiar duplicados</button>
        </div>
      </div>
      <div style="margin-top:18px"></div>
            </div>
    </section>
  <div class="appFoot"><span><b>XiaoZhi Care</b> · Cuidado que conecta</span><span>Un día más de bienestar, juntos corazón</span></div>
</div>
<script>
const $=id=>document.getElementById(id);let csrf='';let authEnabled=true;let profile={},people=[],preferences=[],pillbox=[],reminders=[],routines=[],family=[],maintenance={},recordings=[],recordingLimits={};
const errorText={INVALID_ARGUMENT:'Datos inválidos.',INVALID_ID:'Identificador inválido.',INVALID_DATE:'Fecha inválida.',INVALID_TIME:'Hora inválida.',TOO_LONG:'Uno de los campos es demasiado largo.',TOO_LARGE:'El registro es demasiado grande.',LIMIT_REACHED:'Se alcanzó el límite de registros.',NOT_FOUND:'Registro no encontrado.',IN_USE:'No se puede eliminar: la persona está vinculada a una preferencia o recordatorio.',STORAGE_ERROR:'Error de almacenamiento.',STORAGE_FULL:'No queda espacio suficiente en NVS.',NOT_INITIALIZED:'XiaoZhi Care no está listo.',UNSUPPORTED_VERSION:'Versión de datos no compatible.',AUTH_REQUIRED:'Sesión requerida.',AUTH_FAILED:'Contraseña incorrecta.',AUTH_LOCKED:'Demasiados intentos. Esperá un minuto.',CSRF_FAILED:'La sesión de seguridad cambió. Volvé a ingresar.',VOICE_STORAGE_NOT_READY:'El almacenamiento de audios no está disponible.',VOICE_UPLOAD_TOO_LARGE:'El archivo de audio es demasiado grande.',INVALID_VOICE_RECORDING:'Faltan datos del audio o del recordatorio.',INVALID_BASE64:'No se pudo leer el archivo de audio.',REMINDER_NOT_FOUND:'El recordatorio seleccionado ya no existe.',REMINDER_ALREADY_HAS_AUDIO:'Ese recordatorio ya tiene un audio asignado.',VOICE_LIMIT_REACHED:'Se alcanzó el máximo de audios.',AUDIO_ASSIGNED_TO_REMINDER:'Primero desasigná el audio del recordatorio.',VOICE_INDEX_WRITE_FAILED:'No se pudo guardar el índice de audios.'};
function readableError(x){return errorText[x]||x||'Error desconocido'}
function showMsg(text,type='ok'){
  const e=$('message');
  e.textContent=text;
  e.className='msg'+(type==='error'?' error':'');
  e.style.display='block';
  setTimeout(()=>{e.style.display='none'},4500);

  let toast=$('careToast');
  if(!toast){
    toast=document.createElement('div');
    toast.id='careToast';
    toast.style.position='fixed';
    toast.style.left='50%';
    toast.style.bottom='28px';
    toast.style.transform='translateX(-50%)';
    toast.style.zIndex='9999';
    toast.style.padding='14px 22px';
    toast.style.borderRadius='18px';
    toast.style.fontWeight='900';
    toast.style.boxShadow='0 12px 34px rgba(17,44,95,.22)';
    toast.style.border='1px solid #c8f0df';
    toast.style.background='#e8fbf2';
    toast.style.color='#09603f';
    toast.style.display='none';
    document.body.appendChild(toast);
  }

  toast.textContent=type==='error'?text:'Datos guardados';
  toast.style.background=type==='error'?'#fff0f1':'#e8fbf2';
  toast.style.color=type==='error'?'#9b2727':'#09603f';
  toast.style.borderColor=type==='error'?'#ffd4db':'#c8f0df';
  toast.style.display='block';

  clearTimeout(window.__careToastTimer);
  window.__careToastTimer=setTimeout(()=>{
    toast.style.display='none';
  },2000);
}
function showRoutineSaveConfirmation(created,title){const clean=(title||'esta rutina').trim()||'esta rutina';showMsg(created?'OK Listo, dejé anotado este cuidado: '+clean:'OK Listo, guardé los cambios de este cuidado: '+clean)}
function showPillboxSaveConfirmation(created,title){const clean=(title||'este casillero').trim()||'este casillero';showMsg(created?'OK Listo, agregué esto al pastillero: '+clean:'OK Listo, guardé los cambios del pastillero: '+clean)}
async function api(url,{method='GET',body=null,csrfRequired=false,redirectOn401=true}={}){const headers={};if(body!==null)headers['Content-Type']='application/json';if(csrfRequired&&csrf)headers['X-Care-CSRF']=csrf;const r=await fetch(url,{method,headers,body:body===null?undefined:JSON.stringify(body),credentials:'same-origin'});let j={};try{j=await r.json()}catch(e){}if(!r.ok||j.success===false){const code=j.error||('HTTP '+r.status);if(r.status===401&&redirectOn401){csrf='';showLogin()}throw new Error(code)}return j}
function setPasswordVisibility(ids,visible){for(const id of ids)$(id).type=visible?'text':'password'}
function resetPasswordVisibility(){for(const id of ['showSetupPassword','showLoginPassword','showChangePassword'])$(id).checked=false;setPasswordVisibility(['setupPassword','setupConfirm','loginPassword','currentPassword','newPassword','newPasswordConfirm'],false)}
function hideAll(){for(const id of ['setupView','loginView','appView'])$(id).classList.add('hidden');$('changePasswordBtn').classList.add('hidden');$('logoutBtn').classList.add('hidden');$('passwordPanel').classList.add('hidden')}
function showSetup(){resetPasswordVisibility();hideAll();$('setupView').classList.remove('hidden');$('status').textContent='Configurar seguridad';setTimeout(()=>$('setupPassword').focus(),0)}
function showLogin(){resetPasswordVisibility();hideAll();$('loginView').classList.remove('hidden');$('status').textContent='Bloqueado';$('loginPassword').value='';setTimeout(()=>$('loginPassword').focus(),0)}
async function showApp(){hideAll();$('appView').classList.remove('hidden');if(authEnabled){$('changePasswordBtn').classList.remove('hidden');$('logoutBtn').classList.remove('hidden')}$('status').textContent=authEnabled?'Care listo':'Care listo · DEV sin contraseña';await loadPeople();await loadProfile();await Promise.all([loadPreferences(),loadRoutines(),loadPillbox(),loadReminders(),loadFamily(),loadMaintenance(),loadRecordings()]);setupPeopleFamilyUi();updatePersonFamilyOptions()}
async function refreshAuth(){try{const j=await api('/api/auth/state',{redirectOn401:false});const s=j.data||{};authEnabled=s.authentication!==false;if(!s.configured){csrf='';showSetup();return}if(!s.authenticated){csrf='';showLogin();return}csrf=s.csrf||'';await showApp()}catch(e){hideAll();$('status').textContent='Sin conexión';showMsg('No se pudo consultar XiaoZhi Care: '+readableError(e.message),'error')}}
$('showSetupPassword').addEventListener('change',e=>setPasswordVisibility(['setupPassword','setupConfirm'],e.target.checked));$('showLoginPassword').addEventListener('change',e=>setPasswordVisibility(['loginPassword'],e.target.checked));$('showChangePassword').addEventListener('change',e=>setPasswordVisibility(['currentPassword','newPassword','newPasswordConfirm'],e.target.checked));
$('setupForm').addEventListener('submit',async e=>{e.preventDefault();const p=$('setupPassword').value,c=$('setupConfirm').value;if(p!==c)return showMsg('Las contraseñas no coinciden.','error');if(p.length<8)return showMsg('La contraseña debe tener al menos 8 caracteres.','error');try{const j=await api('/api/auth/setup',{method:'POST',body:{password:p},redirectOn401:false});$('setupForm').reset();csrf=(j.data&&j.data.csrf)||'';showMsg('Contraseña creada.');await showApp()}catch(err){showMsg(readableError(err.message),'error')}});
$('loginForm').addEventListener('submit',async e=>{e.preventDefault();try{const j=await api('/api/auth/login',{method:'POST',body:{password:$('loginPassword').value},redirectOn401:false});$('loginForm').reset();csrf=(j.data&&j.data.csrf)||'';showMsg('Sesión iniciada.');await showApp()}catch(err){showMsg(readableError(err.message),'error')}});
$('logoutBtn').onclick=async()=>{try{await api('/api/auth/logout',{method:'POST',body:{},csrfRequired:true,redirectOn401:false})}catch(e){}csrf='';showLogin();showMsg('Sesión cerrada.')};
$('changePasswordBtn').onclick=()=>{$('passwordPanel').classList.toggle('hidden');if(!$('passwordPanel').classList.contains('hidden'))$('currentPassword').focus()};$('cancelPassword').onclick=()=>{$('passwordForm').reset();$('showChangePassword').checked=false;setPasswordVisibility(['currentPassword','newPassword','newPasswordConfirm'],false);$('passwordPanel').classList.add('hidden')};
$('passwordForm').addEventListener('submit',async e=>{e.preventDefault();const cur=$('currentPassword').value,n=$('newPassword').value,c=$('newPasswordConfirm').value;if(n!==c)return showMsg('Las nuevas contraseñas no coinciden.','error');if(n.length<8)return showMsg('La nueva contraseña debe tener al menos 8 caracteres.','error');try{const j=await api('/api/auth/password',{method:'PUT',body:{current_password:cur,new_password:n},csrfRequired:true});csrf=(j.data&&j.data.csrf)||'';$('passwordForm').reset();$('passwordPanel').classList.add('hidden');showMsg('Contraseña actualizada.')}catch(err){showMsg(readableError(err.message),'error')}});
for(const b of document.querySelectorAll('.tab'))b.addEventListener('click',()=>{for(const x of document.querySelectorAll('.tab'))x.classList.toggle('active',x===b);for(const s of document.querySelectorAll('.section'))s.classList.toggle('active',s.id==='section-'+b.dataset.tab)});
function actionButton(text,kind,fn){const b=document.createElement('button');b.type='button';b.className=kind+' small';b.textContent=text;b.addEventListener('click',fn);return b}
function empty(container,text){container.textContent='';const e=document.createElement('div');e.className='empty';e.textContent=text;container.appendChild(e)}
function itemCard(title,meta,notes,onEdit,onDelete){const c=document.createElement('div');c.className='item';const top=document.createElement('div');top.className='itemTop';const info=document.createElement('div');const n=document.createElement('div');n.className='itemName';n.textContent=title;const m=document.createElement('div');m.className='itemMeta';m.textContent=meta;info.append(n,m);const acts=document.createElement('div');acts.className='actions';acts.append(actionButton('Editar','secondary',onEdit),actionButton('Eliminar','danger',onDelete));top.append(info,acts);c.appendChild(top);if(notes){const q=document.createElement('div');q.className='itemNotes';q.textContent=notes;c.appendChild(q)}return c}
function humanNumber(list,id,prefix){const i=list.findIndex(x=>x.id===id);return prefix+' '+(i>=0?i+1:'?')}
function personNumber(id){return humanNumber(people,id,'Persona')}
function personTitle(p){return personNumber(p.id)+' · '+(p.name||'Sin nombre')+(p.nickname?' · '+p.nickname:'')}
function preferenceNumber(id,list){return humanNumber(list||preferences,id,'Preferencia')}
function normalizePreferenceCategoryForSelect(v){
  const x=String(v||'').trim().toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g,'');
  const map={mascota:'pets',mascotas:'pets',pets:'pets',pet:'pets',comida:'food',comidas:'food',food:'food',musica:'music',music:'music',hobby:'hobby',hobbies:'hobby',pasatiempo:'hobby',pasatiempos:'hobby',television:'tv',tv:'tv',costumbre:'habits',costumbres:'habits',habits:'habits',cuidado:'care',cuidados:'care',care:'care',otro:'other',otros:'other',other:'other'};
  return map[x]||'other';
}
function preferenceCategoryLabel(v){
  const x=normalizePreferenceCategoryForSelect(v);
  return {pets:'Mascotas',food:'Comidas',music:'Música',hobby:'Hobbies',tv:'Televisión',habits:'Costumbres',care:'Cuidados generales',other:'Otro'}[x]||'Otro';
}
function routineNumber(id){return humanNumber(routines,id,'Cuidado')}
function pillboxNumber(id){return humanNumber(pillbox,id,'Medicación')}
function normalizeCompartmentName(name){return (name||'Sin casillero').trim()||'Sin casillero'}
function safeInt(v,def=1){const n=parseInt(v,10);return Number.isFinite(n)&&n>0?n:def}
function routineExtra(r){try{const d=JSON.parse(r.data||'{}');return d&&typeof d==='object'?d:{private_notes:r.data||''}}catch(e){return{private_notes:r.data||''}}}
function routineQuantity(r){const d=routineExtra(r);return safeInt(d.quantity,1)}
function routineVisual(r){const d=routineExtra(r);return (d.visual_description||d.visual||'').trim()}
function reminderNumber(id){return humanNumber(reminders,id,'Recordatorio')}
function familyNumber(id){return humanNumber(family,id,'Relación')}
const PROFILE_ID='profile';
let editingPersonId='';
function profileDisplayName(){return (profile.nickname||profile.name||'Yolita').trim()||'Yolita'}
function profileOptionText(){return 'Usuaria principal · '+profileDisplayName()}
function familyParticipantOptions(){const opts=[{id:PROFILE_ID,text:profileOptionText()}];for(const p of people)opts.push({id:p.id,text:personTitle(p)});return opts}
function personNameById(id){if(id===PROFILE_ID)return profileDisplayName();const p=people.find(x=>x.id===id);return p?(p.name+(p.nickname?' · '+p.nickname:'')):'Persona no encontrada'}

// U2-r6 - Familia y vinculos dentro de Personas
function setupPeopleFamilyUi(){
  try{
    document.querySelectorAll('button,.tab,a').forEach(el=>{
      const t=(el.textContent||'').trim().toLowerCase();
      if(t==='relaciones familiares'||t==='familia'){
        el.style.display='none';
        el.setAttribute('data-care-hidden-family-tab','1');
      }
    });

    document.querySelectorAll('.notice').forEach(el=>{
      const t=(el.textContent||'').trim().toLowerCase();
      if(t.startsWith('relación familiar:')||t.startsWith('relacion familiar:')){
        el.style.display='none';
      }
    });

    const familyForm=$('familyForm');
    if(familyForm){
      const oldFamilyCard=familyForm.closest('.card');
      if(oldFamilyCard)oldFamilyCard.style.display='none';
    }

    const familyList=$('familyList');
    if(familyList){
      const oldFamilyListCard=familyList.closest('.card');
      if(oldFamilyListCard)oldFamilyListCard.style.display='none';
    }

    const familyCount=$('familyCount');
    if(familyCount){
      const title=familyCount.closest('.card');
      if(title)title.style.display='none';
    }

    if($('personFamilyBox'))return;

    const enabled=$('personEnabled');
    const anchor=enabled?enabled.closest('label'):null;
    const form=$('personForm');
    if(!form||!anchor)return;

    const box=document.createElement('div');
    box.id='personFamilyBox';
    box.className='card';
    box.style.margin='14px 0';
    box.innerHTML=
      '<div class="panelTitle"><h3>Vínculos adicionales</h3></div>'+
      '<p class="muted">Carga hasta tres vínculos adicionales con otras personas. La relación directa con Yoli ya se carga arriba en el campo Relación.</p>'+
      '<div class="row">'+
        '<div class="field"><label>Es</label><select id="personFamilyType"><option value="">Sin vínculo</option><option value="child">hijo/a</option><option value="parent">madre/padre</option><option value="spouse">esposa/esposo</option><option value="partner">pareja</option><option value="sibling">hermano/a</option><option value="grandchild">nieto/a</option><option value="grandparent">abuelo/a</option><option value="aunt_uncle">tía/tío</option><option value="niece_nephew">sobrino/a</option><option value="daughter_in_law">nuera</option><option value="son_in_law">yerno</option><option value="in_law">familiar político</option><option value="caregiver">cuidador/a</option><option value="friend">amigo/a</option><option value="neighbor">vecino/a</option><option value="other">otro vínculo</option></select></div>'+
        '<div class="field"><label>de</label><select id="personFamilyRelated"><option value="">Seleccionar persona</option></select></div>'+
      '</div>'+
      '<div class="row">'+
        '<div class="field"><label>También es</label><select id="personFamilyType2"><option value="">Sin vínculo</option><option value="child">hijo/a</option><option value="parent">madre/padre</option><option value="spouse">esposa/esposo</option><option value="partner">pareja</option><option value="sibling">hermano/a</option><option value="grandchild">nieto/a</option><option value="grandparent">abuelo/a</option><option value="aunt_uncle">tía/tío</option><option value="niece_nephew">sobrino/a</option><option value="daughter_in_law">nuera</option><option value="son_in_law">yerno</option><option value="in_law">familiar político</option><option value="caregiver">cuidador/a</option><option value="friend">amigo/a</option><option value="neighbor">vecino/a</option><option value="other">otro vínculo</option></select></div>'+
        '<div class="field"><label>de</label><select id="personFamilyRelated2"><option value="">Seleccionar persona</option></select></div>'+
      '</div>'+
      '<div class="row">'+
        '<div class="field"><label>También es</label><select id="personFamilyType3"><option value="">Sin vínculo</option><option value="child">hijo/a</option><option value="parent">madre/padre</option><option value="spouse">esposa/esposo</option><option value="partner">pareja</option><option value="sibling">hermano/a</option><option value="grandchild">nieto/a</option><option value="grandparent">abuelo/a</option><option value="aunt_uncle">tía/tío</option><option value="niece_nephew">sobrino/a</option><option value="daughter_in_law">nuera</option><option value="son_in_law">yerno</option><option value="in_law">familiar político</option><option value="caregiver">cuidador/a</option><option value="friend">amigo/a</option><option value="neighbor">vecino/a</option><option value="other">otro vínculo</option></select></div>'+
        '<div class="field"><label>de</label><select id="personFamilyRelated3"><option value="">Seleccionar persona</option></select></div>'+
      '</div>'+
      '<p class="muted">Ejemplo: Marcelo ya figura como hijo de Yoli arriba; acá cargá solo pareja de Miriam y hermano de Alejandra.</p>';

    form.insertBefore(box,anchor);
    updatePersonFamilyOptions();
  }catch(e){}
}

function personFamilyOptionText(id){
  if(id===PROFILE_ID)return profileOptionText();
  return personNameById(id);
}

function updatePersonFamilyOptions(currentId=''){
  const ids=['personFamilyRelated','personFamilyRelated2','personFamilyRelated3'];

  // U2-r6-r5:
  // En estos vinculos NO se incluye la usuaria principal.
  // La relacion con Yoli se carga arriba en el campo "Relacion".
  const opts=(people||[])
    .filter(p=>p.enabled!==false)
    .filter(p=>p.id!==currentId)
    .map(p=>({id:p.id,text:personNameById(p.id)}));

  for(const id of ids){
    const el=$(id);
    if(!el)continue;

    const cur=el.value;
    el.textContent='';

    const empty=document.createElement('option');
    empty.value='';
    empty.textContent='Seleccionar persona';
    el.appendChild(empty);

    for(const opt of opts){
      const o=document.createElement('option');
      o.value=opt.id;
      o.textContent=opt.text;
      el.appendChild(o);
    }

    if([...el.options].some(o=>o.value===cur)){
      el.value=cur;
    }
  }
}

function clearPersonFamilyFields(){
  ['personFamilyType','personFamilyType2','personFamilyType3','personFamilyRelated','personFamilyRelated2','personFamilyRelated3'].forEach(id=>{
    const el=$(id);
    if(el)el.value='';
  });
}

function normalizeUiFamilyType(type){
  if(type==='partner')return 'spouse';
  if(type==='aunt_uncle'||type==='niece_nephew'||type==='daughter_in_law'||type==='son_in_law'||type==='friend'||type==='neighbor')return 'other';
  return type||'other';
}

function familyTypeLabelForUi(type){
  return {
    child:'hijo/a',
    parent:'madre/padre',
    spouse:'esposa/esposo',
    partner:'pareja',
    sibling:'hermano/a',
    grandchild:'nieto/a',
    grandparent:'abuelo/a',
    aunt_uncle:'tía/tío',
    niece_nephew:'sobrino/a',
    daughter_in_law:'nuera',
    son_in_law:'yerno',
    in_law:'familiar político',
    caregiver:'cuidador/a',
    friend:'amigo/a',
    neighbor:'vecino/a',
    other:'otro vínculo'
  }[type]||'vínculo';
}

function fillPersonFamilyFields(personId){
  updatePersonFamilyOptions(personId);
  clearPersonFamilyFields();
  if(!personId)return;

  const rels=(family||[]).filter(x=>
    x.enabled!==false &&
    (x.from_person_id||'')===personId &&
    (x.to_person_id||'')
  ).slice(0,3);

  rels.forEach((r,idx)=>{
    const suffix=idx===0?'':String(idx+1);
    const typeEl=$('personFamilyType'+suffix);
    const relatedEl=$('personFamilyRelated'+suffix);
    if(!typeEl||!relatedEl)return;

    relatedEl.value=r.to_person_id||'';

    const label=(r.label||'').toLowerCase();
    if(label.includes('pareja'))typeEl.value='partner';
    else if(label.includes('tía')||label.includes('tio')||label.includes('tío'))typeEl.value='aunt_uncle';
    else if(label.includes('sobrina')||label.includes('sobrino'))typeEl.value='niece_nephew';
    else if(label.includes('nuera'))typeEl.value='daughter_in_law';
    else if(label.includes('yerno'))typeEl.value='son_in_law';
    else if(label.includes('amigo'))typeEl.value='friend';
    else if(label.includes('vecino'))typeEl.value='neighbor';
    else typeEl.value=r.type||r.relationship_type||'other';
  });
}

function familyRelationExists(from,to,type){
  return (family||[]).some(r=>
    (r.from_person_id||'')===from &&
    (r.to_person_id||'')===to &&
    ((r.type||r.relationship_type||'')===type)
  );
}

async function ensureFamilyRelation(from,to,type,label){
  if(!from||!to||from===to)return {status:'skipped'};

  if(familyRelationExists(from,to,type)){
    return {status:'duplicate'};
  }

  try{
    const body={
      from_person_id:from,
      to_person_id:to,
      type:type,
      label:label||'',
      notes:'Creado desde Personas / Vínculos adicionales',
      enabled:true
    };

    const j=await api('/api/family',{method:'POST',body,csrfRequired:true});

    family.push({
      id:(j.data&&j.data.id)||'',
      from_person_id:from,
      to_person_id:to,
      type:type,
      label:label||'',
      notes:'Creado desde Personas / Vínculos adicionales',
      enabled:true
    });

    return {status:'created'};
  }catch(e){
    return {status:'failed',error:e.message||'error'};
  }
}

async function savePersonFamilyLinks(personId){
  const result={created:0,duplicates:0,skipped:0,failed:0,errors:[]};

  if(!personId)return result;

  const rows=[
    ['personFamilyType','personFamilyRelated'],
    ['personFamilyType2','personFamilyRelated2'],
    ['personFamilyType3','personFamilyRelated3']
  ];

  for(const row of rows){
    const typeEl=$(row[0]);
    const relatedEl=$(row[1]);

    const uiType=typeEl?typeEl.value:'';
    const related=relatedEl?relatedEl.value:'';

    if(!uiType||!related||related===personId){
      result.skipped++;
      continue;
    }

    const storedType=normalizeUiFamilyType(uiType);
    const label=familyTypeLabelForUi(uiType);

    const r=await ensureFamilyRelation(personId,related,storedType,label);

    if(r.status==='created')result.created++;
    else if(r.status==='duplicate')result.duplicates++;
    else if(r.status==='failed'){
      result.failed++;
      if(r.error)result.errors.push(r.error);
    }else{
      result.skipped++;
    }
  }

  return result;
}

function personSaveMessage(base,fam){
  if(!fam)return base;

  const parts=[];
  if(fam.created)parts.push(fam.created+' vinculo(s) agregado(s)');
  if(fam.duplicates)parts.push(fam.duplicates+' ya existia(n)');
  if(fam.failed)parts.push(fam.failed+' no se pudo/pudieron guardar');

  if(!parts.length)return base;

  return base+'. Vínculos adicionales: '+parts.join(', ')+'.';
}

function personFamilySummary(id){
  const parts=[];
  for(const r of family||[]){
    if(r.enabled===false)continue;
    const type=r.type||r.relationship_type||'';
    const from=r.from_person_id||'';
    const to=r.to_person_id||'';

    if(type==='spouse'){
      if(from===id&&to)parts.push('Pareja: '+personFamilyOptionText(to));
      else if(to===id&&from)parts.push('Pareja: '+personFamilyOptionText(from));
    }

    if(type==='child'){
      if(from===id&&to)parts.push('Hijo/a de: '+personFamilyOptionText(to));
      if(to===id&&from)parts.push('Madre/padre de: '+personFamilyOptionText(from));
    }
  }
  return parts.slice(0,4).join(' · ');
}

// Personas
function personPayload(){const current=people.find(x=>x.id===(editingPersonId||$('personId').value||''));return{name:$('personName').value.trim(),nickname:$('personNickname').value.trim(),relationship:$('personRelationship').value.trim(),phone:$('personPhone').value.trim(),address:$('personAddress').value.trim(),birthday:$('personBirthday').value,has_pet:$('personHasPet')?$('personHasPet').checked:!!(current&&current.has_pet),pet_type:$('personPetType')?$('personPetType').value:((current&&current.pet_type)||''),pet_name:$('personPetName')?$('personPetName').value.trim():((current&&current.pet_name)||''),aliases:$('personAliases').value.split(',').map(x=>x.trim()).filter(Boolean).slice(0,8),notes:$('personNotes').value.trim(),enabled:$('personEnabled').checked}}
function resetPerson(){editingPersonId='';$('personForm').reset();$('personId').value='';if($('personHasPet'))$('personHasPet').checked=false;if($('personPetType'))$('personPetType').value='';if($('personPetName'))$('personPetName').value='';$('personEnabled').checked=true;$('personFormTitle').textContent='Agregar persona';clearPersonFamilyFields();updatePersonFamilyOptions();const btn=$('personForm').querySelector('button.primary');if(btn)btn.textContent='Guardar'}
async function editPerson(id){let p=people.find(x=>x.id===id);try{const j=await api('/api/people/'+id);if(j&&j.data)p=j.data}catch(e){}if(!p)return;editingPersonId=p.id||id;$('personId').value=editingPersonId;$('personName').value=p.name||'';$('personNickname').value=p.nickname||'';$('personRelationship').value=p.relationship||'';$('personPhone').value=p.phone||'';$('personAddress').value=p.address||'';$('personBirthday').value=p.birthday||'';if($('personHasPet'))$('personHasPet').checked=p.has_pet===true;if($('personPetType'))$('personPetType').value=p.pet_type||'';if($('personPetName'))$('personPetName').value=p.pet_name||'';const aliases=Array.isArray(p.aliases)?p.aliases:(typeof p.aliases==='string'?p.aliases.split(','):[]);$('personAliases').value=aliases.map(x=>String(x).trim()).filter(Boolean).join(', ');$('personNotes').value=p.notes||'';$('personEnabled').checked=p.enabled!==false;$('personFormTitle').textContent='Editar '+personNumber(editingPersonId);const btn=$('personForm').querySelector('button.primary');if(btn)btn.textContent='Actualizar persona';fillPersonFamilyFields(editingPersonId);window.scrollTo({top:0,behavior:'smooth'})}
async function removePerson(id){const p=people.find(x=>x.id===id);if(!confirm('¿Eliminar '+(p?personTitle(p):'persona')+'?'))return;try{await api('/api/people/'+id,{method:'DELETE',csrfRequired:true});showMsg('Persona eliminada');resetPerson();await loadPeople();await loadReminders()}catch(e){showMsg(readableError(e.message),'error')}}
function prefsForPerson(id){return preferences.filter(x=>(x.owner_id||'profile')===id)}
function personCard(p){const c=document.createElement('div');c.className='item';const top=document.createElement('div');top.className='itemTop';const info=document.createElement('div');const n=document.createElement('div');n.className='itemName';n.textContent=p.name+(p.nickname?' · '+p.nickname:'');const m=document.createElement('div');m.className='itemMeta';m.textContent=[personNumber(p.id),p.relationship,p.enabled===false?'inactivo':''].filter(Boolean).join(' · ');info.append(n,m);const pp=prefsForPerson(p.id);const active=pp.filter(x=>x.enabled!==false);const sum=document.createElement('div');sum.className='prefSummary';sum.textContent=active.length?(active.length+' '+(active.length===1?'preferencia':'preferencias')+': '+active.slice(0,3).map(x=>x.value).join(' · ')+(active.length>3?' · ...':'')):'Sin preferencias registradas';info.appendChild(sum);const famSummary=personFamilySummary(p.id);if(famSummary){const fs=document.createElement('div');fs.className='itemNotes';fs.textContent='Vínculos adicionales: '+famSummary;info.appendChild(fs)}const acts=document.createElement('div');acts.className='actions';acts.append(actionButton('Editar datos','secondary',()=>editPerson(p.id)),actionButton('Preferencias','primary',()=>openPersonPreferences(p.id)),actionButton('Eliminar','danger',()=>removePerson(p.id)));top.append(info,acts);c.appendChild(top);if(p.notes){const q=document.createElement('div');q.className='itemNotes';q.textContent=p.notes;c.appendChild(q)}return c}
function renderPeople(){const box=$('peopleList');$('peopleCount').textContent=people.length+' / 32';updateReminderPeople();updateFamilyPeople();updatePersonFamilyOptions(editingPersonId);if(!people.length)return empty(box,'No hay personas guardadas.');box.textContent='';for(const p of people)box.appendChild(personCard(p))}
async function loadPeople(){try{const j=await api('/api/people');people=j.data||[];renderPeople();renderPersonPreferences()}catch(e){showMsg('No se pudieron leer las personas: '+readableError(e.message),'error')}}
$('personForm').addEventListener('submit',async e=>{
  e.preventDefault();
  const id=(editingPersonId||$('personId').value||'').trim();

  try{
    let savedId=id;
    let baseMsg='';

    if(id){
      await api('/api/people/'+encodeURIComponent(id),{method:'PUT',body:personPayload(),csrfRequired:true});
      baseMsg='Persona actualizada';
    }else{
      const j=await api('/api/people',{method:'POST',body:personPayload(),csrfRequired:true});
      savedId=(j.data&&j.data.id)||'';
      baseMsg='Persona agregada';
    }

    let famResult=null;
    if(savedId){
      famResult=await savePersonFamilyLinks(savedId);
    }

    resetPerson();
    await loadPeople();
    await loadFamily();

    const msg=personSaveMessage(baseMsg,famResult);
    showMsg(msg,famResult&&famResult.failed?'error':'ok');
  }catch(err){
    showMsg(readableError(err.message),'error');
  }
});$('personCancel').onclick=resetPerson;

// Familia
const familyTypeNames={child:'es hijo/a de',parent:'es padre/madre de',spouse:'es pareja/esposo/a de',sibling:'es hermano/a de',grandchild:'es nieto/a de',grandparent:'es abuelo/a de',in_law:'es familiar político de',caregiver:'cuida o acompaña a',other:'tiene relación con'};
function updateFamilyPeople(){const from=$('familyFrom'),to=$('familyTo');if(!from||!to)return;const curFrom=from.value,curTo=to.value;from.textContent='';to.textContent='';for(const opt of familyParticipantOptions()){const a=document.createElement('option');a.value=opt.id;a.textContent=opt.text;from.appendChild(a);const b=document.createElement('option');b.value=opt.id;b.textContent=opt.text;to.appendChild(b)}if([...from.options].some(o=>o.value===curFrom))from.value=curFrom;if([...to.options].some(o=>o.value===curTo))to.value=curTo}
function familyPayload(){return{from_person_id:$('familyFrom').value,to_person_id:$('familyTo').value,type:$('familyType').value,label:$('familyLabel').value.trim(),notes:$('familyNotes').value.trim(),enabled:$('familyEnabled').checked}}
function resetFamily(){$('familyForm').reset();$('familyId').value='';$('familyEnabled').checked=true;updateFamilyPeople();$('familyFormTitle').textContent='Agregar relación familiar'}
function editFamily(id){const r=family.find(x=>x.id===id);if(!r)return;$('familyId').value=r.id;updateFamilyPeople();$('familyFrom').value=r.from_person_id||'';$('familyTo').value=r.to_person_id||'';$('familyType').value=r.type||'other';$('familyLabel').value=r.label||'';$('familyNotes').value=cleanVoiceTodoNotes(r.notes)||'';$('familyEnabled').checked=r.enabled!==false;$('familyFormTitle').textContent='Editar '+familyNumber(r.id);window.scrollTo({top:0,behavior:'smooth'})}
async function removeFamily(id){const r=family.find(x=>x.id===id);if(!confirm('¿Eliminar '+(r?familyNumber(r.id):'relación familiar')+'?'))return;try{await api('/api/family/'+id,{method:'DELETE',csrfRequired:true});showMsg('Relación familiar eliminada');resetFamily();await loadFamily()}catch(e){showMsg(readableError(e.message),'error')}}
function renderFamily(){const box=$('familyList');$('familyCount').textContent=family.length+' / 64';updateFamilyPeople();if(!family.length)return empty(box,'No hay relaciones familiares guardadas.');box.textContent='';for(const r of family){const title=personNameById(r.from_person_id)+' '+(familyTypeNames[r.type]||'se relaciona con')+' '+personNameById(r.to_person_id);const meta=[familyNumber(r.id),r.label,r.enabled===false?'inactiva':''].filter(Boolean).join(' · ');box.appendChild(itemCard(title,meta,r.notes,()=>editFamily(r.id),()=>removeFamily(r.id)))}}
async function loadFamily(){try{const j=await api('/api/family');family=j.data||[];renderFamily();renderPeople();updatePersonFamilyOptions(editingPersonId)}catch(e){showMsg('No se pudieron leer las relaciones familiares: '+readableError(e.message),'error')}}
$('familyForm').addEventListener('submit',async e=>{e.preventDefault();const body=familyPayload();if(!body.from_person_id||!body.to_person_id)return showMsg('Elegí ambas personas.','error');if(body.from_person_id===body.to_person_id)return showMsg('La relación debe ser entre dos personas distintas.','error');const id=$('familyId').value;try{if(id)await api('/api/family/'+id,{method:'PUT',body,csrfRequired:true});else await api('/api/family',{method:'POST',body,csrfRequired:true});showMsg(id?'Relación familiar actualizada':'Relación familiar agregada');resetFamily();await loadFamily()}catch(err){showMsg(readableError(err.message),'error')}});$('familyCancel').onclick=resetFamily;

// Perfil
async function loadProfile(){try{const j=await api('/api/profile');const p=j.data||{};profile=p;$('profileName').value=p.name||'';$('profileNickname').value=p.nickname||'';$('profileBirthday').value=p.birthday||'';$('profileCity').value=p.city||'';$('profileTimezone').value=p.timezone||'';$('profileNotes').value=p.notes||'';$('profileState').textContent=p.configured?'Guardado':'Sin configurar';if(!p.configured&&!$('profileTimezone').value){try{$('profileTimezone').value=Intl.DateTimeFormat().resolvedOptions().timeZone||''}catch(e){}}renderPreferences();renderPeople();renderPersonPreferences();renderFamily()}catch(e){showMsg('No se pudo leer el perfil: '+readableError(e.message),'error')}}
$('profileForm').addEventListener('submit',async e=>{e.preventDefault();const body={name:$('profileName').value.trim(),nickname:$('profileNickname').value.trim(),birthday:$('profileBirthday').value,city:$('profileCity').value.trim(),timezone:$('profileTimezone').value.trim(),notes:$('profileNotes').value.trim()};try{await api('/api/profile',{method:'PUT',body,csrfRequired:true});profile={...body,configured:true};$('profileState').textContent='Guardado';renderPreferences();renderPeople();renderPersonPreferences();renderFamily();showMsg('Perfil actualizado')}catch(err){showMsg(readableError(err.message),'error')}});
// Preferencias integradas dentro de Perfil
function preferencePayload(){return{owner_id:'profile',category:$('preferenceCategory').value.trim(),value:$('preferenceValue').value.trim(),notes:$('preferenceNotes').value.trim(),enabled:$('preferenceEnabled').checked}}
function resetPreference(){$('preferenceForm').reset();$('preferenceId').value='';$('preferenceEnabled').checked=true;$('preferenceFormTitle').textContent='Agregar preferencia'}
function editPreference(id){const p=preferences.find(x=>x.id===id&&(x.owner_id||'profile')==='profile');if(!p)return;$('preferenceId').value=p.id;$('preferenceCategory').value=normalizePreferenceCategoryForSelect(p.category);$('preferenceValue').value=p.value||'';$('preferenceNotes').value=p.notes||'';$('preferenceEnabled').checked=p.enabled!==false;$('preferenceFormTitle').textContent='Editar preferencia';window.scrollTo({top:0,behavior:'smooth'})}
async function removePreference(id){if(!confirm('¿Eliminar preferencia?'))return;try{await api('/api/preferences/'+id,{method:'DELETE',csrfRequired:true});showMsg('Preferencia eliminada');resetPreference();await loadPreferences()}catch(e){showMsg(readableError(e.message),'error')}}
function renderPreferences(){const box=$('preferencesList');const own=preferences.filter(p=>(p.owner_id||'profile')==='profile');$('preferencesCount').textContent=own.length+' del usuario · '+preferences.length+' / 32 total';$('preferencesTitle').textContent='Preferencias de '+(profile.name||'la persona usuaria');if(!own.length)return empty(box,'No hay preferencias guardadas en el perfil.');box.textContent='';for(const p of own){const meta=[preferenceNumber(p.id,own),preferenceCategoryLabel(p.category),p.enabled===false?'inactiva':''].filter(Boolean).join(' · ');box.appendChild(itemCard(p.value,meta,p.notes,()=>editPreference(p.id),()=>removePreference(p.id)))}}
async function loadPreferences(){try{const j=await api('/api/preferences');preferences=j.data||[];renderPreferences();renderPeople();renderPersonPreferences()}catch(e){showMsg('No se pudieron leer las preferencias: '+readableError(e.message),'error')}}
$('preferenceForm').addEventListener('submit',async e=>{e.preventDefault();const id=$('preferenceId').value;try{if(id)await api('/api/preferences/'+id,{method:'PUT',body:preferencePayload(),csrfRequired:true});else await api('/api/preferences',{method:'POST',body:preferencePayload(),csrfRequired:true});showMsg(id?'Preferencia actualizada':'Preferencia agregada');resetPreference();await loadPreferences()}catch(err){showMsg(readableError(err.message),'error')}});$('preferenceCancel').onclick=resetPreference;
// Preferencias de personas: se administran dentro de la pestaña Personas.
let personPreferenceOwnerId='';
function resetPersonPreference(){$('personPreferenceForm').reset();$('personPreferenceId').value='';$('personPreferenceEnabled').checked=true;$('personPreferenceCategory').value='pets';$('personPreferenceFormTitle').textContent='Agregar dato o preferencia'}
function openPersonPreferences(id){const p=people.find(x=>x.id===id);if(!p)return;personPreferenceOwnerId=id;resetPersonPreference();$('personPreferencesTitle').textContent='Preferencias de '+p.name+(p.nickname?' · '+p.nickname:'');$('personPreferencesMeta').textContent=[personNumber(p.id),p.relationship].filter(Boolean).join(' · ');$('personPreferencesPanel').classList.remove('hidden');renderPersonPreferences();$('personPreferencesPanel').scrollIntoView({behavior:'smooth',block:'start'})}
function closePersonPreferences(){personPreferenceOwnerId='';resetPersonPreference();$('personPreferencesPanel').classList.add('hidden')}
function personPreferencePayload(){return{owner_id:personPreferenceOwnerId,category:$('personPreferenceCategory').value.trim(),value:$('personPreferenceValue').value.trim(),notes:$('personPreferenceNotes').value.trim(),enabled:$('personPreferenceEnabled').checked}}
function editPersonPreference(id){const p=preferences.find(x=>x.id===id&&x.owner_id===personPreferenceOwnerId);if(!p)return;$('personPreferenceId').value=p.id;$('personPreferenceCategory').value=normalizePreferenceCategoryForSelect(p.category);$('personPreferenceValue').value=p.value||'';$('personPreferenceNotes').value=p.notes||'';$('personPreferenceEnabled').checked=p.enabled!==false;$('personPreferenceFormTitle').textContent='Editar preferencia';$('personPreferencesPanel').scrollIntoView({behavior:'smooth',block:'start'})}
async function removePersonPreference(id){if(!confirm('¿Eliminar preferencia?'))return;try{await api('/api/preferences/'+id,{method:'DELETE',csrfRequired:true});showMsg('Preferencia eliminada');resetPersonPreference();await loadPreferences()}catch(e){showMsg(readableError(e.message),'error')}}
function renderPersonPreferences(){const panel=$('personPreferencesPanel');if(!panel||!personPreferenceOwnerId)return;const owner=people.find(x=>x.id===personPreferenceOwnerId);if(!owner){closePersonPreferences();return}const list=prefsForPerson(personPreferenceOwnerId);$('personPreferencesTitle').textContent='Preferencias de '+owner.name+(owner.nickname?' · '+owner.nickname:'');$('personPreferencesMeta').textContent=[personNumber(owner.id),owner.relationship].filter(Boolean).join(' · ');$('personPreferencesCount').textContent=list.length+' / 32 total';const box=$('personPreferencesList');if(!list.length)return empty(box,'Esta persona todavía no tiene preferencias registradas.');box.textContent='';for(const p of list){const meta=[preferenceNumber(p.id,list),preferenceCategoryLabel(p.category),p.enabled===false?'inactiva':''].filter(Boolean).join(' · ');box.appendChild(itemCard(p.value,meta,p.notes,()=>editPersonPreference(p.id),()=>removePersonPreference(p.id)))}}
$('personPreferenceForm').addEventListener('submit',async e=>{e.preventDefault();if(!personPreferenceOwnerId)return showMsg('Seleccioná una persona.','error');const id=$('personPreferenceId').value;try{if(id)await api('/api/preferences/'+id,{method:'PUT',body:personPreferencePayload(),csrfRequired:true});else await api('/api/preferences',{method:'POST',body:personPreferencePayload(),csrfRequired:true});showMsg(id?'Preferencia actualizada':'Preferencia agregada a la persona');resetPersonPreference();await loadPreferences()}catch(err){showMsg(readableError(err.message),'error')}});$('personPreferenceCancel').onclick=resetPersonPreference;$('closePersonPreferences').onclick=closePersonPreferences;

// Cuidados diarios no medicinales
const routineTypeNames={medication:'Medicación',hydration:'Hidratación',exercise:'Ejercicio',health_measurement:'Medición de salud',reminder:'Recordatorio',birthday:'Cumpleaños',custom:'Personalizada'};
const routineStateNames={active:'Activa',paused:'Pausada',archived:'Archivada'};
const routineRepeatNames={daily:'Todos los días',specific_weekdays:'Días específicos',every_n_days:'Cada N días',as_needed:'Según necesidad'};
const routineMomentNames={anytime:'En cualquier momento',fasting:'En ayunas',before_breakfast:'Antes del desayuno',with_breakfast:'Con el desayuno',after_breakfast:'Después del desayuno',before_lunch:'Antes del almuerzo',with_lunch:'Con el almuerzo',after_lunch:'Después del almuerzo',before_dinner:'Antes de la cena',with_dinner:'Con la cena',after_dinner:'Después de la cena',bedtime:'Antes de dormir'};
const routinePlacementNames={none:'Sin ubicación',pillbox:'Pastillero',blister:'Blíster',original_box:'Caja original',table:'Mesa',shelf:'Estante',custom:'Otra'};const careAlertModeNames={disabled:'Sin aviso',visual:'Solo visual',sound:'Solo sonoro',visual_sound:'Visual + sonoro'};const careVisualPatternNames={none:'Sin visual',solid:'Luz fija',breathing:'Respiración suave',soft_blink:'Parpadeo suave',progress_bar:'Barra de progreso',pillbox_slot:'Color por casillero'};const careAlertColorNames={auto:'Automático',green:'Verde',blue:'Azul',yellow:'Amarillo',soft_red:'Rojo suave',violet:'Violeta',white:'Blanco'};const careSoundPatternNames={none:'Sin sonido',soft_beep:'Bip suave',chime:'Campanita',friendly_reminder:'Recordatorio amable',progressive:'Progresivo'};
const routineDayNames=['Lun','Mar','Mié','Jue','Vie','Sáb','Dom'];
function getRoutineWeekdays(){return [...document.querySelectorAll('.routineWeekday')].map(x=>x.checked)}
function setRoutineWeekdays(days){const arr=Array.isArray(days)&&days.length>=7?days:[true,true,true,true,true,true,true];document.querySelectorAll('.routineWeekday').forEach((x,i)=>x.checked=!!arr[i])}
function ensureRoutineSelectValue(id,value,label){
  const el=$(id);if(!el)return;
  const v=String(value==null?'':value);
  if(!v)return;
  if(![...el.options].some(o=>o.value===v)){
    const o=document.createElement('option');o.value=v;o.textContent=label||v;el.appendChild(o);
  }
  el.value=v;
}
function updateRoutineUi(){
  const repeat=$('routineRepeat').value;
  $('routineWeekdaysWrap').classList.toggle('hidden',repeat!=='specific_weekdays');
  $('routineIntervalWrap').classList.toggle('hidden',repeat!=='every_n_days');

  const enabled=$('routineAlertEnabled').checked;
  const mode=$('routineAlertMode').value;
  const active=enabled&&mode!=='disabled';
  const visual=active&&(mode==='visual'||mode==='visual_sound');
  const sound=active&&(mode==='sound'||mode==='visual_sound');

  $('routineAlertVisualWrap').classList.toggle('hidden',!visual);
  $('routineAlertSoundWrap').classList.toggle('hidden',!sound);
  $('routineAlertCadenceWrap').classList.toggle('hidden',!active);
}
function routinePayload(){return{title:$('routineTitle').value.trim(),type:$('routineType').value,state:$('routineState').value,time:$('routineTime').value,moment:$('routineMoment').value,repeat:$('routineRepeat').value,weekdays:getRoutineWeekdays(),interval_days:Number($('routineInterval').value)||1,reminder_window_minutes:Number($('routineWindow').value)||30,placement_type:$('routinePlacementType').value,placement_description:$('routinePlacementDescription').value.trim(),compartment:$('routineCompartment').value.trim(),monitored:$('routineMonitored').checked,ble_slot:Number($('routineBleSlot').value)||0,led_number:Number($('routineLedNumber').value)||0,alert_enabled:$('routineAlertEnabled').checked,alert_mode:$('routineAlertMode').value,alert_visual_pattern:$('routineAlertVisualPattern').value,alert_visual_color:$('routineAlertVisualColor').value,alert_sound_pattern:$('routineAlertSoundPattern').value,alert_voice_enabled:$('routineAlertVoice').checked,alert_repeat_minutes:Number($('routineAlertRepeat').value)||10,alert_max_repeats:Number($('routineAlertMax').value)||3,description:$('routineDescription').value.trim(),data:$('routineData').value.trim()}}
function resetRoutine(){
  $('routineForm').reset();
  $('routineId').value='';
  $('routineType').value='hydration';
  $('routineState').value='active';
  $('routineMoment').value='anytime';
  $('routineRepeat').value='daily';
  $('routineInterval').value='1';
  $('routineWindow').value='30';
  $('routineAlertEnabled').checked=true;
  $('routineAlertMode').value='visual';
  $('routineAlertVisualPattern').value='breathing';
  $('routineAlertVisualColor').value='auto';
  $('routineAlertSoundPattern').value='soft_beep';
  $('routineAlertVoice').checked=false;
  $('routineAlertRepeat').value='10';
  $('routineAlertMax').value='3';
  $('routineBleSlot').value='0';
  $('routineLedNumber').value='0';
  setRoutineWeekdays([true,true,true,true,true,true,true]);
  $('routineFormTitle').textContent='Agregar cuidado';
  updateRoutineUi();
}
function editRoutine(id){
  const r=routines.find(x=>x.id===id);if(!r)return;
  $('routineId').value=r.id;
  $('routineTitle').value=r.title||'';
  $('routineType').value=r.type||'custom';
  $('routineState').value=r.state||'active';
  $('routineTime').value=r.time||'';
  $('routineMoment').value=r.moment||'anytime';
  $('routineRepeat').value=r.repeat||'daily';
  setRoutineWeekdays(r.weekdays);

  ensureRoutineSelectValue('routineInterval',r.interval_days||1,'Cada '+(r.interval_days||1)+' días');
  ensureRoutineSelectValue('routineWindow',r.reminder_window_minutes||30,(r.reminder_window_minutes||30)+' minutos antes');

  $('routinePlacementType').value=r.placement_type||'none';
  $('routinePlacementDescription').value=r.placement_description||'';
  $('routineCompartment').value=r.compartment||'';
  $('routineMonitored').checked=!!r.monitored;
  $('routineBleSlot').value=r.ble_slot||0;
  $('routineLedNumber').value=r.led_number||0;
  $('routineAlertEnabled').checked=r.alert_enabled!==false;
  $('routineAlertMode').value=r.alert_mode||'visual';
  $('routineAlertVisualPattern').value=r.alert_visual_pattern||'breathing';
  $('routineAlertVisualColor').value=r.alert_visual_color||'auto';
  $('routineAlertSoundPattern').value=r.alert_sound_pattern||'soft_beep';
  $('routineAlertVoice').checked=!!r.alert_voice_enabled;

  ensureRoutineSelectValue('routineAlertRepeat',r.alert_repeat_minutes||10,(r.alert_repeat_minutes||10)+' minutos');
  ensureRoutineSelectValue('routineAlertMax',r.alert_max_repeats||3,(r.alert_max_repeats||3)+' avisos');

  $('routineDescription').value=r.description||'';
  $('routineData').value=r.data||'';
  $('routineFormTitle').textContent='Editar cuidado '+routineNumber(r.id).replace('Cuidado ','');
  updateRoutineUi();
  window.scrollTo({top:0,behavior:'smooth'});
}
async function removeRoutine(id){const r=routines.find(x=>x.id===id);if(!confirm('¿Eliminar '+(r?routineNumber(r.id):'este cuidado')+'?'))return;try{await api('/api/routines/'+id,{method:'DELETE',csrfRequired:true});showMsg('Cuidado eliminado');resetRoutine();await loadRoutines();renderPillbox()}catch(e){showMsg(readableError(e.message),'error')}}
function renderRoutines(){const box=$('routinesList');const careItems=(routines||[]).filter(r=>r.type!=='medication');$('routinesCount').textContent=careItems.length+' cuidados / 64';if(!careItems.length)return empty(box,'No hay cuidados diarios no medicinales guardados. La medicación se carga solamente desde Pastillero.');box.textContent='';const sorted=[...careItems].sort((a,b)=>(a.time||'99:99').localeCompare(b.time||'99:99')||(a.title||'').localeCompare(b.title||''));for(const r of sorted){const days=(r.weekdays||[]).map((v,i)=>v?routineDayNames[i]:'').filter(Boolean).join(' ');const meta=[routineNumber(r.id),r.time,routineTypeNames[r.type]||r.type,routineStateNames[r.state]||r.state,routineRepeatNames[r.repeat]||r.repeat,days].filter(Boolean).join(' · ');const notes=[routineMomentNames[r.moment]?'Momento: '+routineMomentNames[r.moment]:'',routinePlacementNames[r.placement_type]?'Ubicación: '+routinePlacementNames[r.placement_type]:'',r.alert_enabled===false||r.alert_mode==='disabled'?'Aviso: sin aviso':'Aviso: '+(careAlertModeNames[r.alert_mode]||'Solo visual')+' · '+(careVisualPatternNames[r.alert_visual_pattern]||'Respiración suave')+' · '+(careAlertColorNames[r.alert_visual_color]||'Automático')+' · '+(careSoundPatternNames[r.alert_sound_pattern]||'Bip suave')+(r.alert_voice_enabled?' · Voz':'')+', repetir cada '+(r.alert_repeat_minutes||10)+' min, máximo '+(r.alert_max_repeats||3),r.placement_description,r.description].filter(Boolean).join('\n');box.appendChild(itemCard(r.title,meta,notes,()=>editRoutine(r.id),()=>removeRoutine(r.id)))}}
async function loadRoutines(){try{const j=await api('/api/routines');routines=j.data||[];renderRoutines()}catch(e){showMsg('No se pudieron leer los cuidados: '+readableError(e.message),'error')}}
$('routineRepeat').addEventListener('change',updateRoutineUi);
$('routineAlertEnabled').addEventListener('change',updateRoutineUi);
$('routineAlertMode').addEventListener('change',updateRoutineUi);
updateRoutineUi();
$('routineForm').addEventListener('submit',async e=>{e.preventDefault();const id=$('routineId').value;const payload=routinePayload();const created=!id;try{if(id)await api('/api/routines/'+id,{method:'PUT',body:payload,csrfRequired:true});else await api('/api/routines',{method:'POST',body:payload,csrfRequired:true});resetRoutine();await loadRoutines();renderPillbox();showRoutineSaveConfirmation(created,payload.title)}catch(err){showMsg('No pude guardar el cuidado: '+readableError(err.message),'error')}});$('routineCancel').onclick=resetRoutine;

// Pastillero como vista agrupada de medicaciones. La medicación no se repite en Cuidados.
function getPillboxWeekdays(){return [...document.querySelectorAll('.pillboxWeekday')].map(x=>x.checked)}
function setPillboxWeekdays(days){const v=Array.isArray(days)&&days.length===7?days:[true,true,true,true,true,true,true];document.querySelectorAll('.pillboxWeekday').forEach((x,i)=>x.checked=!!v[i])}
function pillboxItems(){return (routines||[]).filter(r=>r.type==='medication'&&r.placement_type==='pillbox')}
function pillboxPayload(){const internal=$('pillboxData').value.trim();const data={quantity:safeInt($('pillboxQuantity').value,1),visual_description:$('pillboxVisualDescription').value.trim(),private_notes:internal};return{title:$('pillboxTitle').value.trim(),type:'medication',state:$('pillboxState').value,time:$('pillboxTime').value,moment:$('pillboxMoment').value,repeat:'specific_weekdays',weekdays:getPillboxWeekdays(),interval_days:1,reminder_window_minutes:Number($('pillboxWindow').value)||30,placement_type:'pillbox',placement_description:$('pillboxPlacementDescription').value.trim(),compartment:$('pillboxCompartment').value.trim(),monitored:false,ble_slot:0,led_number:0,alert_enabled:$('pillboxAlertEnabled').checked,alert_mode:$('pillboxAlertMode').value,alert_visual_pattern:$('pillboxAlertVisualPattern').value,alert_visual_color:$('pillboxAlertVisualColor').value,alert_sound_pattern:$('pillboxAlertSoundPattern').value,alert_voice_enabled:$('pillboxAlertVoice').checked,alert_repeat_minutes:Number($('pillboxAlertRepeat').value)||10,alert_max_repeats:Number($('pillboxAlertMax').value)||3,description:$('pillboxDescription').value.trim(),data:JSON.stringify(data)}}
function resetPillbox(){$('pillboxForm').reset();$('pillboxId').value='';$('pillboxState').value='active';$('pillboxMoment').value='before_breakfast';$('pillboxWindow').value='30';$('pillboxAlertEnabled').checked=true;$('pillboxAlertMode').value='visual';$('pillboxAlertVisualPattern').value='pillbox_slot';$('pillboxAlertVisualColor').value='auto';$('pillboxAlertSoundPattern').value='soft_beep';$('pillboxAlertVoice').checked=true;$('pillboxAlertRepeat').value='10';$('pillboxAlertMax').value='3';$('pillboxQuantity').value='1';setPillboxWeekdays([true,true,true,true,true,true,true]);$('pillboxFormTitle').textContent='Agregar medicamento al pastillero'}
function editPillbox(id){const r=routines.find(x=>x.id===id);if(!r)return;const d=routineExtra(r);$('pillboxId').value=r.id;$('pillboxTitle').value=r.title||'';$('pillboxTime').value=r.time||'';$('pillboxMoment').value=r.moment||'anytime';$('pillboxState').value=r.state||'active';$('pillboxWindow').value=r.reminder_window_minutes||30;$('pillboxAlertEnabled').checked=r.alert_enabled!==false;$('pillboxAlertMode').value=r.alert_mode||'visual';$('pillboxAlertVisualPattern').value=r.alert_visual_pattern||'pillbox_slot';$('pillboxAlertVisualColor').value=r.alert_visual_color||'auto';$('pillboxAlertSoundPattern').value=r.alert_sound_pattern||'soft_beep';$('pillboxAlertVoice').checked=!!r.alert_voice_enabled;$('pillboxAlertRepeat').value=r.alert_repeat_minutes||10;$('pillboxAlertMax').value=r.alert_max_repeats||3;setPillboxWeekdays(r.weekdays);$('pillboxCompartment').value=r.compartment||'';$('pillboxQuantity').value=routineQuantity(r);$('pillboxVisualDescription').value=routineVisual(r);$('pillboxPlacementDescription').value=r.placement_description||'';$('pillboxDescription').value=r.description||'';$('pillboxData').value=d.private_notes||'';$('pillboxFormTitle').textContent='Editar '+pillboxNumber(r.id);window.scrollTo({top:0,behavior:'smooth'})}
async function removePillbox(id){const r=routines.find(x=>x.id===id);if(!confirm('¿Eliminar '+(r?pillboxNumber(r.id):'esta medicación del pastillero')+'? También se quitará del sistema de avisos.'))return;try{await api('/api/routines/'+id,{method:'DELETE',csrfRequired:true});showMsg('Medicación del pastillero eliminada');resetPillbox();await loadRoutines();renderPillbox()}catch(e){showMsg(readableError(e.message),'error')}}
function createPillboxGroupCard(group,index){const c=document.createElement('div');c.className='item';const top=document.createElement('div');top.className='itemTop';const info=document.createElement('div');const title=document.createElement('div');title.className='pillboxGroupTitle';const total=group.items.reduce((a,r)=>a+routineQuantity(r),0);title.textContent='Casillero '+(index+1)+': '+group.name;const badge=document.createElement('span');badge.className='pillboxBadge';badge.textContent='Tiene que tener '+total+' '+(total===1?'pastilla':'pastillas');title.appendChild(badge);const moments=[...new Set(group.items.map(r=>routineMomentNames[r.moment]||r.moment).filter(Boolean))].join(' · ');const times=[...new Set(group.items.map(r=>r.time).filter(Boolean))].sort().join(' · ');const meta=document.createElement('div');meta.className='itemMeta';meta.textContent=[times,moments].filter(Boolean).join(' · ');info.append(title,meta);const location=[...new Set(group.items.map(r=>(r.placement_description||'').trim()).filter(Boolean))].join(' · ');if(location){const loc=document.createElement('div');loc.className='pillboxLineNotes';loc.textContent='Dónde está el pastillero: '+location;info.appendChild(loc)}top.append(info);c.appendChild(top);group.items.forEach((r,itemIndex)=>{const line=document.createElement('div');line.className='pillboxLine';const nm=document.createElement('div');nm.className='pillboxLineName';nm.textContent='Medicamento '+(itemIndex+1)+' - '+routineQuantity(r)+' '+(routineQuantity(r)===1?'pastilla':'pastillas');const mt=document.createElement('div');mt.className='pillboxLineMeta';const days=(r.weekdays||[]).map((v,i)=>v?routineDayNames[i]:'').filter(Boolean).join(' ');mt.textContent=[pillboxNumber(r.id),r.time,routineMomentNames[r.moment]||r.moment,routineStateNames[r.state]||r.state,days].filter(Boolean).join(' · ');const notes=[r.title?'Referencia administrativa: '+r.title:'',routineVisual(r)?'Descripción visual: '+routineVisual(r):'',r.alert_enabled===false||r.alert_mode==='disabled'?'Aviso: sin aviso':'Aviso: '+(careAlertModeNames[r.alert_mode]||'Solo visual')+' · '+(careVisualPatternNames[r.alert_visual_pattern]||'Color por casillero')+' · '+(careAlertColorNames[r.alert_visual_color]||'Automático')+(r.alert_voice_enabled?' · Voz segura':''),r.description].filter(Boolean).join('\n');line.append(nm,mt);if(notes){const nt=document.createElement('div');nt.className='pillboxLineNotes';nt.textContent=notes;line.appendChild(nt)}const acts=document.createElement('div');acts.className='actions';acts.style.marginTop='8px';acts.append(actionButton('Editar','secondary',()=>editPillbox(r.id)),actionButton('Eliminar','danger',()=>removePillbox(r.id)));line.appendChild(acts);c.appendChild(line)});return c}
function renderPillbox(){const box=$('pillboxList');pillbox=pillboxItems().sort((a,b)=>((a.compartment||'').localeCompare(b.compartment||''))||((a.time||'99:99').localeCompare(b.time||'99:99'))||(a.title||'').localeCompare(b.title||''));$('pillboxCount').textContent=pillbox.length+' medicación(es) / 64';if(!pillbox.length)return empty(box,'No hay medicaciones cargadas en el pastillero. Agregalas desde esta pestaña.');box.textContent='';const groups=[];for(const r of pillbox){const name=normalizeCompartmentName(r.compartment);let g=groups.find(x=>x.name.toLowerCase()===name.toLowerCase());if(!g){g={name,items:[]};groups.push(g)}g.items.push(r)}for(let i=0;i<groups.length;i++)box.appendChild(createPillboxGroupCard(groups[i],i))}
async function loadPillbox(){try{const j=await api('/api/routines');routines=j.data||[];renderRoutines();renderPillbox()}catch(e){showMsg('No se pudo leer el pastillero: '+readableError(e.message),'error')}}
$('pillboxForm').addEventListener('submit',async e=>{e.preventDefault();const id=$('pillboxId').value;const payload=pillboxPayload();const created=!id;try{if(id)await api('/api/routines/'+id,{method:'PUT',body:payload,csrfRequired:true});else await api('/api/routines',{method:'POST',body:payload,csrfRequired:true});resetPillbox();await loadRoutines();renderPillbox();showPillboxSaveConfirmation(created,payload.compartment||payload.title)}catch(err){showMsg('No pude guardar el pastillero: '+readableError(err.message),'error')}});$('pillboxCancel').onclick=resetPillbox;

// Recordatorios
const recurrenceNames={none:'No repetir',daily:'Diaria',weekly:'Semanal',monthly:'Mensual',yearly:'Anual'};
function updateReminderPeople(){const s=$('reminderPerson'),current=s.value;s.textContent='';const none=document.createElement('option');none.value='';none.textContent='Sin persona vinculada';s.appendChild(none);for(const p of people){const o=document.createElement('option');o.value=p.id;o.textContent=personTitle(p);s.appendChild(o)}if([...s.options].some(o=>o.value===current))s.value=current}

function pad2(n){return String(n).padStart(2,'0')}
function localDateText(d){return d.getFullYear()+'-'+pad2(d.getMonth()+1)+'-'+pad2(d.getDate())}
function jsWeekdayToCare(d){const v=d.getDay();return v===0?7:v}
function nextDateForReminderWeekday(careWeekday){
  const today=new Date();
  const current=jsWeekdayToCare(today);
  const add=(careWeekday-current+7)%7;
  const d=new Date(today.getFullYear(),today.getMonth(),today.getDate()+add);
  return localDateText(d);
}
function reminderWeekdayFromDate(dateText){
  if(!dateText)return '';
  const d=new Date(dateText+'T12:00:00');
  if(isNaN(d.getTime()))return '';
  return String(jsWeekdayToCare(d));
}
function readRememberBeforeFromNotes(notes){
  const m=(notes||'').match(/\[\[remember_before_minutes:(\d+)\]\]/);
  return m?m[1]:'0';
}

function cleanVoiceTodoNotes(notes){
  let n=(notes||'').replace(/\[\[voice_todo:1\]\]/g,'').trim();
  if(!n)return '';
  n=n.replace(/^Creado por voz\.\s*Cosa para hacer\.$/i,'Anotado por CARE desde la voz.');
  n=n.replace(/^Creado por voz\./i,'Anotado por CARE desde la voz.');
  return n.trim();
}

function cleanRememberBeforeFromNotes(notes){
  return (notes||'').replace(/\s*\[\[remember_before_minutes:\d+\]\]\s*/g,'').trim();
}
function notesWithRememberBefore(notes,minutes){
  const clean=cleanRememberBeforeFromNotes(notes);
  const tag='[[remember_before_minutes:'+(minutes||0)+']]';
  return clean ? clean+'\n'+tag : tag;
}
function updateReminderModeUi(){
  const mode=$('reminderMode');
  const dateBox=$('reminderDateBox');
  const weekdayBox=$('reminderWeekdayBox');
  const date=$('reminderDate');
  const rec=$('reminderRecurrence');

  if(!mode)return;

  const weekly=mode.value==='weekly';

  if(dateBox)dateBox.style.display=weekly?'none':'block';
  if(weekdayBox)weekdayBox.style.display=weekly?'block':'none';

  if(date){
    date.disabled=weekly;
    date.required=!weekly;
  }

  if(rec && weekly)rec.value='weekly';
}
function setupSmartReminderUi(){
  const mode=$('reminderMode');
  const rec=$('reminderRecurrence');

  if(mode)mode.addEventListener('change',updateReminderModeUi);
  if(rec)rec.addEventListener('change',()=>{
    if($('reminderMode') && rec.value==='weekly')$('reminderMode').value='weekly';
    updateReminderModeUi();
  });

  updateReminderModeUi();
}
setupSmartReminderUi();

function reminderPayload(){
  const mode=$('reminderMode').value;
  const weekday=$('reminderWeekday').value;

  let date=$('reminderDate').value;
  let recurrence=$('reminderRecurrence').value;

  if(mode==='weekly'){
    if(weekday===''){
      throw new Error('Seleccioná un día de la semana o Todos los días.');
    }

    if(weekday==='0'){
      // "Todos los días" usa la recurrencia diaria nativa.
      recurrence='daily';

      // Fecha base = hoy.
      const now=new Date();
      const y=now.getFullYear();
      const m=String(now.getMonth()+1).padStart(2,'0');
      const d=String(now.getDate()).padStart(2,'0');
      date=`${y}-${m}-${d}`;

    }else{
      recurrence='weekly';

      // Calcula la próxima fecha correspondiente al día elegido.
      const target=Number(weekday);
      const now=new Date();
      const current=now.getDay()===0 ? 7 : now.getDay();
      const delta=(target-current+7)%7;

      const next=new Date(
        now.getFullYear(),
        now.getMonth(),
        now.getDate()+delta
      );

      const y=next.getFullYear();
      const m=String(next.getMonth()+1).padStart(2,'0');
      const d=String(next.getDate()).padStart(2,'0');

      date=`${y}-${m}-${d}`;
    }
  }

  let notes=$('reminderNotes').value.trim();

  // Eliminar una marca anterior para evitar duplicados.
  notes=notes.replace(
    /\s*\[\[remember_before_minutes:\d+\]\]\s*/g,
    ' '
  ).trim();

  const rememberBefore=
    Number($('reminderRememberBefore').value)||0;

  const tag=
    `[[remember_before_minutes:${rememberBefore}]]`;

  notes=notes ? `${tag} ${notes}` : tag;

  return{
    title:$('reminderTitle').value.trim(),
    date:date,
    time:$('reminderTime').value,
    recurrence:recurrence,
    related_person_id:$('reminderPerson').value,
    notes:notes,
    enabled:$('reminderEnabled').checked
  };
}
function resetReminder(){
  $('reminderForm').reset();
  $('reminderId').value='';
  $('reminderEnabled').checked=true;
  $('reminderRecurrence').value='none';
  if($('reminderMode'))$('reminderMode').value='date';
  if($('reminderWeekday'))$('reminderWeekday').value='';
  if($('reminderRememberBefore'))$('reminderRememberBefore').value='0';
  updateReminderModeUi();
  updateReminderPeople();
  $('reminderFormTitle').textContent='Agregar recordatorio'
}
function editReminder(id){
  const r=reminders.find(x=>x.id===id);
  if(!r)return;
  $('reminderId').value=r.id;
  $('reminderTitle').value=r.title||'';
  $('reminderDate').value=r.date||'';
  $('reminderTime').value=r.time||'';
  $('reminderRecurrence').value=r.recurrence||'none';
  if($('reminderMode'))$('reminderMode').value=(r.recurrence==='weekly'?'weekly':'date');
  if($('reminderWeekday'))$('reminderWeekday').value=(r.recurrence==='weekly'?reminderWeekdayFromDate(r.date):'');
  if($('reminderRememberBefore'))$('reminderRememberBefore').value=readRememberBeforeFromNotes(r.notes);
  updateReminderModeUi();
  updateReminderPeople();
  $('reminderPerson').value=r.related_person_id||'';
  $('reminderNotes').value=cleanRememberBeforeFromNotes(cleanVoiceTodoNotes(r.notes)||'');
  $('reminderEnabled').checked=r.enabled!==false;
  $('reminderFormTitle').textContent='Editar '+reminderNumber(r.id);
  window.scrollTo({top:0,behavior:'smooth'})
}
async function removeReminder(id){const r=reminders.find(x=>x.id===id);if(!confirm('¿Eliminar '+(r?reminderNumber(r.id):'recordatorio')+'?'))return;try{await api('/api/reminders/'+id,{method:'DELETE',csrfRequired:true});showMsg('Recordatorio eliminado');resetReminder();await loadReminders();await loadRecordings();await loadMaintenance()}catch(e){showMsg(readableError(e.message),'error')}}
function renderReminders(){const box=$('remindersList');$('remindersCount').textContent=reminders.length+' / 64';if(!reminders.length)return empty(box,'No hay recordatorios guardados.');box.textContent='';const sorted=[...reminders].sort((a,b)=>(a.date||'').localeCompare(b.date||'')||(a.time||'').localeCompare(b.time||''));for(const r of sorted){const person=people.find(p=>p.id===r.related_person_id);const audio=recordings.find(a=>a.reminder_id===r.id);const meta=[reminderNumber(r.id),r.date,r.time,recurrenceNames[r.recurrence]||r.recurrence,person?personTitle(person):'',audio?'Audio '+(audio.label||audio.id):'',r.enabled===false?'inactivo':''].filter(Boolean).join(' · ');const notes=[cleanRememberBeforeFromNotes(cleanVoiceTodoNotes(r.notes)||''),audio?'Audio asignado: '+(audio.label||audio.id):''].filter(Boolean).join('\n');box.appendChild(itemCard(r.title,meta,notes,()=>editReminder(r.id),()=>removeReminder(r.id)))}}
async function loadReminders(){try{const j=await api('/api/reminders');reminders=j.data||[];renderReminders();updateRecordingReminderOptions();renderRecordings()}catch(e){showMsg('No se pudieron leer los recordatorios: '+readableError(e.message),'error')}}
$('reminderForm').addEventListener('submit',async e=>{e.preventDefault();const id=$('reminderId').value;try{if(id)await api('/api/reminders/'+id,{method:'PUT',body:reminderPayload(),csrfRequired:true});else await api('/api/reminders',{method:'POST',body:reminderPayload(),csrfRequired:true});showMsg(id?'Recordatorio actualizado':'Recordatorio agregado');resetReminder();await loadReminders()}catch(err){showMsg(readableError(err.message),'error')}});$('reminderCancel').onclick=resetReminder;

// Audios por recordatorio - DP-018
function voiceTargetParts(id){
  const value=String(id||'');
  if(value.startsWith('routine:'))return{type:'routine',id:value.slice(8),raw:value};
  if(value.startsWith('reminder:'))return{type:'reminder',id:value.slice(9),raw:value};
  return{type:'reminder',id:value,raw:value};
}
function voiceTargetTitle(id){
  if(!id)return'Sin asignar';
  const t=voiceTargetParts(id);
  if(t.type==='routine'){
    const r=routines.find(x=>x.id===t.id);
    if(!r)return'Pastillero no encontrado';
    if(r.type==='medication'&&r.placement_type==='pillbox')return'Pastillero · '+pillboxNumber(r.id)+' · '+(r.compartment||r.title||'casillero');
    return'Cuidado · '+routineNumber(r.id)+' · '+(r.title||'sin título');
  }
  const r=reminders.find(x=>x.id===t.id);
  return r?(reminderNumber(r.id)+' · '+r.title):'Sin asignar';
}
function recordingReminderTitle(id){return voiceTargetTitle(id)}
function recordingAssignedTo(targetId,exceptId=''){return recordings.find(r=>r.id!==exceptId&&r.reminder_id===targetId)}
function addVoiceOption(group,value,label,exceptRecordingId,current){
  const used=recordingAssignedTo(value,exceptRecordingId);
  const o=document.createElement('option');
  o.value=value;
  o.textContent=label+(used?' · ya tiene audio':'');
  if(used&&value!==current)o.disabled=true;
  group.appendChild(o);
}
function fillReminderSelect(select,current='',exceptRecordingId='',requireAssignment=false){
  select.textContent='';
  if(!requireAssignment){
    const none=document.createElement('option');
    none.value='';
    none.textContent='Sin asignar';
    select.appendChild(none);
  }else{
    const choose=document.createElement('option');
    choose.value='';
    choose.textContent='Seleccionar recordatorio o pastillero';
    select.appendChild(choose);
  }

  if((reminders||[]).length){
    const group=document.createElement('optgroup');
    group.label='Recordatorios';
    for(const r of reminders){
      addVoiceOption(group,r.id,reminderNumber(r.id)+' · '+r.title,exceptRecordingId,current);
    }
    select.appendChild(group);
  }

  const pillItems=typeof pillboxItems==='function'?pillboxItems():[];
  if(pillItems.length){
    const group=document.createElement('optgroup');
    group.label='Pastillero';
    for(const r of pillItems){
      const label='Pastillero · '+pillboxNumber(r.id)+' · '+(r.compartment||r.title||'casillero')+(r.time?' · '+r.time:'');
      addVoiceOption(group,'routine:'+r.id,label,exceptRecordingId,current);
    }
    select.appendChild(group);
  }

  if([...select.options].some(o=>o.value===current))select.value=current;
}
function updateRecordingReminderOptions(){
  const s=$('recordingReminder');if(!s)return;
  const current=s.value;fillReminderSelect(s,current,'',true);
  const targets=[...(reminders||[]).map(r=>r.id),...(typeof pillboxItems==='function'?pillboxItems():[]).map(r=>'routine:'+r.id)];
  const available=targets.some(id=>!recordingAssignedTo(id));
  $('recordingSaveBtn').disabled=!available||!targets.length;
  if(!available&&targets.length)$('recordingSaveBtn').title='Todos los recordatorios y pastilleros ya tienen audio';
  else $('recordingSaveBtn').title='';
}

let preparedAudioFiles=[];
let preparedAudioFile=null;
let preparedAudioObjectUrl='';

function preparedAudioSizeText(file){
  const kb=Math.round((file.size||0)/1024);
  return kb+' KB';
}

function clearPreparedAudioChoice(){
  preparedAudioFile=null;
  const manual=$('recordingFile');
  if(manual)manual.value='';
}

function playPreparedAudio(file){
  if(!file){showMsg('Audio inválido.','error');return}
  try{
    if(preparedAudioObjectUrl)URL.revokeObjectURL(preparedAudioObjectUrl);
    preparedAudioObjectUrl=URL.createObjectURL(file);
    const audio=new Audio(preparedAudioObjectUrl);
    audio.play().then(()=>{
      showMsg('Reproduciendo audio preparado');
    }).catch(()=>{
      showMsg('Este navegador no pudo reproducir este OGG. Convertí nuevamente el audio con el conversor de XiaoZhi Care.','error');
    });
  }catch(e){
    showMsg('No se pudo reproducir el audio preparado.','error');
  }
}

function usePreparedAudio(file){
  if(!file){showMsg('Audio inválido.','error');return}
  preparedAudioFile=file;
  const manual=$('recordingFile');
  if(manual)manual.value='';
  const label=$('recordingLabel');
  if(label && !label.value){
    label.value=file.name.replace(/\.[^.]+$/,'').replaceAll('_',' ');
  }
  showMsg('Audio preparado seleccionado: '+file.name);
}

function renderPreparedAudioList(){
  const total=(preparedAudioFiles||[]).length||0;
  const summary=$('preparedAudioSummary');
  const count=$('preparedAudioCount');
  if(summary)summary.textContent='AUDIOS DISPONIBLES: '+total;
  if(count)count.textContent=String(total);
}

function setupPreparedAudioFolder(){
  const input=$('preparedAudioFolder');
  if(!input)return;
  input.addEventListener('change',()=>{
    clearPreparedAudioChoice();
    preparedAudioFiles=[...(input.files||[])].filter(f=>/\.ogg$/i.test(f.name));
    renderPreparedAudioList();
    if(!preparedAudioFiles.length){
      showMsg('No encontré archivos .ogg en esa carpeta.','error');
    }else{
      showMsg('Audios disponibles encontrados: '+preparedAudioFiles.length);
    }
  });

  const manual=$('recordingFile');
  if(manual){
    manual.addEventListener('change',()=>{
      preparedAudioFile=null;
    });
  }

  renderPreparedAudioList();
}
setupPreparedAudioFolder();


let uploadedRecordingAudio=null;

function playUploadedRecording(id){
  if(!id){
    showMsg('Audio inválido.','error');
    return;
  }

  try{
    if(uploadedRecordingAudio){
      uploadedRecordingAudio.pause();
      uploadedRecordingAudio=null;
    }

    uploadedRecordingAudio = new Audio('/api/voice-recordings/' + encodeURIComponent(id) + '/audio');
    uploadedRecordingAudio.play().then(()=>{
      showMsg('Reproduciendo audio cargado desde XiaoZhi Care');
    }).catch(()=>{
      showMsg('El navegador no pudo reproducir el audio cargado desde XiaoZhi Care.','error');
    });
  }catch(e){
    showMsg('No se pudo iniciar la reproducción del audio cargado.','error');
  }
}

function renderRecordings(){
  const box=$('recordingsList');if(!box)return;
  $('recordingsCount').textContent=(recordings||[]).length+' / '+(recordingLimits.max_recordings||12);
  updateRecordingReminderOptions();
  if(!recordings.length)return empty(box,'No hay audios cargados todavía.');
  box.textContent='';
  for(const r of recordings){
    const item=document.createElement('div');item.className='item';
    const name=document.createElement('div');name.className='itemName';name.textContent=r.label||r.id;
    const meta=document.createElement('div');meta.className='itemMeta';
    meta.textContent=[r.id,Math.round((r.duration_ms||0)/100)/10+' s',Math.round((r.size_bytes||0)/1024)+' KB',r.reminder_id?voiceTargetTitle(r.reminder_id):'Sin asignar'].join(' · ');
    item.append(name,meta);
    if(r.text){const notes=document.createElement('div');notes.className='itemNotes';notes.textContent=r.text;item.appendChild(notes)}
    const lab=document.createElement('label');lab.textContent='Recordatorio asignado';
    const sel=document.createElement('select');fillReminderSelect(sel,r.reminder_id,r.id,false);
    lab.appendChild(sel);item.appendChild(lab);
    const acts=document.createElement('div');acts.className='actions';
    acts.appendChild(actionButton('Escuchar cargado','secondary',()=>playUploadedRecording(r.id)));
    acts.appendChild(actionButton('Guardar asignación','secondary',async()=>{try{await api('/api/voice-recordings/'+r.id,{method:'PUT',body:{reminder_id:sel.value},csrfRequired:true});showMsg(sel.value?'Audio asignado':'Audio desasignado');await loadRecordings();await loadMaintenance()}catch(e){showMsg(readableError(e.message),'error')}}));
    const del=actionButton('Eliminar','danger',async()=>{if(!confirm('¿Eliminar este audio?'+(r.reminder_id?' Se desasignará primero.':'')))return;try{if(r.reminder_id)await api('/api/voice-recordings/'+r.id,{method:'PUT',body:{reminder_id:''},csrfRequired:true});await api('/api/voice-recordings/'+r.id,{method:'DELETE',csrfRequired:true});showMsg('Audio eliminado');await loadRecordings();await loadMaintenance()}catch(e){showMsg(readableError(e.message),'error')}});
    acts.appendChild(del);item.appendChild(acts);box.appendChild(item);
  }
  renderReminders();
}

async function loadRecordings(){try{const j=await api('/api/voice-recordings');recordings=j.data||[];recordingLimits=j.limits||{};renderRecordings()}catch(e){recordings=[];recordingLimits={};renderRecordings();showMsg('No se pudieron leer los audios: '+readableError(e.message),'error')}}
function fileToBase64(file){return new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>{const s=String(r.result||'');resolve(s.includes(',')?s.split(',')[1]:s)};r.onerror=()=>reject(new Error('No se pudo leer el archivo'));r.readAsDataURL(file)})}

function clearRecordingPreview(){
  const audio=$('recordingPreview'),info=$('recordingPreviewInfo'),btn=$('recordingLocalPlayBtn');
  if(audio){audio.pause();audio.removeAttribute('src');audio.load();audio.classList.add('hidden')}
  if(info)info.textContent='';
  if(btn)btn.disabled=true;
}
function setupRecordingPreview(){
  const input=$('recordingFile'),audio=$('recordingPreview'),info=$('recordingPreviewInfo'),btn=$('recordingLocalPlayBtn');
  if(!input||!audio)return;

  if(btn){
    btn.onclick=async()=>{
      if(!audio.src){showMsg('Primero elegí un archivo de audio.','error');return}
      try{
        audio.currentTime=0;
        await audio.play();
      }catch(e){
        showMsg('Este navegador no pudo reproducir este OGG. Probá otro OGG/Opus o verificá el archivo en el celular/PC.','error');
      }
    };
  }

  input.addEventListener('change',()=>{
    clearRecordingPreview();
    const f=input.files&&input.files[0];
    if(!f)return;

    const kb=Math.round(f.size/1024);
    const reader=new FileReader();

    reader.onload=()=>{
      audio.src=reader.result;
      audio.load();
      audio.classList.remove('hidden');
      if(btn)btn.disabled=false;
      if(info)info.textContent='Después de cargarlo, verificá el audio desde Audios asignados con > Escuchar cargado. Tamaño: '+kb+' KB.';
    };

    reader.onerror=()=>{
      if(info)info.textContent='No se pudo preparar la vista previa local.';
      showMsg('No se pudo leer el archivo para escucharlo.','error');
    };

    reader.readAsDataURL(f);
  });
}
setupRecordingPreview();

if($('recordingForm'))$('recordingForm').addEventListener('submit',async e=>{e.preventDefault();const f=preparedAudioFile || (($('recordingFile').files&&$('recordingFile').files[0])?$('recordingFile').files[0]:null);const reminderId=$('recordingReminder').value;if(!reminderId)return showMsg('Seleccioná un recordatorio.','error');if(!f)return showMsg('Elegí un archivo OGG / Opus o usá uno desde Audios disponibles.','error');const max=recordingLimits.max_file_bytes||32768;if(f.size>max)return showMsg('El audio supera '+Math.round(max/1024)+' KB.','error');try{const content_base64=await fileToBase64(f);await api('/api/voice-recordings',{method:'POST',body:{label:$('recordingLabel').value.trim(),text:$('recordingText').value.trim(),reminder_id:reminderId,filename:f.name,content_base64},csrfRequired:true});$('recordingForm').reset();clearRecordingPreview();showMsg('Audio cargado y asignado');await loadRecordings();await loadMaintenance()}catch(err){showMsg(readableError(err.message),'error')}});


if($('cleanDuplicatesBtn'))$('cleanDuplicatesBtn').onclick=cleanCareDuplicates;

async function cleanCareDuplicates(){
  try{
    await Promise.all([loadPeople(),loadPreferences(),loadFamily(),loadRoutines(),loadPillbox(),loadReminders()]);

    function cleanKey(v){
      return String(v||'').trim().toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g,'').replace(/\s+/g,' ');
    }

    function personKey(p){
      const name=cleanKey(p.name);
      const nick=cleanKey(p.nickname);
      const birth=String(p.birthday||'').trim();
      const phone=cleanKey(p.phone);
      if(name||nick||birth)return [name,nick,birth].join('|');
      return phone?'phone|'+phone:'';
    }

    const keep={};
    const replace={};
    let duplicates=0;

    for(const p of people){
      const k=personKey(p);
      if(!k)continue;
      if(!keep[k])keep[k]=p;
      else{
        replace[p.id]=keep[k].id;
        duplicates++;
      }
    }

    if(!duplicates){
      showMsg('No encontre personas duplicadas.');
      return;
    }

    if(!confirm('Se encontraron '+duplicates+' persona(s) duplicada(s). Se intentara mover sus vinculos y borrar las copias. Continuar?'))return;

    let updated=0;
    let deleted=0;
    let skipped=0;

    for(const pref of preferences){
      if(pref.owner_id&&replace[pref.owner_id]){
        const body={...pref,owner_id:replace[pref.owner_id]};
        delete body.id;
        try{
          await api('/api/preferences/'+pref.id,{method:'PUT',body,csrfRequired:true});
          updated++;
        }catch(e){}
      }
    }

    for(const rem of reminders){
      if(rem.related_person_id&&replace[rem.related_person_id]){
        const body={...rem,related_person_id:replace[rem.related_person_id]};
        delete body.id;
        try{
          await api('/api/reminders/'+rem.id,{method:'PUT',body,csrfRequired:true});
          updated++;
        }catch(e){}
      }
    }

    for(const rel of family){
      let changed=false;
      const body={...rel};

      if(body.from_person_id&&replace[body.from_person_id]){
        body.from_person_id=replace[body.from_person_id];
        changed=true;
      }

      if(body.to_person_id&&replace[body.to_person_id]){
        body.to_person_id=replace[body.to_person_id];
        changed=true;
      }

      delete body.id;

      if(changed&&body.from_person_id&&body.to_person_id&&body.from_person_id!==body.to_person_id){
        try{
          await api('/api/family/'+rel.id,{method:'PUT',body,csrfRequired:true});
          updated++;
        }catch(e){}
      }
    }

    for(const oldId of Object.keys(replace)){
      try{
        await api('/api/people/'+oldId,{method:'DELETE',csrfRequired:true});
        deleted++;
      }catch(e){
        skipped++;
      }
    }

    await Promise.all([loadPeople(),loadPreferences(),loadFamily(),loadRoutines(),loadPillbox(),loadReminders(),loadMaintenance()]);
    

    showMsg('Limpieza terminada: '+deleted+' persona(s) duplicada(s) eliminada(s), '+updated+' vinculo(s) actualizado(s)'+(skipped?'. No se pudieron borrar '+skipped+'.':'.'));
  }catch(e){
    showMsg('No se pudo limpiar duplicados: '+readableError(e.message),'error');
  }
}

// Mantenimiento
function usageTitle(key){return {people:'Personas',preferences:'Preferencias',legacy_pillbox:'Pastillero legacy',reminders:'Recordatorios',routines:'Cuidados / cosas para hacer',executions:'Historial de cosas hechas',family:'Relaciones familiares',voice_recordings:'Audios de recordatorios'}[key]||key}
function renderMaintenance(){const box=$('maintenanceList');if(!box)return;const data=maintenance||{};const usage=data.usage||{};const keys=['people','preferences','family','routines','executions','legacy_pillbox','reminders','voice_recordings'];box.textContent='';for(const k of keys){const u=usage[k]||{};const max=u.max||0, used=u.used||0;const item=document.createElement('div');item.className='item';const name=document.createElement('div');name.className='itemName';name.textContent=usageTitle(k);const meta=document.createElement('div');meta.className='itemMeta';meta.textContent=max?(used+' / '+max):String(used);item.append(name,meta);if(u.near_limit){const note=document.createElement('div');note.className='itemNotes';note.textContent='Atención: cerca del límite configurado.';item.appendChild(note)}box.appendChild(item)}
const n=data.nvs||{};if(n.available){$('maintenanceNvs').textContent='NVS: '+n.used_entries+' entradas usadas, '+n.free_entries+' libres, '+n.total_entries+' totales. Namespaces: '+n.namespace_count+(n.low_space?'\nAtención: queda poco espacio libre en NVS.':'');}else{$('maintenanceNvs').textContent='NVS: no se pudo leer el estado'+(n.error?': '+n.error:'');}
$('maintenanceCount').textContent='Mantenimiento';}
async function loadMaintenance(){try{const j=await api('/api/maintenance/status');maintenance=j.data||{};renderMaintenance()}catch(e){showMsg('No se pudo leer mantenimiento: '+readableError(e.message),'error')}}

function downloadJsonFile(filename,obj){
  const blob=new Blob([JSON.stringify(obj,null,2)],{type:'application/json;charset=utf-8'});
  const url=URL.createObjectURL(blob);
  const a=document.createElement('a');
  a.href=url;
  a.download=filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(()=>URL.revokeObjectURL(url),1000);
}

async function downloadFullBackup(){
  const st=$('fullBackupStatus');
  if(st)st.textContent='Preparando backup completo...';
  try{
    const [profileRes,peopleRes,preferencesRes,familyRes,remindersRes,routinesRes,pillboxRes]=await Promise.all([
      api('/api/profile'),
      api('/api/people'),
      api('/api/preferences'),
      api('/api/family'),
      api('/api/reminders'),
      api('/api/routines'),
      api('/api/pillbox')
    ]);
    const now=new Date();
    const stamp=now.toISOString().replace(/[:.]/g,'-');
    const backup={
      schema:'xiaozhi-care-backup',
      version:1,
      created_at:now.toISOString(),
      app:'XiaoZhi Care',
      source:'web-panel',
      includes:['profile','people','preferences','family','reminders','routines','legacy_pillbox'],
      data:{
        profile:profileRes.data||{},
        people:peopleRes.data||[],
        preferences:preferencesRes.data||[],
        family:familyRes.data||[],
        reminders:remindersRes.data||[],
        routines:routinesRes.data||[],
        legacy_pillbox:pillboxRes.data||[]
      }
    };
    downloadJsonFile('xiaozhi-care-backup-'+stamp+'.json',backup);
    if(st)st.textContent='Backup completo descargado correctamente.';
    showMsg('Backup completo descargado');
  }catch(e){
    if(st)st.textContent='No se pudo descargar el backup: '+readableError(e.message);
    showMsg('No se pudo descargar el backup: '+readableError(e.message),'error');
  }
}

let selectedFullBackup=null;

function getBackupDataObject(raw){
  if(!raw||typeof raw!=='object')throw new Error('El archivo no contiene un JSON válido.');
  if(raw.schema!=='xiaozhi-care-backup')throw new Error('El archivo no parece ser un backup de XiaoZhi Care.');
  if(!raw.data||typeof raw.data!=='object')throw new Error('El backup no contiene el bloque data.');
  const d=raw.data;
  const required=['profile','people','preferences','family','reminders','routines'];
  for(const k of required){
    if(typeof d[k]==='undefined')throw new Error('Falta el bloque '+k+' en el backup.');
  }
  if(!Array.isArray(d.people))throw new Error('people debe ser una lista.');
  if(!Array.isArray(d.preferences))throw new Error('preferences debe ser una lista.');
  if(!Array.isArray(d.family))throw new Error('family debe ser una lista.');
  if(!Array.isArray(d.reminders))throw new Error('reminders debe ser una lista.');
  if(!Array.isArray(d.routines))throw new Error('routines debe ser una lista.');
  return d;
}

function backupSummaryText(d){
  return 'Backup válido.\n'+
    'Perfil: '+((d.profile&&d.profile.name)?d.profile.name:'sin nombre')+'\n'+
    'Personas: '+d.people.length+'\n'+
    'Preferencias: '+d.preferences.length+'\n'+
    'Familia / vínculos: '+d.family.length+'\n'+
    'Recordatorios: '+d.reminders.length+'\n'+
    'Rutinas / pastillero / cuidados: '+d.routines.length+'\n'+
    'Audios: no se restauran en esta versión.';
}

async function readSelectedFullBackup(){
  const input=$('fullBackupFile');
  if(!input||!input.files||!input.files[0])throw new Error('Seleccioná un archivo JSON de backup.');
  const txt=await input.files[0].text();
  const raw=JSON.parse(txt);
  const data=getBackupDataObject(raw);
  selectedFullBackup={raw,data};
  return data;
}

async function validateFullBackup(){
  const st=$('fullBackupStatus');
  try{
    const data=await readSelectedFullBackup();
    if(st)st.textContent=backupSummaryText(data);
    showMsg('Backup validado correctamente');
  }catch(e){
    selectedFullBackup=null;
    if(st)st.textContent='Backup inválido: '+readableError(e.message);
    showMsg('Backup inválido: '+readableError(e.message),'error');
  }
}

async function deleteListById(endpoint,items){
  for(const item of items){
    if(item&&item.id){
      await api(endpoint+'/'+encodeURIComponent(item.id),{method:'DELETE',csrfRequired:true});
    }
  }
}

function cloneWithoutId(obj){
  const out=Object.assign({},obj||{});
  delete out.id;
  return out;
}

function mapOwnerId(id,personMap){
  if(!id)return id;
  if(id==='profile')return 'profile';
  return personMap[id]||id;
}

async function restoreFullBackup(){
  const st=$('fullBackupStatus');
  try{
    const data=selectedFullBackup?selectedFullBackup.data:await readSelectedFullBackup();

    const msg=backupSummaryText(data)+'\n\nEsta acción reemplazará los datos actuales de XiaoZhi Care. No restaura audios. ¿Continuar?';
    if(!confirm(msg))return;

    if(st)st.textContent='Restaurando backup... No cierres esta página.';

    const current=await Promise.all([
      api('/api/family'),
      api('/api/reminders'),
      api('/api/routines'),
      api('/api/pillbox'),
      api('/api/preferences'),
      api('/api/people')
    ]);

    const curFamily=current[0].data||[];
    const curReminders=current[1].data||[];
    const curRoutines=current[2].data||[];
    const curPillbox=current[3].data||[];
    const curPreferences=current[4].data||[];
    const curPeople=current[5].data||[];

    if(st)st.textContent='Borrando datos actuales...';

    await deleteListById('/api/family',curFamily);
    await deleteListById('/api/reminders',curReminders);
    await deleteListById('/api/routines',curRoutines);
    await deleteListById('/api/pillbox',curPillbox);
    await deleteListById('/api/preferences',curPreferences);
    await deleteListById('/api/people',curPeople);

    if(st)st.textContent='Restaurando perfil...';
    if(data.profile&&data.profile.name){
      await api('/api/profile',{method:'PUT',body:cloneWithoutId(data.profile),csrfRequired:true});
    }

    const personMap={};

    if(st)st.textContent='Restaurando personas...';
    for(const p of data.people){
      const created=await api('/api/people',{method:'POST',body:cloneWithoutId(p),csrfRequired:true});
      if(p.id&&created&&created.data&&created.data.id)personMap[p.id]=created.data.id;
    }

    if(st)st.textContent='Restaurando preferencias...';
    for(const pref of data.preferences){
      const body=cloneWithoutId(pref);
      body.owner_id=mapOwnerId(body.owner_id,personMap);
      await api('/api/preferences',{method:'POST',body,csrfRequired:true});
    }

    if(st)st.textContent='Restaurando familia y vínculos...';
    for(const f of data.family){
      const body=cloneWithoutId(f);
      body.from_person_id=mapOwnerId(body.from_person_id,personMap);
      body.to_person_id=mapOwnerId(body.to_person_id,personMap);
      await api('/api/family',{method:'POST',body,csrfRequired:true});
    }

    if(st)st.textContent='Restaurando rutinas, pastillero y cuidados...';
    for(const r of data.routines){
      await api('/api/routines',{method:'POST',body:cloneWithoutId(r),csrfRequired:true});
    }

    if(st)st.textContent='Restaurando recordatorios...';
    for(const r of data.reminders){
      const body=cloneWithoutId(r);
      body.related_person_id=mapOwnerId(body.related_person_id,personMap);
      await api('/api/reminders',{method:'POST',body,csrfRequired:true});
    }

    await Promise.all([loadPeople(),loadProfile(),loadPreferences(),loadRoutines(),loadPillbox(),loadReminders(),loadFamily(),loadMaintenance(),loadRecordings()]);
    setupPeopleFamilyUi();
    updatePersonFamilyOptions();

    if(st)st.textContent='Backup restaurado correctamente. Audios no restaurados.';
    showMsg('Backup restaurado correctamente');
  }catch(e){
    if(st)st.textContent='Error al restaurar backup: '+readableError(e.message);
    showMsg('Error al restaurar backup: '+readableError(e.message),'error');
  }
}
async function clearExecutions(){if(!confirm('¿Borrar el historial de cosas hechas/confirmadas? No borra las cosas para hacer.'))return;try{const j=await api('/api/maintenance/clear-executions',{method:'POST',body:{confirm:true},csrfRequired:true});showMsg('Historial borrado: '+((j.data&&j.data.deleted)||0)+' evento(s)');await loadMaintenance()}catch(e){showMsg(readableError(e.message),'error')}}
async function clearLegacyPillbox(){if(!confirm('¿Borrar entradas viejas del pastillero legacy? No borra medicaciones cargadas en Pastillero.'))return;try{const j=await api('/api/maintenance/clear-legacy-pillbox',{method:'POST',body:{confirm:true},csrfRequired:true});showMsg('Pastillero legacy borrado: '+((j.data&&j.data.deleted)||0)+' entrada(s)');await loadMaintenance()}catch(e){showMsg(readableError(e.message),'error')}}
if($('maintenanceRefresh'))$('maintenanceRefresh').onclick=loadMaintenance;
if($('fullBackupDownload'))$('fullBackupDownload').onclick=downloadFullBackup;
if($('fullBackupValidate'))$('fullBackupValidate').onclick=validateFullBackup;
if($('fullBackupRestore'))$('fullBackupRestore').onclick=restoreFullBackup;
if($('clearExecutionsBtn'))$('clearExecutionsBtn').onclick=clearExecutions;
if($('clearLegacyPillboxBtn'))$('clearLegacyPillboxBtn').onclick=clearLegacyPillbox;

refreshAuth();
</script>
</body>
</html>)HTML";

}  // namespace xiaozhi_care














