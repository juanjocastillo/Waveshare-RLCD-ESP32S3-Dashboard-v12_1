# RLCD42 Weather Dashboard (ESP32-S3) - V12.1.YT

Dashboard meteorologico para la Waveshare ESP32-S3 RLCD 4.2 con pantalla reflectiva, UI web local, radio por internet, ISS tracker y OTA.

> Estado del proyecto: V12.1.YT estable

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

- Waveshare ESP32-S3 RLCD 4.2"
- Pantalla RLCD monocromatica (400x300 en panel, area de imagen usada en modo foto: 400x272)
- Sensor interno de temperatura/humedad
- Medicion de bateria
- Buzzer / salida de audio para beeps
- Salida de audio para radio streaming

Referencias:

- Ficha del fabricante (producto): <https://www.waveshare.com/esp32-s3-rlcd-4.2.htm>
- Documentacion tecnica: <https://docs.waveshare.com/ESP32-S3-RLCD-4.2>

---

## Carcasa y STL

Los archivos .stl de carcasas y adaptaciones usadas en este proyecto se incluyen en la carpeta `STL/`.

Incluye:

- Modelo base de carcasa
- Modificaciones propias para este build
- Variantes para montaje y acceso a puertos

---

## Funcionalidades por pantalla (dispositivo)

El firmware usa 14 pantallas:

1. Principal  
   Hora/fecha, datos generales, bateria, estado basico.

2. Dashboard KUMA  
   Vista resumida con telemetria/estado de servicios.

3. Imagen  
   Modo imagen online por tema o imagen fija subida.

4. Zonas horarias  
   Visualizacion de hora en zonas configuradas.

5. Clima actual  
   Condiciones meteorologicas actuales.

6. Pronostico por hora  
   Pronostico horario.

7. Pronostico diario  
   Pronostico por dias.

8. Astronomia  
   Datos astronomicos (salida/puesta, fase lunar, etc.).

9. Grafico temperatura  
   Historial de sensor interno (ventana rodante).

10. Grafico humedad  
    Historial de humedad interna (ventana rodante).

11. Sistema  
    Red, IP, uptime, sync, sensor y firmware.

12. Redes WiFi  
    Informacion/escaneo de redes segun configuracion.

13. ISS  
    Mapa ISS, distancia a ubicacion local y alertas sonoras de aproximacion.

14. Radio  
    Streaming por URL o Radio Browser, filtros, presets y estado de reproduccion.

---

## Web UI local (dispositivo)

Al conectar el equipo a WiFi, se sirve una UI web local (por IP LAN) con navegacion completa.

### Flujo WiFi Manager

- En el primer arranque, la configuracion de red se realiza por portal cautivo (WiFiManager).
- Una vez conectado, el panel local queda disponible en la IP del dispositivo.
- El acceso web esta protegido con:
  - Usuario: `admin`
  - Contrasena: la misma clave de la red WiFi configurada en WiFiManager.
- Si actualizas la clave del WiFi, el acceso web se sincroniza con esa nueva clave.

### Secciones principales de la Web UI

- Panel principal
  - Estado general
  - Volumen general
  - Toggles de beeps (Bateria / ISS)
  - Selector de pantalla

- Imagen (`/image`)
  - Selector de tipo: Imagen Online / Subir imagen
  - Tema online (predefinido o personalizado)
  - Intervalo de cambio (1 a 10 minutos)
  - Upload de imagen fija
  - Ajuste/recorte centrado para salida 400x272

- Sistema (`/system`)
  - Datos de sistema en vivo
  - Bloque OTA Firmware (subida `.bin`)

- Radio (`/radio`)
  - Pais
  - Filtro por tipo de stream (MP3/AAC/AAC+/Any)
  - Bitrate maximo
  - Lista de emisoras por Radio Browser
  - URL manual
  - Reproducir/stop
  - Presets editables (10) con modo eliminar

---

## OTA

Actualizacion de firmware por navegador (UI local de Sistema):

- Seleccion de `.bin`
- Validacion y proceso OTA
- Reinicio automatico con restauracion de estado

Convencion actual de nombre de firmware:

- `RLCD42-vX_Y.bin`
- Ejemplo actual: `RLCD42-v12_1.bin`

---

## Estructura del proyecto

Archivos clave:

- `sketch_feb21c.ino` -> firmware principal
- `platformio.ini` -> entorno de compilacion
- `secrets.h` -> placeholders de WiFi/API
- `STL/` -> archivos de carcasa

---

## Compilacion y carga

Entorno recomendado:

- PlatformIO
- Framework Arduino para ESP32-S3

Flujo operativo recomendado:

1. Completar `secrets.h`
2. Compilar localmente en este repositorio
3. Generar `.bin`
4. Subir por OTA desde UI local

---

## APIs y claves

- Weather API: requiere clave en `secrets.h`
- ISS API: endpoint publico, no requiere clave
- KUMA: configurable por `kKumaBaseUrl` y `kKumaSlug`
  - Tambien se puede adaptar para UptimeRobot (ver comentarios en `sketch_feb21c.ino`)

---

## Creditos

Proyecto original (firmware base RLCD):

- JohnWillieGee
- YouTube: <https://www.youtube.com/watch?v=wd1mQkY0oWk>
- GitHub: <https://github.com/JohnWillieGee/Waveshare-RLCD-ESP32S3-Weather-dashboard>

Gabinete 3D base:

- @Nick_314947 (Printables)
- Modelo original: <https://www.printables.com/model/1625482-enclosure-for-esp32-s3-rlcd-42-display>

Modificaciones propias sobre STL:

- `back_witharm.stl`
  - Abertura adicional para exponer GPIO
  - Ranuras para ventilacion / salida de sonido del parlante
- `arm_slim.stl`
  - Refuerzo interno agregado
  - Caladura para colgar en tornillo
  - Dos huecos para imanes de neodimio de 8x2 mm

Modificaciones y evolucion (este fork):

- Juanjo Castillo / RLCD42
- Ajustes de UI, radio, ISS, OTA e integracion general del proyecto

Agradecimientos:

- Waveshare
- Comunidad ESP32 / PlatformIO
- Radio Browser API
- Visual Studio Code + Codex de OpenAI

---

## Licencia

Este repositorio se publica bajo licencia MIT.

- Ver archivo: [LICENSE](LICENSE)
- Esta licencia aplica a las modificaciones y material propio de este fork.
- Los componentes de terceros (firmware base, librerias, disenos STL base, fuentes y APIs) mantienen sus propias licencias y condiciones de uso.
