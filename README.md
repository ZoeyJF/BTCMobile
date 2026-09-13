# BTCMobile
Herramienta de búsqueda de claves privadas de Bitcoin (BTC) y Ethereum (ETH) por fuerza bruta aleatoria optimizada. Escrito 100% en C++ puro (C++17), desarrollado íntegramente desde un teléfono móvil **Motorola Moto G60** usando Termux + proot-distro Ubuntu, sin PC y sin asistencia de IA en la lógica central del código.

Busca coincidencias contra una base de **BTC.bin hashes** (Hash160 de Bitcoin y los últimos 20 bytes de Keccak-256 de Ethereum) almacenados en memoria RAM mediante `mmap`, usando filtro Bloom como pre-filtro y búsqueda binaria como confirmación final.

---

## Rendimiento real medido en dispositivos móviles

| Dispositivo | Núcleos / Hilos | Velocidad sostenida |
|---|---|---|
| Motorola Moto G60 | 8 núcleos / 8 hilos | ~900,000 keys/s |
| Motorola Moto G86 | 8+ núcleos | ~2,600,000+ keys/s |

La velocidad depende del procesador ARM, la frecuencia sostenida y la disipación térmica del dispositivo. El programa detecta automáticamente la cantidad de núcleos con `std::thread::hardware_concurrency()` y lanza un hilo por núcleo (máximo 32). Dejando a un lado los Nuclúcleo no potentes para el dispositivo.

---

## Cómo funciona BTC.cpp paso a paso

El flujo interno por cada clave generada es el siguiente:

1. **Generación de clave privada**: cada hilo genera una clave privada aleatoria dentro de su zona exclusiva del subrango activo usando un generador Mersenne Twister (`std::mt19937_64`) sembrado con reloj de alta precisión + ID de hilo + hash del thread_id, evitando bloqueos de entropía en `/dev/urandom` de Android.

2. **Cálculo del punto público**: se llama a `localSecp.ComputePublicKey(&privKey)` que realiza la multiplicación escalar `k * G` sobre la curva secp256k1.

3. **Endomorfismo GLV (3 puntos por 1 cálculo)**: se aplican las constantes de la curva `beta = 0x7ae9...01ee` y `lambda = 0x5363...bd72` para obtener `P2 = beta * P1` y `P3 = beta^2 * P1` con solo multiplicaciones modulares de la coordenada X, sin nuevas multiplicaciones escalares. Las claves privadas correspondientes se calculan como `privKey2 = privKey * lambda mod n` y `privKey3 = privKey * lambda^2 mod n`.

4. **Derivación de 6 hashes por iteración**:
   - Para cada uno de los 3 puntos: Hash160 (pubkey comprimida 33 bytes → SHA-256 → RIPEMD-160) para BTC.
   - Para cada uno de los 3 puntos: Keccak-256 (pubkey sin comprimir 64 bytes → últimos 20 bytes) para ETH.

5. **Consulta al filtro Bloom**: cada hash de 20 bytes se consulta en `bloom_check(bloomFilter, hash, 20)`. Si retorna 0, se descarta inmediatamente (sin tocar el disco ni el bin (BTC.bin en disco).

6. **Confirmación con búsqueda binaria**: si el Bloom da positivo, se llama a `searchInBin(hash)` que hace búsqueda binaria primero sobre `BTC.idx` (por prefijo de 8 bytes) y luego sobre el bloque correspondiente en `BTC.bin`.

7. **Hallazgo**: si se confirma, se guarda en `FOUND.txt` (HEX, DEC, RANGE_ID, SUB, TIME), se activa `foundFlag` y todos los hilos terminan de forma sincronizada.

### Avance dentro del batch (caminata secuencial)

Dentro de cada salto aleatorio, el hilo camina **1024 claves consecutivas** usando `localSecp.NextKey(p1)` que es una suma afín de puntos (mucho más rápida que recalcular la multiplicación escalar). Los puntos endomórficos `p2` y `p3` se recalculan desde `p1` en cada paso, y las claves `privKey2`/`privKey3` se avanzan sumando `lambda` y `lambda^2 mod n` respectivamente. Al llegar al límite de la zona, se hace un nuevo salto aleatorio dentro de la misma zona.

---

## Sistema de Rangos y Subrangos

El espacio total de claves secp256k1 (2^256) se organiza así:

- **256 rangos**: cada rango cubre 2^248 claves. El rango inicial se elige al azar entre 1 y 256 al arrancar.
- **100 subrangos por rango**: cada subrango cubre 2^248 / 100 claves.

- **Zonas exclusivas por hilo**: cada subrango se divide en N zonas (N = número de hilos). El hilo 0 toma la primera fracción, el hilo 1 la segunda, etc. El último hilo absorbe el residuo de la división. Ningún hilo pisa la zona de otro.

- **Fragmentación interna**: cada zona se subdivide en 100 fragmentos, y cada fragmento en 100 micro-fragmentos, para determinar el tamaño efectivo del batch (mínimo entre 1024 y el tamaño del micro-fragmento).

### Rotación temporal

- Cada **25 segundos** cada hilo elige un nuevo subrango aleatorio dentro del rango actual.

- Cada **24 cambios de subrango** (~10 minutos) se avanza al siguiente rango (con `compare_exchange_weak` atómico para evitar carreras entre hilos).

- Ciclo completo de los 256 rangos: aproximadamente **42 horas**.
- El reporte de consola se refresca cada **50 segundos** sobrescribiéndose en el mismo lugar (sin scroll).

---

## Estructura y formato exacto de los archivos de datos que debes usar.

Los tres archivos de datos deben estar en la misma carpeta donde se ejecuta `./BTC`, porque se abren por nombre relativo (`BTC.bin`, `BTC.idx`, `BTC.xor`).

### Estructuración  de como debe ser tu BTC.bin:

- **Contenido**: hashes de 20 bytes cada uno, concatenados sin separadores.

- **Cantidad**: hashes → tamaño exacto **bytes** (debe ser múltiplo exacto de 20).

- **Tipos de hash incluidos**: Hash160 de direcciones Bitcoin (P2PKH que empiezan con `1`, y el hash directo de pubkey para SegWit `bc1q`) y los últimos 20 bytes de Keccak-256 de direcciones Ethereum.

- **Ordenamiento obligatorio**: los hashes deben estar ordenados de menor a mayor (orden lexicográfico de bytes). 
Esto es crítico porque `searchInBin` usa búsqueda binaria; si el bin no está ordenado, las búsquedas fallarán silenciosamente.

- **Prefijos**: no se almacenan prefijos en el bin; los prefijos se derivan del índice.


### Estructuración  de como debe ser tu BTC.idx

- **Formato**: array de estructuras `IdxEntry` empaquetadas (`#pragma pack(push, 1)`), sin padding.
- **Estructura de cada entrada (28 bytes)**:

```c
struct IdxEntry {
    uint64_t prefix;    // 8 bytes: primeros 8 bytes del primer hash del grupo (big-endian en memoria, se compara con bswap64)
    
    long long offset;   // 8 bytes: desplazamiento en bytes dentro de BTC.bin donde empieza el grupo
    
    uint64_t count;     // 8 bytes: cantidad de hashes de 20 bytes en este grupo
    
    
    uint32_t reserved;  // 4 bytes: reservado (alineación)
};
```

- **Cantidad**: entradas → tamaño exacto ** ** ((× 28)).

- **Agrupación**: cada entrada agrupa hashes que comparten el mismo prefijo de 8 bytes. Las entradas deben estar ordenadas por `prefix` de menor a mayor.

- **Relación con BTC.bin**: `offset` apunta al byte exacto dentro de BTC.bin donde comienza el grupo; `count` indica cuántos hashes de 20 bytes hay consecutivos desde ese offset. La suma de todos los `count` debe ser exactamente (numero exacto .

### Estructuración  de como debe ser tu BTC.xor

- **Contenido**: mapa de bits del filtro Bloom serializado.

- **Tamaño**: **bytes** → 3bits.

- **Parámetros del Bloom**: 10 funciones hash, tasa de error objetivo 0.001, ~14.3 bits por elemento.
- 
- **Validación matemática**: al iniciar, `initFiles` calcula el tamaño esperado del Bloom con la fórmula `m = (-k * n) / ln(1 - p^(1/k))` donde `n = binSize/20`, `k = 10`, `p = 0.001`, y verifica que `xorSize` esté dentro de una tolerancia del 5%. Si no cuadra, el programa rechaza el archivo y termina. Esto evita bucles infinitos de falsos positivos por un Bloom mal dimensionado.

- **Los tres archivos deben ser consistentes entre sí**: el `BTC.xor` debe haber sido generado exactamente sobre los mismos hashes que contiene `BTC.bin`, con los mismos parámetros. Si el xor corresponde a un bin diferente, la tasa de falsos positivos se disparará y el programa será inútil.

---

## Librerías en Biblioteca/ y su conexión con BTC.cpp

Todas las librerías están en la carpeta `Biblioteca/` y se incluyen desde `BTC.cpp` con rutas relativas (`#include "Biblioteca/..."`). Cada una cumple un rol específico en la cadena de búsqueda:

| Archivo | Rol | Conexión con el flujo |
|---|---|---|
| `GMP256K1.cpp/.h` | Implementación de la curva secp256k1: `ComputePublicKey` (multiplicación escalar k*G), `NextKey` (suma afín P+G), `ModMulK1` (multiplicación modular usada por el endomorfismo GLV) | Es el núcleo criptográfico. BTC.cpp crea un objeto `Secp256K1 localSecp` por hilo y lo usa para generar puntos públicos y avanzar secuencialmente |
| `Int.cpp/.h` | Enteros grandes arbitrarios sobre GMP (`mpz_t`): suma, resta, multiplicación, división con módulo, comparación, conversión a base 10/16 | Todas las claves privadas, constantes de la curva (beta, lambda, curveOrder) y límites de rango/subrango son objetos `Int`. Es la base aritmética de todo |
| `IntGroup.cpp/.h` | Cálculo de inversos modulares por lotes (técnica de Montgomery batch inversion) | Usado internamente por GMP256K1 para acelerar la conversión de coordenadas proyectivas a afines |
| `IntMod.cpp` | Operaciones modulares especializadas: `ModMul`, `ModInv`, `ModSqrt`, `ModAdd`, `ModSub` sobre el orden de la curva y el primo del campo | Soporta la aritmética de la curva y el endomorfismo |
| `Point.cpp/.h` | Estructura de punto de la curva con coordenadas `x, y, z` (proyectivas Jacobianas): constructores, copia, `Clear`, `Set`, `isZero`, `Reduce`, `equals` | Los puntos `p1, p2, p3` y los temporales `tmp` son objetos `Point`. Se pasan a las funciones de derivación de hash |
| `Random.cpp/.h` | Generación de números aleatorios para objetos `Int` dentro de un rango `[min, max]`: método `Int::Rand(min, max)` | Usado por `privKey.Rand(&zoneStart, &zoneEnd)` para generar la clave privada inicial de cada salto y al hacer wrap-around de zona |
| `bloom.cpp/.h` | Implementación del filtro Bloom: `bloom_check(filter, data, len)` que testa membership probabilístico | BTC.cpp consulta `bloom_check(bloomFilter, h160, 20)` antes de cada búsqueda binaria. Es la primera línea de defensa que elimina el 99.9% de las consultas innecesarias al bin |
| `sha256.cpp/.h` | SHA-256: `sha256(data, len, out)` | Primer paso del Hash160 de Bitcoin: pubkey comprimida → SHA-256 |
| `ripemd160.cpp/.h` | RIPEMD-160: `ripemd160(data, len, out)` | Segundo paso del Hash160 de Bitcoin: resultado SHA-256 → RIPEMD-160 → 20 bytes finales |
| `keccak.c` | Función interna de permutación Keccak (f[1600]) | Llamada por sha3.c para implementar Keccak-256 |
| `rmd160.c` | Implementación alternativa de RIPEMD-160 en C puro | Respaldo/compatibilidad para la derivación RIPEMD |
| `sha3.c/.h` | Keccak-256 para Ethereum: `KECCAK_256_Init`, `KECCAK_256_Update`, `KECCAK_256_Final` con contexto `SHA3_256_CTX` | Usado por `deriveKeccakFromPoint`: pubkey sin comprimir 64 bytes → Keccak-256 → se toman los últimos 20 bytes como dirección ETH |
| `util.c/.h` | Utilidades de conversión hex/bytes, manejo de buffers | Soporte general para las operaciones de hashing y serialización |
| `xxhash.c/.h` | Hash no criptográfico de alta velocidad (xxHash) | Usado internamente por el filtro Bloom para generar las 10 funciones hash a partir de los datos de entrada |

### Cadena completa de conexión

```
BTC.cpp (workerThread)
   │
   ├─→ Random.cpp        → genera privKey aleatoria en [zoneStart, zoneEnd]
   ├─→ GMP256K1.cpp      → ComputePublicKey(privKey) = p1; NextKey(p1) para el batch
   │     └─→ Int.cpp + IntMod.cpp + IntGroup.cpp + Point.cpp  (aritmética de curva)
   ├─→ Endomorfismo GLV  → p2 = beta*p1, p3 = beta²*p1 (ModMulK1 de GMP256K1)
   ├─→ sha256.cpp + ripemd160.cpp  → deriveHash160FromPoint (BTC)
   ├─→ sha3.c + keccak.c           → deriveKeccakFromPoint (ETH)
   ├─→ bloom.cpp         → bloom_check contra BTC.xor (pre-filtro)
   └─→ searchInBin       → búsqueda binaria en BTC.idx + BTC.bin (confirmación)
         └─→ si confirma → saveFoundKey → FOUND.txt
```

---

## Funciones de BTC.cpp (14 en total)

| # | Función | Trabajo detallado |
|---|---|---|
| 1 | `formatWithCommas` | Convierte un `uint64_t` en string con separadores de miles (ej. 44,485,632) para el reporte legible |
| 2 | `cleanupResources` | Libera en orden seguro: `munmap(binMap)`, `free(idxMap)`, `free(xorMap)`, `delete bloomFilter`. Verifica `MAP_FAILED` antes de `munmap` |
| 3 | `initFiles` | Abre, valida tamaño y carga en RAM los tres archivos. Valida que binSize sea múltiplo de 20, que idxSize sea múltiplo de 28 (sizeof IdxEntry), que xorSize no sea 0, y que el tamaño del Bloom cuadre matemáticamente. Cierra todos los descriptores de archivo en cada ruta de error |
| 4 | `deriveHash160FromPoint` | Toma un punto público, construye la pubkey comprimida (33 bytes: prefijo 0x02/0x03 según paridad de Y + 32 bytes de X), aplica SHA-256 y luego RIPEMD-160 → 20 bytes (dirección BTC) |
| 5 | `deriveKeccakFromPoint` | Toma un punto público, construye la pubkey sin comprimir (64 bytes: 32 de X + 32 de Y), aplica Keccak-256 y extrae los últimos 20 bytes → dirección ETH |
| 6 | `searchInBin` | Búsqueda binaria en dos niveles: primero en BTC.idx buscando el prefijo de 8 bytes (con `bswap64` para endian), luego en el bloque de BTC.bin apuntado por el offset. Incluye guardas contra underflow de `size_t` y acceso fuera de límites del mmap |
| 7 | `saveFoundKey` | Bajo mutex `fileMutex`, abre FOUND.txt en modo append y escribe HEX, DEC, RANGE_ID, SUB y timestamp. Incrementa el contador atómico `walletsFoundCount` |
| 8 | `makeBar` | Genera una barra de progreso ASCII de 10 caracteres `[####------]` según un valor 0-10 |
| 9 | `getRangeHex` | Calcula los límites hexadecimales (from/to) de un rango dado y los escribe en buffers con `snprintf` + `free` seguro |
| 10 | `getSubRangeHex` | Calcula los límites hexadecimales (from/to) de un subrango dado dentro de un rango |
| 11 | `printReport` | Bajo mutex `consoleMutex`, construye el reporte completo (versión, modo, núcleos, rango, subrango, límites hex, barra de rango, subrangos por hilo, bloom, eficiencia, velocidad, total, found) y lo imprime sobrescribiendo las líneas anteriores con secuencias ANSI `\033[nA` y `\r\033[2K` |
| 12 | `workerThread` | Hilo de búsqueda principal: inicializa la curva, precalcula constantes (beta, lambda, curveOrder, tamaños de zona/fragmento/batch), y entra al bucle principal que rota subrangos cada 25s, genera claves, aplica GLV, deriva 6 hashes, consulta bloom, confirma en bin, y avanza el batch con NextKey |
| 13 | `reportThread` | Hilo dedicado que duerme 100ms por ciclo, acumula tiempo, y cada 50 segundos llama a `printReport` con las métricas actuales cargadas desde los contadores atómicos |
| 14 | `main` | Punto de entrada: carga archivos, inicializa random, elige rango inicial aleatorio, detecta núcleos (limitado a MAX_THREADS=32), inicializa el array atómico `threadSubRanges`, lanza los hilos worker y el reportThread, espera a que terminen (por hallazgo o excepción), y libera recursos |

---

## Estructura de archivos y orden obligatorio

```
Proyecto/
├── BTC.cpp                 código principal (C++17 puro)
├── Makefile                reglas de compilación (los .o van a Biblioteca/)
├── BTC.bin                 243,026,023 hashes × 20 bytes = 4,860,520,460 bytes (ordenado)
├── BTC.idx                 237,331 entradas × 28 bytes = 6,645,268 bytes (ordenado por prefijo)
├── BTC.xor                 filtro Bloom = 436,535,494 bytes (generado sobre el mismo bin)
└── Biblioteca/
    ├── GMP256K1.cpp / .h   curva secp256k1
    ├── Int.cpp / .h        enteros grandes (GMP)
    ├── IntGroup.cpp / .h   inversos modulares por lote
    ├── IntMod.cpp          aritmética modular
    ├── Point.cpp / .h      puntos de la curva
    ├── Random.cpp / .h     random para Int
    ├── bloom.cpp / .h      filtro Bloom
    ├── sha256.cpp / .h     SHA-256
    ├── ripemd160.cpp / .h  RIPEMD-160
    ├── keccak.c            permutación Keccak
    ├── rmd160.c            RIPEMD-160 en C
    ├── sha3.c / .h         Keccak-256 (Ethereum)
    ├── util.c / .h         utilidades hex/bytes
    └── xxhash.c / .h       hash rápido (Bloom)
```

**Regla de orden**: `BTC.cpp` y `Makefile` en la raíz; `Biblioteca/` al mismo nivel que `BTC.cpp`; los tres archivos de datos (`BTC.bin`, `BTC.idx`, `BTC.xor`) en la carpeta desde donde se ejecuta `./BTC`. Si falta cualquiera de los tres archivos de datos, el programa termina con error antes de consumir memoria.

---

## Compilación y requisitos previos

### Paso 1: preparar el entorno en Android

Desde Termux (fuera de proot):

```bash
pkg update && pkg upgrade
pkg install proot-distro
proot-distro install ubuntu
proot-distro login ubuntu
```

### Paso 2: instalar dependencias dentro de Ubuntu

Una vez dentro del entorno Ubuntu de proot:

```bash
apt update
apt install g++ make libgmp-dev
```

- `g++`: compilador C++ (requiere soporte C++17).
- `make`: sistema de construcción.
- `libgmp-dev`: biblioteca GMP para aritmética de enteros grandes (usada por `Int.cpp`).

### Paso 3: compilar y ejecutar

```bash
make
./BTC
```

El `Makefile` compila con `-O3 -std=c++17 -pthread`, enlaza con `-lgmp`, coloca todos los `.o` dentro de `Biblioteca/` (incluido `BTC.o`), y limpia la salida de la consola al terminar el enlace para que solo quede el prompt `root@localhost:~#`.

Para recompilar desde cero:

```bash
make clean
make
```

---

## Entorno probado y compatibilidad

- **Probado y estable**: Android + Termux + proot-distro Ubuntu en Motorola Moto G60 (8 núcleos, ~900K keys/s) y Motorola Moto G86 (~2.6M+ keys/s).

- **No probado aún**: otras distribuciones Linux de escritorio o servidor (Debian, Ubuntu nativo, Arch, Fedora, etc.). 
El código usa llamadas POSIX estándar (`mmap`, `open`, `read`, `pthread`) y GMP, por lo que en teoría compila en cualquier Linux con g++ y libgmp-dev, pero no ha sido verificado fuera del entorno Termux/proot.

---

## Salida al encontrar una clave

Cuando se confirma una coincidencia real (Bloom positivo + búsqueda binaria positiva), el programa:

1. Escribe en `FOUND.txt` (modo append, bajo mutex):
   ```
   HEX: <clave privada en hexadecimal>
   DEC: <clave privada en decimal>
   RANGE_ID: <rango donde se encontró>
   SUB: <subrango / 100>
   TIME: <timestamp Unix>
   ----------------------------------------
   ```
2. Imprime en consola `KEY FOUND T<id>: <hex>`.
3. Activa `foundFlag` con `memory_order_release` para que todos los hilos terminen de forma sincronizada.

4. Libera toda la memoria (`munmap`, `free`, `delete bloom`) antes de retornar desde `main`.

---

## Detalles técnicos de concurrencia y seguridad

- **Límite de hilos**: constante `MAX_THREADS = 32`. El array

`threadSubRanges[MAX_THREADS]` está blindado contra accesos fuera de límites.

- **Semilla de entropía**: combina `steady_clock` + ID de hilo + `getpid()` para no agotar `/dev/urandom` en Android.

- **Atómicos**: todos los contadores compartidos (`totalKeysScanned`, `bloomFalsePositives`, etc.) usan `std::atomic` con `memory_order_relaxed`; `foundFlag` usa `release/acquire` para garantizar visibilidad del cierre.

- **Mutex**: `consoleMutex` protege la salida de consola; `fileMutex` protege la escritura a FOUND.txt.

- **Validación de búsqueda**: `searchInBin` incluye guardas contra underflow de `size_t`, offsets negativos, y accesos que excedan `binSize`.

- **Cierre limpio**: `cleanupResources` verifica `MAP_FAILED` antes de `munmap` y nulifica cada puntero tras liberarlo.


☕ Support the Project / Donations

If you find this tool useful and want to support its development, you can donate to the following address:

./USDT/BSC : 0x755d370be94a8b904d66539886f8a933067adf22

 . /ETH: 0x755d370be94a8b904d66539886f8a933067adf22

   . /BTC : 16E9B3mNrRWXvN63ZVFgPLtHB4e13JUSZE
      
     ./SOL : 8U9jrjdHQdXscVvkPeLfmCoa7yEoox3Le8a15Bai2jwM
