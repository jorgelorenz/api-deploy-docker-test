# --- Stage 1: compilar el binario C++ ---
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends g++ libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY native/ /build/native/
RUN g++ -O3 -fopenmp -o /build/simulator /build/native/simulator.cpp -lm

# --- Stage 2: imagen final solo con runtime ---
FROM python:3.12-slim-bookworm

# Solo la runtime de OpenMP, no el compilador
RUN apt-get update && apt-get install -y --no-install-recommends libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/simulator /usr/local/bin/simulator

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app/ app/

EXPOSE 8000

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
