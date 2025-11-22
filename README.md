☕ Sistema Semi-Automático para el Control de la Fermentación del Café
IoT · ESP32 · FreeRTOS · Sensores · n8n · Azure · Agricultura 4.0
📌 Descripción general

Este proyecto implementa un sistema semi-automático para monitorear y controlar la fermentación del café utilizando un ESP32, sensores digitales y analógicos, una bomba peristáltica y una celda de flujo.

El sistema mide en tiempo real variables críticas como:

pH

Temperatura del medio (DS18B20)

Sólidos disueltos totales (TDS)

CO₂, temperatura y humedad ambiental (SCD41)

Además, envía los datos a la nube mediante HTTP hacia la plataforma n8n, y registra la información en bases de datos como Supabase, permitiendo un análisis completo del proceso de fermentación.

🚀 Características principales

🔹 Lectura simultánea de sensores usando FreeRTOS.

🔹 Muestreo automático mediante bomba peristáltica.

🔹 Muestreo manual por botón físico.

🔹 Celda de flujo para proteger la sonda de pH.

🔹 Pantalla OLED (SSD1331) para visualización en tiempo real.

🔹 Envío de datos vía WiFi hacia un webhook de n8n.

🔹 Procesamiento en la nube (alertas, dashboards, almacenamiento).

🔹 Sistema estable, capaz de funcionar más de 48 horas continuas.

🔹 Diseño modular, replicable y de bajo costo.

🧠 Arquitectura del sistema
🟦 1. Hardware (ESP32)

El firmware está estructurado en tareas independientes utilizando FreeRTOS, distribuidas entre los dos núcleos:

Core 1

Lectura de sensores (pH, DS18B20, TDS, SCD41)

Actualización de pantalla OLED

Core 0

Control de bomba peristáltica

Muestreo automático

Muestreo manual

Envío periódico de datos

Contador del tiempo de fermentación

Para evitar colisiones, se utilizan mutex y colas.

🟩 2. Flujo de datos IoT

ESP32 recopila las mediciones.

Los datos son empaquetados en JSON.

Se envían vía HTTP POST a un webhook de n8n.

n8n procesa los datos, genera alertas y los almacena en Supabase.

El usuario recibe notificaciones o revisa los datos desde un panel web.

🟧 3. Arquitectura física

Incluye:

Tanque de fermentación

Bomba peristáltica

Celda de flujo

Sonda de pH

Sensor TDS

DS18B20 en inmersión

Sensor SCD41 en el ambiente

Pantalla OLED

💾 Requisitos

ESP32 DEVKit V1

Arduino IDE o PlatformIO

Librerías:

DallasTemperature

OneWire

Adafruit_SSD1331

SparkFun SCD4x

FreeRTOS (incluida en ESP32)

n8n (instancia en la nube)

Cuenta Supabase (o BD alternativa)

🔧 Instalación y uso

Clonar el repositorio

git clone https://github.com/usuario/repositorio.git


Abrir /src/main.ino en Arduino IDE.

Configurar credenciales WiFi y URL de n8n.

Cargar el firmware en el ESP32.

Ejecutar el flujo n8n (n8n/workflow.json).

Encender el sistema:

La pantalla OLED mostrará las variables en tiempo real.

La bomba realizará muestreos según intervalo configurado.

Los datos se enviarán a tu instancia n8n.

🧪 Pruebas realizadas

Funcionamiento continuo: más de 48 horas sin fallos.

Calibración de sensores pH (4, 7, 10).

Validación de TDS contra soluciones patrón.

Validación DS18B20 vs termómetro digital.

Pruebas de bombeo prolongado.

Verificación de manejo de redes múltiples.

🧾 Licencia

Este proyecto se distribuye bajo la licencia MIT.
Puedes utilizarlo, modificarlo y distribuirlo libremente citando el repositorio.

✉️ Contacto

Si deseas colaborar, mejorar el firmware o usar el sistema en campo, puedes contactarme:

📧 tito1999.2009@gmail.com o carlos.olaya@cecar.edu.co  
🌐 www.linkedin.com/in/carlos-enrique-olaya-hernandez-ab973a290

⭐ Si este proyecto te parece útil, dale una estrella al repositorio.
