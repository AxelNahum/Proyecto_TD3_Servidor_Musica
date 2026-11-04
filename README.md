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
- [ ] Reproducción mientras se recibe la canción (*streaming*)
- [ ] Controles: `PAUSE`, `RESUME`, `SEEK` y `QUIT`
- [ ] Buffer circular en el cliente (productor-consumidor con semáforos)
- [ ] Tabla de sesiones en memoria compartida, con estadísticas y límite de conexiones
- [ ] Salas de reproducción sincronizada (`JOIN` y `LEAVE`)
- [ ] Interfaz gráfica del cliente

## Uso

```bash
make                            # compila bin/server y bin/client
./bin/server                    # en la PC servidor (lee las canciones de songs/)
./bin/client <IP del servidor>  # en la PC cliente
```

## Licencia

Copyright (C) 2026 Ramiro Aldax, Axel Nathanel Nahum, Ignacio Gabriel Ziccardi.

Este proyecto se distribuye bajo la licencia [GNU AGPL-3.0](LICENSE), sin ninguna garantía.
