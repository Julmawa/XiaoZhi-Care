# XiaoZhi Care - shared installer/restorer language support
# Supported UI languages: Spanish, English, Portuguese

$script:XzLang = 'en'
$script:XzCulture = ''

$script:XzText = @{
    warning_prefix = @{ es='AVISO'; en='WARNING'; pt='AVISO' }
    detected_language = @{ es='Idioma de Windows detectado'; en='Detected Windows language'; pt='Idioma do Windows detectado' }
    language_prompt = @{ es='Presione ENTER para continuar con {0}, o elija 1/2/3'; en='Press ENTER to continue in {0}, or choose 1/2/3'; pt='Pressione ENTER para continuar em {0}, ou escolha 1/2/3' }
    invalid_option = @{ es='Opcion no valida.'; en='Invalid option.'; pt='Opcao invalida.' }
    yes_no = @{ es='S/N'; en='Y/N'; pt='S/N' }
    progress_processing = @{ es='Procesando memoria Flash'; en='Processing Flash memory'; pt='Processando memoria Flash' }
    progress_reading = @{ es='Leyendo memoria Flash'; en='Reading Flash memory'; pt='Lendo memoria Flash' }
    progress_writing = @{ es='Grabando memoria Flash'; en='Writing Flash memory'; pt='Gravando memoria Flash' }
    progress_done = @{ es='{0}% completado'; en='{0}% complete'; pt='{0}% concluido' }
    esptool_code = @{ es='esptool termino con codigo {0}'; en='esptool exited with code {0}'; pt='esptool terminou com codigo {0}' }

    installer_title = @{ es='XIAOZHI CARE - INSTALADOR V1'; en='XIAOZHI CARE - INSTALLER V1'; pt='XIAOZHI CARE - INSTALADOR V1' }
    assisted_install = @{ es='Instalacion asistida para usuarios sin entorno de desarrollo.'; en='Assisted installation for users without a development environment.'; pt='Instalacao assistida para usuarios sem ambiente de desenvolvimento.' }
    compatibility = @{ es='COMPATIBILIDAD DE ESTA VERSION'; en='COMPATIBILITY OF THIS VERSION'; pt='COMPATIBILIDADE DESTA VERSAO' }
    compat_base = @{ es='  - XiaoZhi previamente instalado y funcionando'; en='  - XiaoZhi already installed and working'; pt='  - XiaoZhi previamente instalado e funcionando' }
    compat_profile = @{ es='  - Perfil soportado por esta release (se verificara antes de grabar)'; en='  - Hardware profile supported by this release (verified before flashing)'; pt='  - Perfil de hardware suportado por esta release (verificado antes da gravacao)' }
    warn_preserve_nvs = @{ es='Este instalador NO borra toda la memoria y conserva la NVS de XiaoZhi.'; en='This installer DOES NOT erase the whole Flash and preserves XiaoZhi NVS.'; pt='Este instalador NAO apaga toda a memoria Flash e preserva a NVS do XiaoZhi.' }
    warn_backup_first = @{ es='Antes de modificar la placa crea un respaldo completo de 16 MB.'; en='Before modifying the board, it creates a full 16 MB backup.'; pt='Antes de modificar a placa, ele cria um backup completo de 16 MB.' }
    start_verification = @{ es='¿Desea comenzar la verificacion?'; en='Do you want to start verification?'; pt='Deseja iniciar a verificacao?' }
    no_extra_write = @{ es='No se realizo ninguna escritura adicional en la placa.'; en='No additional writes were made to the board.'; pt='Nenhuma gravacao adicional foi feita na placa.' }

    title_verify_package = @{ es='1. VERIFICANDO PAQUETE'; en='1. VERIFYING PACKAGE'; pt='1. VERIFICANDO PACOTE' }
    care_version = @{ es='XiaoZhi Care: {0}'; en='XiaoZhi Care: {0}'; pt='XiaoZhi Care: {0}' }
    base_version = @{ es='Base XiaoZhi compatible: {0}'; en='Compatible XiaoZhi base: {0}'; pt='Base XiaoZhi compativel: {0}' }
    profile = @{ es='Perfil: {0}'; en='Profile: {0}'; pt='Perfil: {0}' }
    release_missing = @{ es='Falta release.json. Este paquete todavia no fue preparado para distribucion.'; en='release.json is missing. This package has not been prepared for distribution yet.'; pt='release.json esta ausente. Este pacote ainda nao foi preparado para distribuicao.' }
    release_fw_missing = @{ es='release.json no contiene la lista firmware.'; en='release.json does not contain the firmware list.'; pt='release.json nao contem a lista de firmware.' }
    firmware_missing = @{ es='Falta el archivo de firmware: {0}'; en='Firmware file is missing: {0}'; pt='Arquivo de firmware ausente: {0}' }
    firmware_hash_bad = @{ es='SHA-256 incorrecto en {0}. El paquete puede estar dañado.'; en='Incorrect SHA-256 for {0}. The package may be damaged.'; pt='SHA-256 incorreto em {0}. O pacote pode estar danificado.' }
    firmware_integrity_ok = @{ es='Integridad del paquete de firmware verificada.'; en='Firmware package integrity verified.'; pt='Integridade do pacote de firmware verificada.' }
    flasher_found = @{ es='Grabador encontrado: {0}'; en='Flasher found: {0}'; pt='Gravador encontrado: {0}' }
    flasher_download = @{ es='No se encontro el grabador. Se descargara esptool oficial de Espressif.'; en='Flasher not found. Official Espressif esptool will be downloaded.'; pt='Gravador nao encontrado. O esptool oficial da Espressif sera baixado.' }
    flasher_download_fail = @{ es='No se pudo descargar esptool. Verifique Internet. Detalle: {0}'; en='Could not download esptool. Check your Internet connection. Details: {0}'; pt='Nao foi possivel baixar o esptool. Verifique a Internet. Detalhe: {0}' }
    flasher_hash_bad = @{ es='La firma SHA-256 del grabador descargado no coincide. Se cancela por seguridad.'; en='The downloaded flasher SHA-256 does not match. Cancelling for safety.'; pt='O SHA-256 do gravador baixado nao confere. Cancelando por seguranca.' }
    flasher_hash_ok = @{ es='Descarga de esptool verificada por SHA-256.'; en='esptool download verified by SHA-256.'; pt='Download do esptool verificado por SHA-256.' }
    flasher_exe_missing = @{ es='El paquete de esptool no contiene esptool.exe.'; en='The esptool package does not contain esptool.exe.'; pt='O pacote do esptool nao contem esptool.exe.' }
    esptool_ready = @{ es='esptool v{0} listo.'; en='esptool v{0} ready.'; pt='esptool v{0} pronto.' }

    title_detect_board = @{ es='2. DETECTANDO PLACA'; en='2. DETECTING BOARD'; pt='2. DETECTANDO PLACA' }
    no_com = @{ es='No se encontro ningun puerto COM. Conecte la ESP32-S3 por USB.'; en='No COM port was found. Connect the ESP32-S3 by USB.'; pt='Nenhuma porta COM foi encontrada. Conecte a ESP32-S3 por USB.' }
    port_detected = @{ es='Puerto detectado: {0}'; en='Port detected: {0}'; pt='Porta detectada: {0}' }
    multiple_ports = @{ es='Se encontraron varios puertos:'; en='Multiple ports were found:'; pt='Foram encontradas varias portas:' }
    choose_port = @{ es='Elija el numero correspondiente a la ESP32-S3'; en='Choose the number corresponding to the ESP32-S3'; pt='Escolha o numero correspondente a ESP32-S3' }
    connecting = @{ es='Conectando a {0} ...'; en='Connecting to {0} ...'; pt='Conectando a {0} ...' }
    connect_fail = @{ es='No se pudo comunicar con la placa en {0}. Mantenga BOOT presionado al conectar si fuera necesario.'; en='Could not communicate with the board on {0}. Hold BOOT while connecting if necessary.'; pt='Nao foi possivel comunicar com a placa em {0}. Mantenha BOOT pressionado ao conectar, se necessario.' }
    wrong_chip = @{ es='La placa conectada NO es una ESP32-S3. Esta release no es compatible.'; en='The connected board is NOT an ESP32-S3. This release is not compatible.'; pt='A placa conectada NAO e uma ESP32-S3. Esta release nao e compativel.' }
    chip_ok = @{ es='Chip: ESP32-S3.'; en='Chip: ESP32-S3.'; pt='Chip: ESP32-S3.' }
    psram_fail = @{ es='No se pudo confirmar PSRAM integrada de 8 MB (R8). Esta V1 exige ESP32-S3 N16R8.'; en='Could not confirm 8 MB embedded PSRAM (R8). V1 requires ESP32-S3 N16R8.'; pt='Nao foi possivel confirmar PSRAM integrada de 8 MB (R8). A V1 exige ESP32-S3 N16R8.' }
    psram_ok = @{ es='PSRAM: 8 MB detectada.'; en='PSRAM: 8 MB detected.'; pt='PSRAM: 8 MB detectada.' }
    flash_fail = @{ es='La Flash no fue identificada como 16 MB.'; en='Flash was not identified as 16 MB.'; pt='A Flash nao foi identificada como 16 MB.' }
    flash_detected = @{ es='Flash: 16 MB detectada.'; en='Flash: 16 MB detected.'; pt='Flash: 16 MB detectada.' }
    security_query_fail = @{ es='No se pudo consultar el estado de seguridad de la ESP32-S3.'; en='Could not query the ESP32-S3 security state.'; pt='Nao foi possivel consultar o estado de seguranca da ESP32-S3.' }
    security_fail = @{ es='No se pudo confirmar Secure Boot y Flash Encryption desactivados. La V1 solo instala en placas sin proteccion de Flash.'; en='Could not confirm that Secure Boot and Flash Encryption are disabled. V1 only installs on boards without Flash protection.'; pt='Nao foi possivel confirmar Secure Boot e Flash Encryption desativados. A V1 instala apenas em placas sem protecao da Flash.' }
    security_ok = @{ es='Secure Boot: desactivado. Flash Encryption: desactivado.'; en='Secure Boot: disabled. Flash Encryption: disabled.'; pt='Secure Boot: desativado. Flash Encryption: desativado.' }

    title_existing = @{ es='3. COMPROBACION DEL XIAOZHI EXISTENTE'; en='3. CHECKING EXISTING XIAOZHI'; pt='3. VERIFICANDO O XIAOZHI EXISTENTE' }
    existing_work = @{ es='Antes de continuar, el XiaoZhi actual debe funcionar correctamente.'; en='Before continuing, the current XiaoZhi installation must work correctly.'; pt='Antes de continuar, o XiaoZhi atual deve funcionar corretamente.' }
    hardware_not_detected = @{ es='La configuracion de display, microfono, audio y pines NO se detecta por el bootloader.'; en='Display, microphone, audio and pin configuration cannot be detected from the bootloader.'; pt='A configuracao de display, microfone, audio e pinos NAO pode ser detectada pelo bootloader.' }
    single_profile = @{ es='Esta release fue compilada para un unico perfil de hardware soportado.'; en='This release was compiled for one supported hardware profile.'; pt='Esta release foi compilada para um unico perfil de hardware suportado.' }
    hardware_worked = @{ es='¿Antes de ejecutar este instalador funcionaban correctamente pantalla, microfono y parlante?'; en='Before running this installer, were the display, microphone and speaker working correctly?'; pt='Antes de executar este instalador, tela, microfone e alto-falante funcionavam corretamente?' }
    fix_xiaozhi_first = @{ es='Primero debe dejar XiaoZhi funcionando correctamente con su hardware.'; en='First make sure XiaoZhi works correctly with your hardware.'; pt='Primeiro deixe o XiaoZhi funcionando corretamente com seu hardware.' }

    title_backup = @{ es='4. RESPALDO COMPLETO ANTES DE MODIFICAR'; en='4. FULL BACKUP BEFORE MODIFICATION'; pt='4. BACKUP COMPLETO ANTES DE MODIFICAR' }
    reading_backup = @{ es='Leyendo 16 MB de la placa. Este respaldo permite una recuperacion manual completa.'; en='Reading 16 MB from the board. This backup allows full manual recovery.'; pt='Lendo 16 MB da placa. Este backup permite uma recuperacao manual completa.' }
    show_progress = @{ es='Se mostrara el progreso real del grabador. No desconecte el cable USB.'; en='Real flasher progress will be shown. Do not disconnect the USB cable.'; pt='O progresso real do gravador sera exibido. Nao desconecte o cabo USB.' }
    backup_try = @{ es='Intentando respaldo a {0} baudios...'; en='Trying backup at {0} baud...'; pt='Tentando backup a {0} bauds...' }
    backup_ok = @{ es='Respaldo de 16 MB completado a {0} baudios.'; en='16 MB backup completed at {0} baud.'; pt='Backup de 16 MB concluido a {0} bauds.' }
    backup_retry = @{ es='El intento a {0} no completo el respaldo. Se reintentara a menor velocidad.'; en='The attempt at {0} did not complete the backup. Retrying at a lower speed.'; pt='A tentativa em {0} nao concluiu o backup. Nova tentativa em velocidade menor.' }
    last_detail = @{ es='Ultimo detalle: {0}'; en='Last detail: {0}'; pt='Ultimo detalhe: {0}' }
    diag_logs = @{ es='Logs de diagnostico: {0}'; en='Diagnostic logs: {0}'; pt='Logs de diagnostico: {0}' }
    backup_all_fail = @{ es='No se pudo crear el respaldo completo despues de tres intentos. No se instalara nada.'; en='Could not create the full backup after three attempts. Nothing will be installed.'; pt='Nao foi possivel criar o backup completo apos tres tentativas. Nada sera instalado.' }
    backup_created = @{ es='Respaldo completo creado: {0}'; en='Full backup created: {0}'; pt='Backup completo criado: {0}' }
    backup_private = @{ es='El respaldo puede contener Wi-Fi y datos privados. No lo comparta.'; en='The backup may contain Wi-Fi credentials and private data. Do not share it.'; pt='O backup pode conter credenciais Wi-Fi e dados privados. Nao o compartilhe.' }
    layout_unsupported = @{ es='La tabla de particiones no corresponde a XiaoZhi v2 de 16 MB ni a XiaoZhi Care. Se conserva el respaldo y se cancela.'; en='The partition table is neither XiaoZhi v2 16 MB nor XiaoZhi Care. The backup is kept and installation is cancelled.'; pt='A tabela de particoes nao corresponde ao XiaoZhi v2 de 16 MB nem ao XiaoZhi Care. O backup e preservado e a instalacao e cancelada.' }
    layout_stock = @{ es='Tabla detectada: XiaoZhi v2 oficial de 16 MB.'; en='Detected layout: official XiaoZhi v2 16 MB.'; pt='Tabela detectada: XiaoZhi v2 oficial de 16 MB.' }
    layout_care = @{ es='Tabla detectada: XiaoZhi Care (actualizacion/reinstalacion).'; en='Detected layout: XiaoZhi Care (update/reinstall).'; pt='Tabela detectada: XiaoZhi Care (atualizacao/reinstalacao).' }
    no_valid_ota = @{ es='No se encontro una aplicacion valida en las particiones OTA.'; en='No valid application was found in the OTA partitions.'; pt='Nenhum aplicativo valido foi encontrado nas particoes OTA.' }
    ota_ambiguous = @{ es='Hay una imagen XiaoZhi valida en otra OTA cuyo perfil no coincide con {0}. V1 cancela por seguridad.'; en='Another OTA contains a valid XiaoZhi image whose profile does not match {0}. V1 cancels for safety.'; pt='Outra OTA contem uma imagem XiaoZhi valida cujo perfil nao corresponde a {0}. A V1 cancela por seguranca.' }
    no_compatible = @{ es='No se pudo confirmar XiaoZhi compatible y perfil {0} en el firmware instalado.'; en='Could not confirm a compatible XiaoZhi installation with profile {0}.'; pt='Nao foi possivel confirmar um XiaoZhi compativel com o perfil {0} no firmware instalado.' }
    compatible_detected = @{ es='XiaoZhi compatible detectado. Perfil: {0}'; en='Compatible XiaoZhi detected. Profile: {0}'; pt='XiaoZhi compativel detectado. Perfil: {0}' }

    title_ready = @{ es='5. LISTO PARA INSTALAR'; en='5. READY TO INSTALL'; pt='5. PRONTO PARA INSTALAR' }
    version_care = @{ es='Version Care: {0}'; en='Care version: {0}'; pt='Versao Care: {0}' }
    profile_preserved = @{ es='Perfil preservado: {0}'; en='Preserved profile: {0}'; pt='Perfil preservado: {0}' }
    port_line = @{ es='Puerto: {0}'; en='Port: {0}'; pt='Porta: {0}' }
    backup_line = @{ es='Respaldo: {0}'; en='Backup: {0}'; pt='Backup: {0}' }
    install_summary = @{ es='La instalacion:'; en='Installation:'; pt='A instalacao:' }
    install_no_erase = @{ es='  - NO ejecuta erase-flash.'; en='  - DOES NOT run erase-flash.'; pt='  - NAO executa erase-flash.' }
    install_no_nvs = @{ es='  - NO borra la particion NVS (Wi-Fi/configuracion de XiaoZhi).'; en='  - DOES NOT erase the NVS partition (Wi-Fi/XiaoZhi configuration).'; pt='  - NAO apaga a particao NVS (Wi-Fi/configuracao do XiaoZhi).' }
    install_keep_care = @{ es='  - Conserva los datos Care si ya estaban instalados.'; en='  - Preserves Care data if it was already installed.'; pt='  - Preserva os dados Care se ja estavam instalados.' }
    install_new_areas = @{ es='  - Si viene de XiaoZhi oficial, crea areas nuevas care_data y voice.'; en='  - When migrating from official XiaoZhi, creates new care_data and voice areas.'; pt='  - Ao migrar do XiaoZhi oficial, cria novas areas care_data e voice.' }
    confirm_install = @{ es='¿CONFIRMA instalar XiaoZhi Care ahora?'; en='CONFIRM installing XiaoZhi Care now?'; pt='CONFIRMA instalar o XiaoZhi Care agora?' }
    install_cancelled = @{ es='Instalacion cancelada por el usuario.'; en='Installation cancelled by the user.'; pt='Instalacao cancelada pelo usuario.' }
    preparing_care = @{ es='Preparando los 2 MB nuevos de XiaoZhi Care...'; en='Preparing the new 2 MB XiaoZhi Care areas...'; pt='Preparando os novos 2 MB do XiaoZhi Care...' }
    flashing_care = @{ es='Grabando XiaoZhi Care...'; en='Flashing XiaoZhi Care...'; pt='Gravando XiaoZhi Care...' }
    flashing_progress = @{ es='Se mostrara el progreso real. Si una velocidad falla, el instalador reintentara a una menor.'; en='Real progress will be shown. If one speed fails, the installer will retry at a lower speed.'; pt='O progresso real sera exibido. Se uma velocidade falhar, o instalador tentara novamente em uma velocidade menor.' }
    flash_try = @{ es='Intentando grabacion a {0} baudios...'; en='Trying flash at {0} baud...'; pt='Tentando gravacao a {0} bauds...' }
    flash_ok = @{ es='Grabacion completada a {0} baudios.'; en='Flashing completed at {0} baud.'; pt='Gravacao concluida a {0} bauds.' }
    flash_retry = @{ es='La grabacion a {0} fallo. Se reintentara completa a menor velocidad.'; en='Flashing at {0} failed. Retrying the full flash at a lower speed.'; pt='A gravacao em {0} falhou. A gravacao completa sera tentada novamente em velocidade menor.' }
    flash_failed = @{ es='La grabacion no termino correctamente.'; en='Flashing did not complete correctly.'; pt='A gravacao nao terminou corretamente.' }
    backup_is = @{ es='El respaldo completo esta en: {0}'; en='The full backup is located at: {0}'; pt='O backup completo esta em: {0}' }
    preserve_backup = @{ es='No intente borrar la placa. Conserve esa carpeta para recuperacion.'; en='Do not erase the board. Keep that folder for recovery.'; pt='Nao tente apagar a placa. Guarde essa pasta para recuperacao.' }

    title_installed = @{ es='INSTALACION COMPLETADA'; en='INSTALLATION COMPLETED'; pt='INSTALACAO CONCLUIDA' }
    care_written = @{ es='XiaoZhi Care fue grabado correctamente.'; en='XiaoZhi Care was flashed successfully.'; pt='XiaoZhi Care foi gravado com sucesso.' }
    nvs_preserved = @{ es='La NVS original de XiaoZhi no fue borrada.'; en='The original XiaoZhi NVS was not erased.'; pt='A NVS original do XiaoZhi nao foi apagada.' }
    security_backup = @{ es='Respaldo de seguridad: {0}'; en='Safety backup: {0}'; pt='Backup de seguranca: {0}' }
    first_boot_1 = @{ es='En el primer arranque, care_data y voice pueden formatearse automaticamente'; en='On first boot, care_data and voice may be formatted automatically'; pt='Na primeira inicializacao, care_data e voice podem ser formatados automaticamente' }
    first_boot_2 = @{ es='si esta es la primera instalacion de XiaoZhi Care.'; en='if this is the first XiaoZhi Care installation.'; pt='se esta for a primeira instalacao do XiaoZhi Care.' }
    check = @{ es='Compruebe:'; en='Check:'; pt='Verifique:' }
    check_1 = @{ es='  1. que XiaoZhi inicia normalmente;'; en='  1. XiaoZhi starts normally;'; pt='  1. o XiaoZhi inicia normalmente;' }
    check_2 = @{ es='  2. pantalla, microfono y parlante;'; en='  2. display, microphone and speaker;'; pt='  2. tela, microfone e alto-falante;' }
    check_3 = @{ es='  3. conexion Wi-Fi;'; en='  3. Wi-Fi connection;'; pt='  3. conexao Wi-Fi;' }
    check_4 = @{ es='  4. panel web de XiaoZhi Care;'; en='  4. XiaoZhi Care web panel;'; pt='  4. painel web do XiaoZhi Care;' }
    check_5 = @{ es='  5. LEDs / recordatorios / radio segun su hardware.'; en='  5. LEDs / reminders / radio according to your hardware.'; pt='  5. LEDs / lembretes / radio conforme seu hardware.' }
    no_dev_tools = @{ es='No necesita Visual Studio, VS Code, ESP-IDF ni Python para usar este instalador.'; en='You do not need Visual Studio, VS Code, ESP-IDF or Python to use this installer.'; pt='Voce nao precisa de Visual Studio, VS Code, ESP-IDF nem Python para usar este instalador.' }

    restore_title = @{ es='XIAOZHI CARE - PRUEBA DE RESTAURACION'; en='XIAOZHI CARE - RESTORE TEST'; pt='XIAOZHI CARE - TESTE DE RESTAURACAO' }
    restore_intro = @{ es='Este proceso restaura byte por byte el respaldo de 16 MB creado antes de instalar XiaoZhi Care.'; en='This process restores, byte for byte, the 16 MB backup created before installing XiaoZhi Care.'; pt='Este processo restaura, byte por byte, o backup de 16 MB criado antes de instalar o XiaoZhi Care.' }
    restore_warn_all = @{ es='La restauracion reemplaza TODO el contenido actual de la Flash, incluida la NVS actual.'; en='Restoration replaces ALL current Flash contents, including the current NVS.'; pt='A restauracao substitui TODO o conteudo atual da Flash, incluindo a NVS atual.' }
    restore_warn_backup = @{ es='Antes de hacerlo se creara un segundo respaldo completo del XiaoZhi Care que funciona ahora.'; en='Before doing so, a second full backup of the currently working XiaoZhi Care will be created.'; pt='Antes disso, sera criado um segundo backup completo do XiaoZhi Care que esta funcionando agora.' }
    restore_start = @{ es='¿Desea comenzar la verificacion para restaurar?'; en='Do you want to start verification for restoration?'; pt='Deseja iniciar a verificacao para restaurar?' }
    restore_title_validate = @{ es='1. VALIDANDO RESPALDO ORIGINAL'; en='1. VALIDATING ORIGINAL BACKUP'; pt='1. VALIDANDO BACKUP ORIGINAL' }
    backup_root_missing = @{ es='No existe la carpeta de backup: {0}'; en='Backup folder does not exist: {0}'; pt='A pasta de backup nao existe: {0}' }
    backup_none = @{ es='No se encontro ningun respaldo antes-xiaozhi-care-*.'; en='No before-xiaozhi-care-* backup was found.'; pt='Nenhum backup antes-xiaozhi-care-* foi encontrado.' }
    backup_found = @{ es='Respaldo encontrado: {0}'; en='Backup found: {0}'; pt='Backup encontrado: {0}' }
    backups_multiple = @{ es='Se encontraron varios respaldos previos a XiaoZhi Care:'; en='Multiple pre-XiaoZhi Care backups were found:'; pt='Foram encontrados varios backups anteriores ao XiaoZhi Care:' }
    backup_choose = @{ es='Elija el respaldo que desea restaurar'; en='Choose the backup you want to restore'; pt='Escolha o backup que deseja restaurar' }
    backup_file_missing = @{ es='Falta el archivo: {0}'; en='File is missing: {0}'; pt='Arquivo ausente: {0}' }
    backup_size_bad = @{ es='El respaldo no mide exactamente 16 MB. Tamano detectado: {0} bytes.'; en='The backup is not exactly 16 MB. Detected size: {0} bytes.'; pt='O backup nao tem exatamente 16 MB. Tamanho detectado: {0} bytes.' }
    backup_size_ok = @{ es='Tamano del respaldo: 16 MB exactos.'; en='Backup size: exactly 16 MB.'; pt='Tamanho do backup: exatamente 16 MB.' }
    sha_missing = @{ es='Falta el SHA-256 creado por el instalador. No se restaurara un backup sin validar.'; en='The SHA-256 created by the installer is missing. An unvalidated backup will not be restored.'; pt='O SHA-256 criado pelo instalador esta ausente. Um backup sem validacao nao sera restaurado.' }
    sha_format_bad = @{ es='El archivo SHA-256 del respaldo no tiene un formato valido.'; en='The backup SHA-256 file does not have a valid format.'; pt='O arquivo SHA-256 do backup nao possui um formato valido.' }
    sha_mismatch = @{ es='El SHA-256 del respaldo NO coincide. El archivo pudo alterarse o corromperse.'; en='The backup SHA-256 DOES NOT match. The file may have been modified or corrupted.'; pt='O SHA-256 do backup NAO confere. O arquivo pode ter sido alterado ou corrompido.' }
    sha_ok = @{ es='SHA-256 del respaldo verificado: {0}'; en='Backup SHA-256 verified: {0}'; pt='SHA-256 do backup verificado: {0}' }
    restore_connect_fail = @{ es='No se pudo comunicar con la placa en {0}.'; en='Could not communicate with the board on {0}.'; pt='Nao foi possivel comunicar com a placa em {0}.' }
    restore_wrong_chip = @{ es='La placa conectada NO es una ESP32-S3.'; en='The connected board is NOT an ESP32-S3.'; pt='A placa conectada NAO e uma ESP32-S3.' }
    restore_psram_fail = @{ es='No se pudo confirmar PSRAM integrada de 8 MB.'; en='Could not confirm 8 MB embedded PSRAM.'; pt='Nao foi possivel confirmar PSRAM integrada de 8 MB.' }
    restore_sec_query = @{ es='No se pudo consultar el estado de seguridad.'; en='Could not query the security state.'; pt='Nao foi possivel consultar o estado de seguranca.' }
    restore_sec_fail = @{ es='Secure Boot o Flash Encryption estan activos. Este restaurador V1 cancela por seguridad.'; en='Secure Boot or Flash Encryption is enabled. Restore V1 cancels for safety.'; pt='Secure Boot ou Flash Encryption estao ativos. O restaurador V1 cancela por seguranca.' }
    restore_title_backup_care = @{ es='3. RESPALDANDO EL XIAOZHI CARE ACTUAL'; en='3. BACKING UP CURRENT XIAOZHI CARE'; pt='3. FAZENDO BACKUP DO XIAOZHI CARE ATUAL' }
    read_try = @{ es='Intentando lectura de 16 MB a {0} baudios...'; en='Trying 16 MB read at {0} baud...'; pt='Tentando leitura de 16 MB a {0} bauds...' }
    read_ok = @{ es='Lectura completa a {0} baudios.'; en='Full read completed at {0} baud.'; pt='Leitura completa em {0} bauds.' }
    read_retry = @{ es='La lectura a {0} fallo; se reintentara a menor velocidad.'; en='Read at {0} failed; retrying at a lower speed.'; pt='A leitura em {0} falhou; nova tentativa em velocidade menor.' }
    restore_write_try = @{ es='Intentando restauracion completa a {0} baudios...'; en='Trying full restoration at {0} baud...'; pt='Tentando restauracao completa a {0} bauds...' }
    restore_write_ok = @{ es='Restauracion completada a {0} baudios.'; en='Restoration completed at {0} baud.'; pt='Restauracao concluida em {0} bauds.' }
    restore_write_retry = @{ es='La escritura a {0} fallo; se reintentara a menor velocidad.'; en='Write at {0} failed; retrying at a lower speed.'; pt='A gravacao em {0} falhou; nova tentativa em velocidade menor.' }
    care_backup_fail = @{ es='No se pudo respaldar el XiaoZhi Care actual. No se restaurara nada.'; en='Could not back up the current XiaoZhi Care. Nothing will be restored.'; pt='Nao foi possivel fazer backup do XiaoZhi Care atual. Nada sera restaurado.' }
    care_backup_created = @{ es='Backup de XiaoZhi Care creado: {0}'; en='XiaoZhi Care backup created: {0}'; pt='Backup do XiaoZhi Care criado: {0}' }
    sha_line = @{ es='SHA-256: {0}'; en='SHA-256: {0}'; pt='SHA-256: {0}' }
    restore_title_confirm = @{ es='4. CONFIRMACION FINAL'; en='4. FINAL CONFIRMATION'; pt='4. CONFIRMACAO FINAL' }
    restore_this = @{ es='Se restaurara este respaldo: {0}'; en='This backup will be restored: {0}'; pt='Este backup sera restaurado: {0}' }
    overwrite_warn = @{ es='A partir de la siguiente confirmacion se sobrescribiran los 16 MB completos de la ESP32.'; en='After the next confirmation, all 16 MB of the ESP32 will be overwritten.'; pt='A partir da proxima confirmacao, os 16 MB completos da ESP32 serao sobrescritos.' }
    restore_word_prompt = @{ es='Para continuar escriba exactamente {0}'; en='To continue, type exactly {0}'; pt='Para continuar, digite exatamente {0}' }
    restore_word = @{ es='RESTAURAR'; en='RESTORE'; pt='RESTAURAR' }
    restore_cancelled = @{ es='Restauracion cancelada por el usuario.'; en='Restoration cancelled by the user.'; pt='Restauracao cancelada pelo usuario.' }
    restore_title_write = @{ es='5. RESTAURANDO LOS 16 MB ORIGINALES'; en='5. RESTORING THE ORIGINAL 16 MB'; pt='5. RESTAURANDO OS 16 MB ORIGINAIS' }
    restore_all_fail = @{ es='La restauracion no pudo completarse tras tres intentos.'; en='Restoration could not be completed after three attempts.'; pt='A restauracao nao pode ser concluida apos tres tentativas.' }
    care_backup_safe = @{ es='El backup del estado Care actual esta a salvo en: {0}'; en='The current Care-state backup is safe at: {0}'; pt='O backup do estado Care atual esta seguro em: {0}' }
    original_backup_safe = @{ es='El backup original sigue a salvo en: {0}'; en='The original backup remains safe at: {0}'; pt='O backup original continua seguro em: {0}' }
    restore_title_verify = @{ es='6. VERIFICACION BYTE POR BYTE'; en='6. BYTE-BY-BYTE VERIFICATION'; pt='6. VERIFICACAO BYTE A BYTE' }
    verify_read_fail = @{ es='La escritura termino, pero no se pudo leer la Flash completa para la verificacion final.'; en='Writing completed, but the full Flash could not be read for final verification.'; pt='A gravacao terminou, mas nao foi possivel ler toda a Flash para a verificacao final.' }
    sha_expected = @{ es='SHA-256 esperado:   {0}'; en='Expected SHA-256:   {0}'; pt='SHA-256 esperado:   {0}' }
    sha_restored = @{ es='SHA-256 restaurado: {0}'; en='Restored SHA-256:   {0}'; pt='SHA-256 restaurado: {0}' }
    verify_mismatch = @{ es='La lectura final NO coincide byte por byte con el backup original.'; en='The final read DOES NOT match the original backup byte for byte.'; pt='A leitura final NAO corresponde byte a byte ao backup original.' }
    verify_not_valid = @{ es='No considere validada la restauracion. Conserve ambos backups y no reinstale Care todavia.'; en='Do not consider the restoration validated. Keep both backups and do not reinstall Care yet.'; pt='Nao considere a restauracao validada. Guarde ambos os backups e ainda nao reinstale o Care.' }
    verify_ok = @{ es='La Flash restaurada coincide byte por byte con el backup original.'; en='The restored Flash matches the original backup byte for byte.'; pt='A Flash restaurada corresponde byte a byte ao backup original.' }
    restore_title_done = @{ es='RESTAURACION COMPLETADA'; en='RESTORATION COMPLETED'; pt='RESTAURACAO CONCLUIDA' }
    restore_exact = @{ es='El estado anterior a XiaoZhi Care fue restaurado exactamente.'; en='The state before XiaoZhi Care was restored exactly.'; pt='O estado anterior ao XiaoZhi Care foi restaurado exatamente.' }
    original_backup_line = @{ es='Backup original: {0}'; en='Original backup: {0}'; pt='Backup original: {0}' }
    care_backup_line = @{ es='Backup del estado Care previo a restaurar: {0}'; en='Backup of the Care state before restoration: {0}'; pt='Backup do estado Care antes da restauracao: {0}' }
    now_check_original = @{ es='Ahora compruebe en XiaoZhi original:'; en='Now check the original XiaoZhi:'; pt='Agora verifique o XiaoZhi original:' }
    original_check_1 = @{ es='  1. arranque normal;'; en='  1. normal startup;'; pt='  1. inicializacao normal;' }
    original_check_2 = @{ es='  2. Wi-Fi conservado;'; en='  2. Wi-Fi preserved;'; pt='  2. Wi-Fi preservado;' }
    original_check_3 = @{ es='  3. pantalla;'; en='  3. display;'; pt='  3. tela;' }
    original_check_4 = @{ es='  4. microfono;'; en='  4. microphone;'; pt='  4. microfone;' }
    original_check_5 = @{ es='  5. parlante.'; en='  5. speaker.'; pt='  5. alto-falante.' }
    rollback_approved = @{ es='Si todo funciona, la prueba de rollback queda APROBADA.'; en='If everything works, the rollback test is APPROVED.'; pt='Se tudo funcionar, o teste de rollback fica APROVADO.' }
}

function Get-XzDefaultLanguage([string]$Culture) {
    if ([string]::IsNullOrWhiteSpace($Culture)) { return 'en' }
    $c = $Culture.Trim().ToLowerInvariant()
    if ($c -match '^es(?:-|$)') { return 'es' }
    if ($c -match '^pt(?:-|$)') { return 'pt' }
    if ($c -match '^en(?:-|$)') { return 'en' }
    return 'en'
}

function Get-XzLanguageName([string]$Lang) {
    switch ($Lang) {
        'es' { return 'Español' }
        'pt' { return 'Português' }
        default { return 'English' }
    }
}

function Select-XzLanguage([string]$DetectedCulture) {
    if ([string]::IsNullOrWhiteSpace($DetectedCulture)) {
        try { $DetectedCulture = (Get-UICulture).Name } catch { $DetectedCulture = 'en-US' }
    }
    $script:XzCulture = $DetectedCulture
    $default = Get-XzDefaultLanguage $DetectedCulture
    $defaultName = Get-XzLanguageName $default

    Clear-Host
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
    Write-Host '  XIAOZHI CARE' -ForegroundColor Cyan
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
    Write-Host ''
    Write-Host ('Windows UI / Interfaz / Interface: ' + $DetectedCulture)
    Write-Host ''
    Write-Host '  [1] Español'
    Write-Host '  [2] English'
    Write-Host '  [3] Português'
    Write-Host ''
    $answer = Read-Host ('ENTER = ' + $defaultName + ' | 1/2/3')
    switch ($answer.Trim()) {
        '1' { $script:XzLang = 'es' }
        '2' { $script:XzLang = 'en' }
        '3' { $script:XzLang = 'pt' }
        ''  { $script:XzLang = $default }
        default { $script:XzLang = $default }
    }
    Clear-Host
}

function T([string]$Key) {
    if (-not $script:XzText.ContainsKey($Key)) { return $Key }
    $entry = $script:XzText[$Key]
    if ($entry.ContainsKey($script:XzLang)) { return [string]$entry[$script:XzLang] }
    if ($entry.ContainsKey('en')) { return [string]$entry['en'] }
    return $Key
}

function Xz-ConfirmYes([string]$Question) {
    $label = T 'yes_no'
    $answer = Read-Host ($Question + ' [' + $label + ']')
    $a = $answer.Trim().ToUpperInvariant()
    if ($script:XzLang -eq 'en') { return ($a -eq 'Y' -or $a -eq 'YES' -or $a -eq 'S' -or $a -eq 'SI' -or $a -eq 'SIM') }
    return ($a -eq 'S' -or $a -eq 'SI' -or $a -eq 'SIM' -or $a -eq 'Y' -or $a -eq 'YES')
}
