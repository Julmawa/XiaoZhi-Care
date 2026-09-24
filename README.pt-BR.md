# XiaoZhi Care

**Personal Memory & Care Assistant**

[Español](README.md) · [English](README.en.md)

XiaoZhi Care é uma extensão open source para **XiaoZhi ESP32**, criada para apoiar pessoas idosas com memória pessoal local, lembretes, assistência cotidiana e uma interface simples para a família.

> **Estado atual:** `v0.3.2-alpha`  
> **Base validada:** XiaoZhi `v2.5.0`  
> **Hardware validado:** ESP32-S3 N16R8  
> **Perfil validado:** `bread-compact-wifi`  
> **Licença:** MIT

## Principais recursos

- Memória pessoal local.
- Painel web de configuração.
- Integração MCP com XiaoZhi.
- Lembretes cotidianos.
- Assistência conversacional para o organizador de medicamentos.
- Verificação visual de doses anteriores não confirmadas.
- Áudios personalizados.
- Rádio pela Internet.
- Estados visuais por LEDs.
- Instalador Windows com backup completo antes de modificar a placa.
- Restauração byte a byte.
- Instalador multilíngue: Español, English e Português.

## Instalação

A release pré-compilada **não substitui a instalação base do XiaoZhi**.

Antes da instalação:

1. XiaoZhi deve estar instalado e funcionando.
2. Tela, microfone e alto-falante devem funcionar corretamente.
3. A placa deve corresponder ao perfil suportado pela release.
4. O instalador verifica chip, Flash, PSRAM, segurança, versão e perfil antes de gravar.

O instalador cria primeiro um **backup completo de 16 MB** e não executa apagamento total da Flash.

Veja [docs/INSTALLATION.md](docs/INSTALLATION.md).

## Privacidade

XiaoZhi Care segue uma abordagem **Local First** para a memória pessoal e os dados Care. Isso não significa que todo o XiaoZhi funcione offline; reconhecimento de voz, modelos e TTS podem depender do backend configurado.

Veja [docs/PRIVACY.md](docs/PRIVACY.md).

## Segurança

XiaoZhi Care pode ajudar com lembretes do organizador de medicamentos, mas **não é um dispositivo médico** e não diagnostica, prescreve ou decide tratamentos.

Veja [docs/SAFETY.md](docs/SAFETY.md).

## Projeto original

XiaoZhi Care deriva de [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32), distribuído sob licença MIT.

A base validada desta release é **XiaoZhi v2.5.0** e os avisos originais de copyright e licença são preservados.

Veja [UPSTREAM.md](UPSTREAM.md).

## Licença

MIT. Veja [LICENSE](LICENSE).
