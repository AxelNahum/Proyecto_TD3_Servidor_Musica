# Servidor de Música en Red

Proyecto de la asignatura **Técnicas Digitales III**: un sistema cliente-servidor en C sobre Linux para escuchar música por la red.

El servidor tiene una carpeta con canciones MP3 y atiende a varios clientes a la vez. Cada cliente se conecta por TCP, ve la lista de canciones, elige una y la reproduce a medida que la recibe.

## Integrantes

- Aldax, Ramiro
- Nahum, Axel Nathanel
- Ziccardi, Ignacio Gabriel

## Datos del proyecto

| | |
|---|---|
| **Universidad** | Universidad Tecnológica Nacional — FRH |
| **Asignatura** | Técnicas Digitales III |
| **Profesor** | Mercado, Mariano |
| **JTP** | Marino, Martín Miguel |
| **Ayudante** | Acuña Pavese, Claudio Alejandro |
| **Año** | 2026 |

## Funcionalidades

- [x] Servidor TCP concurrente con `fork()`
- [x] Protocolo de aplicación: `LIST` y `GET`
- [x] Reproducción mientras se recibe la canción (*streaming*)
- [x] Controles: `PAUSE` y `RESUME`
- [x] Controles: `SEEK` y `QUIT`, y cambio de canción al instante
- [x] Buffer circular en el cliente (productor-consumidor con `fork()`, memoria compartida y semáforos)
- [x] Tabla de sesiones en memoria compartida, con estadísticas y límite de conexiones
- [x] Salas de reproducción sincronizada (`JOIN`, `LEAVE` y `ROOMS`)
- [x] Interfaces gráficas del servidor y del cliente (Python + PySide6), con modo oscuro y claro

## Instalación

Cada máquina necesita solo su parte: la **servidor** usa `server/` y `common/`, la **cliente** usa `client/` y `common/`. Las canciones (MP3) van en la carpeta `songs/` de la máquina servidor.

```bash
# Máquina servidor
make deps-server              # paquetes del sistema (pide la contraseña; una sola vez)
make install-gui-server       # compila, instala PySide6 y crea el acceso directo "Servidor de Musica"

# Máquina cliente
make deps-client              # paquetes del sistema (pide la contraseña; una sola vez)
make install-gui-client       # compila, instala PySide6 y crea el acceso directo "Reproductor de Musica"
```

Las interfaces gráficas son opcionales: sin ellas alcanza con `make server` / `make client` (no hace falta Python).

## Uso

**Con interfaz gráfica:** abrir *Servidor de Musica* y apretar **Iniciar** (muestra la IP del servidor); en cada cliente, abrir *Reproductor de Musica*, escribir esa IP y apretar **Conectar**.

**Por terminal:**

```bash
./bin/server [carpeta] [max]    # en la PC servidor (por defecto songs/ y 5 clientes)
kill -USR1 <pid>                # muestra la tabla de clientes (el PID se ve al arrancar)

./bin/client <IP del servidor>  # en la PC cliente
```

Comandos del cliente por terminal:

| Comando | Qué hace |
|---|---|
| `LIST` | Muestra las canciones disponibles |
| `PLAY <canción>` | Reproduce la canción mientras la recibe (si sonaba otra, la cambia) |
| `PAUSE` / `RESUME` | Pausa / reanuda |
| `SEEK <seg>` | Va a ese segundo; `SEEK +10` / `SEEK -10` adelanta / atrasa |
| `QUIT` | Saca la canción |
| `ROOMS` | Lista las salas |
| `JOIN <sala>` | Entra a una sala (si no existe, la crea); ahí los comandos afectan a todos |
| `LEAVE` | Sale de la sala y sigue escuchando solo |
| `SALIR` | Cierra el cliente |

## Estructura

```
common/        protocolo y memoria compartida (lo usan los dos)
server/        servidor en C
server/gui/    interfaz gráfica del servidor (Python)
client/        cliente en C
client/gui/    interfaz gráfica del cliente (Python)
```

## Licencia

Copyright (C) 2026 Ramiro Aldax, Axel Nathanel Nahum, Ignacio Gabriel Ziccardi.

Este proyecto se distribuye bajo la licencia [GNU AGPL-3.0](LICENSE), sin ninguna garantía.
