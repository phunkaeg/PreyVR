# Partidas de prueba (saves) — qué hay en cada una

Registro de las partidas de Prey que sirven de entorno de prueba, para poder probar en el mock (o pedir una prueba en
el casco) sin preguntar dónde está cada cosa. **Al guardar una partida nueva para pruebas, añadirla aquí** (plantilla
al final).

## Dónde están y cómo se llaman

- Carpeta: `%USERPROFILE%\Saved Games\Arkane Studios\Prey\SaveGames\`.
- **La UI cuenta desde 1 y las carpetas desde 0**: «CAMPAÑA 3 (slot 3)» del menú = carpeta `Campaign2`.
- Dentro de cada campaña: `manualN` (guardado manual), `quicksave0-2` y `autosave0-2` (rotan: el juego pisa el más
  antiguo), `temp` (caché de niveles del juego).
- Cada `save.meta` es XML legible: `level`, `saveTime` (UTC), `playTime` (**minutos** de la partida: 292 = 4h52, lo
  que muestra el menú), `location`. Para listar:
  `for d in */; do echo $d; tr -d '\0' < $d/save.meta; echo; done` (Git Bash, dentro de `Campaign2`).

## Cómo se carga una partida en el mock

- `build\jordi-mock\Boot.ps1` pulsa «Continuar», que carga **la partida más reciente** (`saveTime`) de la campaña
  jugada por última vez. Por eso, la partida que se cargue depende de qué se guardó lo último.
- Cargar otra partida concreta = navegar el menú «Cargar partida» con `Mock.ps1` (`-Window` para ver el menú). No hay
  atajo por consola: `ConsolePolicy` bloquea a propósito todo lo que toca partidas.
- **Protección de las partidas del usuario**: desde el 2026-10-06, `Boot.ps1` copia toda la carpeta de Prey a
  `build\jordi-savebackup-session` al arrancar y `Boot.ps1 -Close` restaura **esa** copia (antes restauraba la copia
  fija `build\jordi-savebackup-20261005`, que habría pisado los quicksaves nuevos del usuario). Si Prey ya está
  abierto sin sesión del mock, `Boot.ps1` no lo toca: es el juego del usuario.
- Si una prueba en el mock guarda (quicksave/autosave), `-Close` lo deshace; los ficheros nuevos se avisan y se
  quedan.

## Las partidas

### Campaña 3 · `Campaign2\manual4` — entorno de interacción con objetos (2026-10-06 18:48, manual)

Guardada por el usuario el 2026-10-06 para probar la interacción con objetos. División de Neuromods
(`campaign/research/simulationlabs`), 4h55 (`playTime` 295). Es la más reciente: **«Continuar» la carga.**

Zona segura, objetos delante del jugador:

| Tipo | Objetos | Cómo se interactúa |
|---|---|---|
| Agarrables, no son ítems (no van al inventario), agarre instantáneo sin mantener F | toallas, zapatos, una escultura | pulsar F |
| Agarrable rompible | vaso de café | pulsar F; se rompe al lanzarlo/golpearlo |
| Ítems que se recogen al inventario | bolsa de patatas, cables, material mimético, etc. | pulsar F |
| Agarrable manteniendo F | cubo de basura | mantener F |
| Cadáver (sin ítems dentro) | un cadáver | se puede arrastrar; se puede abrir para recoger ítems (no tiene) |
| Contenedor | dos taquillas, **ambas con ítems** | abrir y recoger ítems de dentro |

**No todo está de frente** (palabras del usuario): «el cadáver está mirando atrás, las taquillas mirando a la izquierda,
cubo de basura y zapatos mirando abajo, toallas a la derecha». Es decir, desde la vista al cargar: el cadáver hay que
buscarlo girándose hacia atrás, las taquillas a la izquierda, el cubo y los zapatos en el suelo (mirar abajo), las
toallas a la derecha. Esto prueba el rayo de selección/agarre en todas las direcciones, no solo al frente.

Comprobado en el mock (2026-10-06, `build\jordi-mock\UseScan.ps1` y `UseCases.ps1`, PARTE 10 del HANDOFF): al
cargar, **llave inglesa** equipada; el inventario es el de `quicksave0` (`Equip.ps1 gloo` funciona igual). El GLOO
tiene poca reserva (~60 disparos en total): si se vacía, la prueba de recarga ya no vale en esa sesión.

Objetos, con el ID de entidad que da esta partida al cargar, la dirección de la mano izquierda que los selecciona
(`lhandYaw/lhandPitch` del mock: grados desde el frente; yaw positivo = izquierda) y lo que hace el juego (tipo y
modo de `PerformInteraction`; modo 0 = toque, modo 1 = mantener ~0,33 s). `use.find <ID>` da la dirección exacta
en cualquier momento (los objetos se mueven al cargarlos y soltarlos).

| ID | Objeto (arquetipo) | Dirección | Toque | Mantener |
|---|---|---|---|---|
| 3409, 3410 | toallas (`ArkPhysicsProps.Bathroom.Towels`) | −80…−110 / −25…+5 | cargar (6) | — |
| 3407 | toalla (la tercera, a la izquierda) | 30…40 / −25…−10 | cargar (6) | — |
| 3406, 3413 | zapatos L/R (`Misc.Clothes_Shoes_A_Male1_L/_R`) | −60…−90 / −30…−40 | cargar (6) | — |
| 3011 | ventilador de mesa = la "escultura" (`Office.Desk_Fan`) | −10…0 / −25…−10, solo sin objetos recogibles delante (el juego los prioriza) | cargar (6) | — |
| 3005 | taza de café, rompible (`Kitchen.Coffee_Mug_A`, `ArkBreakable`) | −20 / −10…−15 | cargar (6); el gatillo derecho la lanza | — |
| 3415 | banco (`Chairs.Bench_Leather`) | −30…0 / −40 | cargar manteniendo 0,75 s | — |
| 64589 | cecina (`ArkPickups.Food.Bag.SunDriedTomatoJerky`) | −50…−30 / −25…−10 | coger al inventario (4) | tipo 12 |
| 64596 | tubo de plástico (`RecyclerJunk.PlasticTubing`) | 0…30 / −40…−10 | coger (4) | tipo 12 |
| 64594 | tumor mimético (`RecyclerJunk.Exotic.MimicTumor`) | 20…60 / −55…−40 | coger (4) | tipo 12 |
| 64595 | órgano alienígena (`RecyclerJunk.Exotic.AlienOrganSmall`) | −90…−40 / −70…−55 | coger (4) | tipo 12 |
| 64587 | muestra en tubo de ensayo (`RecyclerJunk.TestTubeSample`) | −70…−30 / −55…−40 | coger (4) | tipo 12 |
| 5510 | cubo de basura (`Containers/Misc.Trash_Can_Small`) | 60…110 / −55 | registrar (3) | cargar (6) |
| 65047 | cadáver, Jovan Gavrilovic (`ArkHumans.Scientists`) | ±180, 140…170 / −40…−25 (detrás) | registrar (3, al soltar; vacío) | arrastrar (6) |
| 3412 | taquilla «Lobby Locker A» (`Containers/Misc.Lobby`) | 80…100 / −40…+5 | coge el objeto marcado de su lista (3), uno por toque | — |
| 3408 | la otra taquilla | 60…70 / −40…+5 | igual | — |

En la lista de cada taquilla el juego ofrece además «Y Buscar» y, en la comida, «Y Comer»; Y no está asignado en
partida (ver `docs/INTERACTION-LEFT-HAND-2026-10-06.md`).

### Campaña 3 · `Campaign2\quicksave1`, `quicksave2` (2026-10-06 18:47 y 18:48)

Del usuario mientras preparaba `manual4`, mismo sitio (División de Neuromods, 4h53 y 4h55). Sin describir; usar
`manual4`. Al ser quicksaves, el juego los pisará con el tiempo.

### Campaña 3 · `Campaign2\quicksave0` — pruebas de armas (2026-08-15 22:09, quicksave)

La partida de las PARTES 1-9 (`build\jordi-mock\HANDOFF.md`): División de Neuromods, 4h52, **GLOO equipado**, Rayo Q
en el inventario (`Equip.ps1 qbeam`: abajo, abajo desde la 1ª casilla; ver HANDOFF §5.4). Modo supervivencia: el
Rayo Q puede averiarse. Era la que cargaba «Continuar» hasta el 2026-10-06; ahora hay que cargarla desde el menú
(o pedir al usuario que vuelva a guardarla como la más reciente).

### Otras de la Campaña 3

`manual0-1` (2024-02, Neuromods), `manual2` (2024-02, Psicotrónica), `manual3` (2026-08-15, Psicotrónica),
`autosave0-2` (2026-08-15, Vestíbulo). Sin uso conocido para pruebas.

Campañas 1-2 (`Campaign0`, `Campaign1`, 2021-2022) y Mooncrash: partidas antiguas del usuario, no usar.

## Plantilla

```markdown
### Campaña N · `Campaign<N-1>\<slot>` — <para qué sirve> (<fecha hora>, <manual|quicksave>)

Lugar (`level`), tiempo de juego, arma equipada, qué hay y dónde (dirección desde la vista al cargar),
cómo se interactúa con cada cosa, qué queda por verificar.
```
