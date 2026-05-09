# RLCD42 Weather Dashboard (ESP32-S3) - V12.1.YT

Dashboard meteorológico para la **Waveshare ESP32-S3 RLCD 4.2** con pantalla reflectiva, UI web local, radio por internet, ISS tracker y OTA.

> Estado del proyecto: **V12.1.YT estable**

---

## Vista general

Este proyecto extiende el firmware original del RLCD y lo convierte en una plataforma completa:

- 14 pantallas en el dispositivo
- UI web local por WiFi
- OTA desde navegador
- Radio Browser + presets + URL manual
- Modo imagen online o imagen fija subida
- Pantalla ISS con alerta sonora por distancia

---

## Hardware

Plataforma principal:

- **Waveshare ESP32-S3 RLCD 4.2"**
- Pantalla RLCD monocromatica (400x300 en panel, area de imagen usada en modo foto: **400x272**)
- Sensor interno de temperatura/humedad
- medicion de batería
- buzzer / salida de audio para beeps
- salida de audio para radio streaming

Referencias:

- Ficha del fabricante (producto): <https://www.waveshare.com/esp32-s3-rlcd-4.2.htm?srsltid=AfmBOoo58n7GXDbdSR9f_E71tCNEj3zZmV5xePfc_ZuCjhvvjHB0WbWA>\n- Documentacion técnica: <https://docs.waveshare.com/ESP32-S3-RLCD-4.2>

---

## Carcasa y STL

Se van a publicar los archivos **.stl** de carcasas y adaptaciones usadas en este proyecto.

Incluye:

- modelo base de carcasa
- modificaciones propias para este build
- variantes para montaje y acceso a puertos

---

## Funcionalidades por pantalla (dispositivo)

El firmware usa 14 pantallas:

1. **Principal**  
   Hora/fecha, datos generales, batería, estádo basico.

2. **Dashboard KUMA**  
   Vista resumida con telemetría/estádo de servicios.

3. **Imagen**  
   Modo imagen online por tema o imagen fija subida.

4. **Zonas horarias**  
   Visualizacion de hora en zonas configuradas.

5. **Clima actual**  
   Condiciones meteorológicas actuales.

6. **Pronóstico por hora**  
   Pronóstico horario.

7. **Pronóstico diario**  
   Pronóstico por días.

8. **Astronomia**  
   Datos astronomicos (salida/puestá, fase lunar, etc.).

9. **Gráfico temperatura**  
   Historial de sensor interno (ventana rodante).

10. **Gráfico humedad**  
    Historial de humedad interna (ventana rodante).

11. **Sistema**  
    Red, IP, uptime, sync, sensor y firmware.

12. **Redes WiFi**  
    Informacion/escaneo de redes segun configuración.

13. **ISS**  
    Mapa ISS, distancia a ubicacion local y alertas sonoras de aproximacion.

14. **Radio**  
    Streaming por URL o Radio Browser, filtros, presets y estádo de reproducción.

---

## Web UI local (dispositivo)

Al conectar el equipo a WiFi, se sirve una UI web local (por IP LAN) con navegacion completa.

### Flujo WiFi Manager

- En el primer arranque, la configuración de red se realiza por portal cautivo (WiFiManager).
- Una vez conectado, el panel local queda disponible en la IP del dispositivo.
- El acceso web está protegido con:
  - **Usuario:** `admin`
  - **Contraseña:** la misma clave de la red WiFi configurada en WiFiManager.
- Si actualizas la clave del WiFi, el acceso web se sincroniza con esa nueva clave.

### Secciones principales de la Web UI

- **Panel principal**
  - estádo general
  - volumen general
  - toggles de beeps (Batería / ISS)
  - selector de pantalla

- **Imagen (`/image`)**
  - selector de tipo: Imagen Online / Subir imagen
  - tema online (predefinido o personalizado)
  - intervalo de cambio (1 a 10 minutos)
  - upload de imagen fija
  - ajuste/recorte centrado para salida 400x272

- **Sistema (`/system`)**
  - datos de sistema en vivo
    - bloque OTA Firmware (subida `.bin`)

- **Radio (`/radio`)**
  - pais
  - filtro por tipo de stream (MP3/AAC/AAC+/Any)
  - bitrate máximo
  - lista de emisoras por Radio Browser
  - URL manual
  - reproducir/stop
  - presets editables (10) con modo eliminar

---

## OTA

Actualizacion de firmware por navegador (UI local de Sistema):

- seleccion de `.bin`
- validacion y proceso OTA
- reinicio automatico con restáuracion de estádo

Convencion actual de nombre de firmware:

- `RLCD42-vX_Y.bin`
- ejemplo actual: `RLCD42-V12.1.YT.bin`

---

## Estructura del proyecto

Archivos clave:

- `sketch_feb21c.ino` -> firmware principal
- `platformio.ini` -> entorno de compilacion
- `DOCUMENTACION_*.md` -> historial de cambios
- `OTA bin/` -> binarios listos para OTA

---

## Compilacion y carga

Entorno recomendado:

- PlatformIO
- Framework Arduino para ESP32-S3

Flujo operativo usado en este proyecto:

1. compilar localmente en este repositorio
2. generar `.bin`
3. subir **exclusivamente por OTA** desde UI local

---

## Creditos

Proyecto original (firmware base RLCD):

- **JohnWillieGee**
- YouTube: <https://www.youtube.com/watch?v=wd1mQkY0oWk>
- GitHub: <https://github.com/JohnWillieGee/Waveshare-RLCD-ESP32S3-Weather-dashboard>

Gabinete 3D base:

- **@Nick_314947** (Printables)
- Modelo original: <https://www.printables.com/model/1625482-enclosure-for-esp32-s3-rlcd-42-display>

Modificaciones propias sobre STL:

- `back_witharm.stl`
  - abertura adicional para exponer GPIO
  - ranuras para ventilacion / salida de sonido del parlante
- `arm_slim.stl`
  - refuerzo interno agregado
  - caladura para colgar en tornillo
  - dos huecos para imanes de neodimio de **8x2 mm**

Modificaciones y evolucion (este fork):

- **Juanjo Castillo / RLCD42**
- Ajustes de UI, radio, ISS, OTA e integración general del proyecto

Agradecimientos:

- Waveshare
- Comunidad ESP32 / PlatformIO
- Radio Browser API
- Visual Studio Code + Codex de OpenAI

---

## Roadmap sugerido (YouTube/GitHub)

- publicacion de STL finales
- vídeo de armado y flashing
- demo completa de las 14 pantallas
- demo OTA en vivo

---

## Licencia

Este repositorio se publica bajo licencia **MIT**.

- Ver archivo: [LICENSE](LICENSE)
- Esta licencia aplica a las modificaciones y material propio de este fork.
- Los componentes de terceros (firmware base, librerias, diseños STL base, fuentes y APIs) mantienen sus propias licencias y condiciones de uso.



