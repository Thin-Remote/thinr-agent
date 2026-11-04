# Propuesta de diseño: autoactualizador seguro del agente

Estado: borrador para revisión · Objetivo: agente v1.8.0 · Fecha: 2026-11-03

Esta es una **propuesta de diseño, no una implementación**. Recoge el acuerdo con
Álvaro del 2026-11-03 y lo aterriza sobre el código actual del agente para que se
pueda valorar antes de escribir nada.

## 1. Objetivo y motivación

Mantener al día la flota de agentes de **usuarios casuales**: equipos heterogéneos,
repartidos, que nosotros no orquestamos con playbooks. Hoy hay dispositivos
quedándose en versiones viejas (p. ej. 1.6.x) sin arreglos importantes, incluidos
los de seguridad (el de reconexión cuando no se completaba el handshake TLS, el
fd-leak de `close_range` de la 1.7.4). Un autoactualizador activo por defecto hace
que esos arreglos lleguen a todos sin intervención.

El agente **es** el acceso remoto y la monitorización del equipo. Por eso la pieza
central del diseño no es "actualizar", que ya sabemos hacer, sino **no dejar la
máquina inaccesible si una actualización sale mal**. De ahí el watchdog de rollback.

## 2. Qué hay hoy (punto de partida real)

- **`extensions/updater.{hpp,cpp}`**: descarga desde `get.thinremote.io/<canal>.json`,
  compara versión, descarga el binario, verifica SHA256 (y si no hay checksum, ejecuta
  `--version` vía `cmd::exec` con timeout), sustituye el binario y lanza `SIGTERM` para
  que el init reinicie. Es **solo bajo demanda**: lo invoca el recurso IOTMP `update`
  con `{action: "apply"|"check", channel}`. **No hay bucle periódico** y **no hay
  rollback**: el swap hace `remove(install_path)` y luego `copy_file(temp -> install_path)`,
  sin guardar el binario anterior ni comprobar que el nuevo arranca sano.
- **CDN** (`get.thinremote.io`, S3 + CloudFront): canales `latest`/`main`/`develop`,
  cada uno con `<canal>.json` = `{version, checksums{<arch>: "sha256:..."}}` y
  `binaries/<canal>/thinr-agent.<arch>`. La verificación SHA256 ya existe y se mantiene.
- **Reinicio por el init, ya garantizado** (clave para que SIGTERM baste tanto en update
  como en rollback):
  - systemd: `Restart=always`, `RestartSec=5` (`installer/init_systems/systemd_service_installer.cpp`).
  - launchd: `KeepAlive` (`launchd_service_installer.cpp`).
  - OpenWrt/procd: `respawn` (`sysv_service_installer.cpp`).
- **Señal de salud ya disponible**: el cliente IOTMP expone
  `set_state_callback(std::function<void(client_state, const std::string&)>)` con los
  estados `CONNECTING, CONNECTED, AUTHENTICATING, AUTHENTICATED, AUTH_FAILED,
  DISCONNECTED, CONNECTION_ERROR, STREAMS_READY`. **`STREAMS_READY`** (autenticado y
  con las property streams suscritas) es el "plenamente operativo" que necesitamos.
- **Config del dispositivo**: `config/config_manager.{hpp,cpp}`, cifrada, en
  `/etc/thinr-agent/config.json` (root) o `~/.config/thinr-agent/config.json` (usuario).
  Hoy guarda credenciales y `verify_ssl`. Hay además una property `config` remota
  (`agent.cpp::init_property_streams` / `apply_config`) que ya aplica `monitoring` y
  `modbus` en caliente.
- **Android**: el updater está deshabilitado (el binario vive en el `nativeLibraryDir`
  de solo lectura; la unidad de actualización es el APK). El modo auto es un no-op ahí.

## 3. Diseño propuesto

Dos piezas separables, y esta separación es una decisión de diseño importante:

1. **Bucle de auto-sondeo** (tira del canal estable y aplica): activo **solo** en modo `auto`.
2. **Watchdog de rollback** (envuelve *cualquier* sustitución de binario): activo
   **siempre**, tanto en auto como en actualizaciones orquestadas (acción `update`,
   playbooks). Un mal release empujado por playbook también debe autosanarse.

### 3.1 Política de actualización por dispositivo

Nueva sección `update` en la config local del agente:

```json
{
  "update": {
    "policy": "auto",            // "auto" | "managed"
    "channel": "latest",         // canal estable por defecto
    "check_interval_h": 6,        // periodicidad del sondeo en modo auto
    "rollback_deadline_s": 90     // ventana para considerar sano el arranque nuevo
  }
}
```

- **`auto`** (por defecto en instalaciones nuevas de usuarios): el agente sondea
  `latest.json` cada `check_interval_h` (con *jitter* para no sincronizar a toda la
  flota) y aplica si hay versión distinta. Opt-out = pasar a `managed`.
- **`managed`** (equipos gestionados: nuestras instancias thinger-server/monitoring):
  sin sondeo; solo se actualizan por orquestación central (`agent_update` / playbooks
  `agent-update-*`), como hoy. El watchdog sigue activo.

Dónde se fija la política:
- **Instalación**: el instalador escribe `policy: auto` para instalaciones
  interactivas de usuario; nuestra instalación de flota la pone en `managed`
  (flag de instalador, p. ej. `--update-policy managed`). *Decisión abierta: ver §7.*
- **Servidor**: se puede sobrescribir desde la property `config` (vía `apply_config`),
  útil para marcar en `managed` un producto entero por defecto. **Restricción dura**
  (de la tarea): la nube **nunca** puede desactivar lo que permite recuperar el equipo;
  en concreto, una config remota rota o ausente **no** apaga el updater ni el watchdog.
  El peor caso de una config remota mala es cambiar `auto`↔`managed`, nada más.

### 3.2 Bucle de auto-sondeo (modo `auto`)

- Un timer sobre el `io_context` del cliente (mismo patrón que la extensión de
  monitoring), no un `std::thread` con `sleep`.
- Cada `check_interval_h` + jitter: `updater::run(check, channel)`; si hay versión
  nueva, `updater::run(apply, channel)` **a través del nuevo camino con watchdog** (§3.3).
- El sondeo solo se dispara con el agente **conectado** (de hecho necesita red para
  leer `latest.json`), lo que da una garantía útil al watchdog: en el momento de
  actualizar había conectividad, así que "no reconecta tras reiniciar" es una
  regresión real y no un equipo apagado.

### 3.3 Watchdog de rollback (la pieza de seguridad)

Secuencia de una actualización (vale para auto y orquestada):

**Antes de reiniciar (proceso viejo):**
1. Descargar a temp en el **mismo sistema de ficheros** que el binario instalado.
2. Verificar SHA256 (como hoy). **Añadir** un *preflight* `--version` *siempre* (hoy
   solo corre cuando no hay checksum): un "¿este binario siquiera ejecuta en este
   kernel/arch?" barato antes de tocar nada.
3. Copiar el binario **actual** (bueno conocido) a `install_path + ".prev"`.
4. Escribir un **marcador de estado** de forma atómica (temp + `rename` + `fsync`),
   junto a la config (`update.state`):
   ```json
   { "phase": "pending", "from": "v1.7.5", "to": "v1.8.0",
     "prev_binary": "/usr/local/bin/thinr-agent.prev",
     "started_at": 1762000000, "attempts": 0, "deadline_s": 90 }
   ```
5. **Swap atómico**: `rename(temp_nuevo -> install_path)` (atómico en el mismo fs:
   `install_path` queda siempre entero-viejo o entero-nuevo, nunca truncado; cierra
   la ventana de un corte de luz a mitad del `copy_file` actual).
6. `SIGTERM` -> el init reinicia (systemd/launchd/procd).

**Al arrancar (proceso nuevo), muy pronto en `main()` y antes de nada pesado:**
7. Leer `update.state`. Si `phase == "pending"`:
   - `attempts++` y `fsync` del marcador **inmediatamente** (para sobrevivir a un crash
     posterior).
   - Si `attempts > MAX_ATTEMPTS` (p. ej. 3): es un **bucle de crash** -> **rollback ya**
     (ver abajo).
   - Si no: registrar un `set_state_callback` y armar un timer de `deadline_s`.
8. Continuar el arranque normal.
9. **Confirmación**: si el callback llega a `STREAMS_READY` antes del deadline ->
   marcar `phase: "confirmed"` (o borrar el marcador), borrar `install_path.prev`,
   reportar éxito y cancelar el timer. `AUTH_FAILED` -> rollback inmediato (el servidor
   rechaza al binario nuevo: no esperamos al deadline).
10. **Rollback**: si vence el deadline sin `STREAMS_READY`, o si saltó el bucle de crash:
    `rename(prev_binary -> install_path)`, `phase: "rolled_back"`, `SIGTERM`. El init
    reinicia con el binario **anterior bueno conocido**. Al arrancar, ese binario ve
    `phase: "rolled_back"`, lo registra, **reporta al servidor** "update a vX falló,
    revertido a vY", borra el marcador y la copia `.prev`.

Un mal update se convierte así en un susto de ~1-2 minutos, y el equipo vuelve solo a
estar accesible con la versión que ya funcionaba.

### 3.4 Mitigar el blast radius

En modo auto, toda la flota tira del mismo `latest.json`: un release malo podría tocar
a muchos a la vez. Defensas, de más a menos importante:

- **(a) Rollback por dispositivo** (§3.3): principal. Cada equipo se autosana aunque el
  release sea malo para todos.
- **(b) Publicación escalonada del canal estable (canario)**: añadir un campo
  `rollout` a `latest.json`, p. ej. `"rollout": 25`. El agente aplica solo si
  `hash_estable(device_id) % 100 < rollout`. Así se expone 5% -> 50% -> 100% en días,
  de forma determinista y **sin orquestación por dispositivo**. Un paso de CI/herramienta
  "promote vX a 10/50/100%" ajusta el número.
- **(c) Botón de pánico**: re-publicar `latest.json` apuntando al release bueno anterior.
  El updater compara por **desigualdad** (`current != latest`), así que los equipos ya
  actualizados **bajan** al bueno conocido (y esa bajada también pasa por el watchdog).
  Conviene confirmarlo y dejarlo como comportamiento explícito y probado.

## 4. Casos límite y cómo se cubren

| Caso | Cobertura |
|------|-----------|
| Binario nuevo arranca pero no conecta (red, TLS, config) | Deadline -> rollback. El sondeo solo dispara con red, así que "no reconecta" es regresión real. |
| Binario nuevo crashea en el arranque completo | Contador `attempts` (incrementado y `fsync` lo primero en `main`) -> rollback tras `MAX_ATTEMPTS`. |
| Binario corrupto / no ejecuta | Bloqueado antes del swap por SHA256 + preflight `--version`. |
| Corte de luz a mitad del swap | `rename` atómico: `install_path` nunca queda truncado. |
| El propio `.prev` no conecta | Era el bueno conocido (estaba corriendo); si no conecta es red/servidor, reintenta como siempre, sin watchdog. |
| Equipo legítimamente offline durante auto-update | No ocurre: el sondeo exige conectividad previa. Deadline generoso (varias reconexiones) para un blip transitorio. |
| systemd se rinde por start-limit antes del rollback | Desactivar el limitador del start-limit en la unidad (`StartLimitIntervalSec=0`): nuestro contador `attempts` ya acota el bucle de forma segura. *Ver §7.* |
| OpenWrt/BusyBox, service managers variados | SIGTERM + respawn/KeepAlive/Restart ya cubren el reinicio; el rollback usa el mismo mecanismo. No se añaden dependencias. |
| Kernel Linux muy viejo (2.6) | Nada del diseño usa syscalls nuevas; el marcador es E/S de ficheros normal. |
| Android | Updater deshabilitado; auto es no-op. Sin cambios. |

## 5. Cambios de código (orientativo, no vinculante)

- `extensions/updater.{hpp,cpp}`:
  - Nuevo camino de aplicación con copia `.prev`, swap atómico (`rename`), escritura del
    marcador, y preflight `--version` siempre.
  - Lectura/confirmación del marcador en el arranque, `set_state_callback` para la señal
    de salud, timer de deadline, y la rutina de rollback + reporte.
- `agent/agent.cpp`: enganchar el callback de estado del cliente al watchdog; arrancar el
  bucle de auto-sondeo si `policy == auto`.
- `config/config_manager.{hpp,cpp}`: leer/escribir la sección `update`.
- `installer/*`: flag de política en la instalación (`--update-policy`), default `auto`
  para usuario; opción `StartLimitIntervalSec=0` en la unidad systemd (§7).
- CLI (`cli/service_manager`, `cli/command_handler`): `thinr update --policy ...` para
  opt-out/opt-in y para fijar canal; posible `thinr update --now`.
- Marcador de estado `update.state` junto a la config, con escritura atómica.
- (Servidor/CI) campo `rollout` en `latest.json` y herramienta de promoción escalonada.

## 6. Plan de validación

En un canario con kernel y SO representativos:
1. Con una versión nueva en estable, un agente en modo `auto` la aplica solo y
   reconecta sano (`STREAMS_READY`), y el marcador queda `confirmed`.
2. Forzar un binario que arranca pero no conecta (o que crashea): el watchdog revierte
   al anterior y el equipo vuelve a estar accesible dentro del deadline.
3. Un equipo en modo `managed` **no** se autoactualiza.
4. Opt-out (`auto` -> `managed`) funciona y persiste.
5. Botón de pánico: re-publicar `latest.json` al release anterior y ver que un equipo
   actualizado baja al bueno conocido, pasando por el watchdog.
6. Canario escalonado: con `rollout: 25`, solo ~1/4 de los device_id de prueba aplican.

## 7. Decisiones abiertas para Álvaro

1. **Default de política en la instalación**: ¿el instalador pone `auto` para todo lo
   interactivo y `managed` solo con flag explícito? ¿O inferimos `managed` cuando el
   install viene de nuestra orquestación (token/producto conocido)?
2. **`check_interval_h` y `rollback_deadline_s`**: propongo 6 h y 90 s. ¿Cómodo?
3. **start-limit de systemd**: ¿desactivamos `StartLimitIntervalSec` y confiamos en
   nuestro contador `attempts`? Es lo que hace el rollback fiable; el riesgo es que un
   binario genuinamente colgado respawnee hasta que el contador revierta (acotado).
4. **Canario escalonado (`rollout`)**: ¿lo metemos ya en la v1.8.0 o lo dejamos para
   después y en la v1.8.0 solo va rollback + auto-poll? El rollback por dispositivo ya
   nos cubre lo crítico.
5. **Downgrade por pánico**: confirmar que queremos que `current != latest` baje de
   versión (hoy el código lo haría). Alternativa: permitir solo subir salvo bandera de
   pánico explícita en `latest.json`.

## 8. No-goals

- No tocar la orquestación central existente (`agent_update`, playbooks `agent-update-*`).
- No romper OpenWrt/BusyBox ni los service managers.
- No mantener delta/parcheo de binarios: se descarga el binario completo (ya es estático).
- No auto-actualizar sin rollback: el watchdog es requisito, no opcional.

## 9. Decisiones tomadas (2026-11-03, con Álvaro)

- **Política por defecto**: `auto` para toda instalación interactiva de usuario;
  `managed` solo cuando se marca explícitamente al instalar. Arranca en auto de forma
  natural.
- **Intervalos**: sondeo cada **6 h** (leer el manifiesto `latest.json`, el mismo que se
  usa para descargar); deadline de rollback **90 s**.
- **Canario**: se adopta un **despliegue escalonado basado en tiempo** (ver §3.4
  revisado), no un porcentaje fijo editado a mano.
- **Botón de pánico**: se confirma permitir **bajar de versión** (`current != latest`
  también baja) para poder revertir toda la flota re-publicando el manifiesto.

### 3.4 (revisado) Despliegue escalonado basado en tiempo

En vez de un porcentaje fijo que haya que ir editando, el manifiesto lleva la fecha de
publicación y la duración de la rampa, y cada equipo decide solo:

```json
{ "version": "v1.8.0", "published_at": 1762000000,
  "soak_h": 72, "ramp_h": 96, "checksums": { } }
```

- **Reposo (`soak_h`)**: durante las primeras horas tras publicar, ningún equipo en auto
  aplica (como Tailscale, que vigila varios días antes de habilitar una versión). Da
  margen a detectar problemas antes de exponer a nadie.
- **Rampa (`ramp_h`)**: pasado el reposo, la exposición se ensancha sola con el tiempo.
  Cada equipo calcula un número fijo entre 0 y 1 a partir de un hash de su `device_id`
  (y, para que el grupo que va primero **rote** en cada release, mezclando también la
  versión destino en el hash). Aplica cuando la fracción de rampa ya transcurrida supera
  su número. Las franjas son anidadas: según avanza el tiempo solo se suman equipos.
- **Publico una sola vez**: no hay que editar el manifiesto periódicamente; la rampa es
  autónoma. Lo único manual es el **botón de pánico** / congelar la rampa.
- **Congelar/revertir ante reversiones**: si el servidor detecta un pico de rollbacks
  reportados, puede **congelar** la rampa (fijar la exposición al nivel actual) o
  revertir el manifiesto al release bueno anterior. Esto recupera el gate de "parar a
  mirar" sin trabajo manual en el caso bueno.

### Añadidos adoptados de referentes externos

- **Actualización consciente de actividad** (de Tailscale): **no** sustituir el binario
  mientras haya una sesión interactiva en curso (terminal, exec en streaming, proxy o
  transferencia de ficheros). Diferir el swap a un momento tranquilo. Evita cortarle la
  sesión al usuario justo cuando la está usando.
- **Reposo antes de la rampa** (de Tailscale): `soak_h`, ya recogido arriba.
- **Pinning N-1/N-2** (de CrowdStrike), futurible: permitir a un usuario cauto quedarse
  una o dos versiones por detrás en lugar de solo `auto`/`managed`. No para la v1.8.0.

## 10. Referencias: cómo lo hacen otros

- **CrowdStrike Falcon**: políticas de versión N / N-1 / N-2 (el cliente elige cuánto se
  retrasa) y despliegue por fases (interno -> early adopters -> general). El apagón del
  19/07/2024 (una actualización de contenido salió global e instantánea, sin escalonar y
  sin rollback, afectando ~8,5 M de equipos) llevó a CrowdStrike a comprometerse con
  **canario + despliegue escalonado**. Es justo el doble fallo (sin escalonar, sin
  revertir) que este diseño evita.
  - [CrowdStrike PIR (resumen ejecutivo)](https://crowdstrike.com/wp-content/uploads/2024/07/CrowdStrike-PIR-Executive-Summary.pdf)
  - [TechTarget: content validation bug led to global outage](https://www.techtarget.com/cybersecurity/news/366596579/CrowdStrike-Content-validation-bug-led-to-global-outage)
  - [Cómo gestionar versiones del sensor (N-1/N-2)](https://inventivehq.com/knowledge-base/crowdstrike/how-to-manage-crowdstrike-sensor-versions)
- **Tailscale** (el análogo más cercano: agente ligero en máquinas de clientes que no
  controlan): vigilan estabilidad varios días antes de habilitar una versión para
  auto-update (los equipos empiezan ~1 semana tras publicar); difieren la actualización
  si el equipo está ocupado o con sesiones abiertas; los equipos nuevos heredan el
  ajuste por defecto y cambiarlo no afecta a los existentes.
  - [Tailscale auto-update GA](https://tailscale.com/blog/auto-update-ga)
  - [Docs: Update Tailscale](https://tailscale.com/docs/features/client/update)
- **Google Chrome / Omaha**: despliegue estable escalonado por porcentajes
  (5% -> 15% -> 50% -> 100%, u otra variante 1/5/10/50/100) y comprobación cada ~5 h.
  Valida el método de exposición gradual.
  - [Chromium: release cycle (staged rollout)](https://chromium.googlesource.com/chromium/src/+show/ff8d22e/docs/process/release_cycle.md)
