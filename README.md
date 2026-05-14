# Monte Carlo Simulation API

API en Python (FastAPI) que delega simulaciones Monte Carlo a un motor C++ compilado con `-O3` y paralelizado con OpenMP. El binario C++ genera caminos de precios bajo el modelo Geometric Brownian Motion (GBM) y devuelve los resultados por stdout en JSON.

## Endpoints

### `GET /health`

Verifica que el binario C++ está operativo.

```bash
curl http://localhost:8000/health
# {"status":"ok"}
```

### `POST /simulations`

Ejecuta simulaciones Monte Carlo de precios de activos.

**Request body:**

| Campo        | Tipo       | Descripción                              |
|-------------|------------|------------------------------------------|
| `S0`        | float      | Precio inicial del activo (> 0)          |
| `dates`     | float[]    | Fechas de observación en años (crecientes) |
| `volatility`| float      | Volatilidad anualizada (>= 0)            |
| `r`         | float      | Tipo de interés libre de riesgo          |
| `q`         | float      | Dividendo continuo                       |
| `N`         | int        | Número de simulaciones (1 – 10,000,000)  |

**Ejemplo:**

```bash
curl -X POST http://localhost:8000/simulations \
  -H "Content-Type: application/json" \
  -d '{
    "S0": 100.0,
    "dates": [0.25, 0.5, 1.0],
    "volatility": 0.2,
    "r": 0.05,
    "q": 0.02,
    "N": 5
  }'
```

**Respuesta:**

```json
{
  "simulations": [
    [102.345678, 104.123456, 108.654321],
    [98.765432, 97.234567, 95.123456],
    ...
  ]
}
```

Cada elemento de `simulations` es un camino de precios con un valor por cada fecha en `dates`.

## Uso con Docker

```bash
docker build -t python-c-hello .
docker run --rm -p 8000:8000 python-c-hello
```

### Configurar threads OpenMP

Por defecto OpenMP usa todos los cores disponibles. Para limitarlo:

```bash
docker run --rm -p 8000:8000 -e OMP_NUM_THREADS=4 python-c-hello
```

## Arquitectura

```
Cliente → JSON body → FastAPI (valida) → stdin → C++ (OpenMP, -O3) → stdout → FastAPI → Cliente
```

- **FastAPI** (`app/main.py`): validación de entrada con Pydantic, invocación del binario via `subprocess`.
- **C++** (`native/simulator.cpp`): parseo JSON manual (schema fijo), simulación GBM paralelizada con OpenMP, salida JSON por stdout.
- **Dockerfile** (multi-stage build):
  - **Stage 1** (`debian:bookworm-slim`): compila el binario C++ con `g++ -O3 -fopenmp`. El compilador no llega a la imagen final.
  - **Stage 2** (`python:3.12-slim-bookworm`): copia solo el binario compilado, instala `libgomp1` (runtime OpenMP) y las dependencias Python. Imagen final más ligera y sin toolchain de compilación.

## Estructura

```
app/main.py          — API FastAPI
native/simulator.cpp — Motor Monte Carlo C++ con OpenMP
Dockerfile           — Build y runtime
requirements.txt     — fastapi + uvicorn
```
