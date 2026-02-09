FROM --platform=linux/arm64/v8 python:3.11-slim

WORKDIR /app

ARG PROCESSOR_CLIENT_TIMEOUT_S
ARG ENABLE_SVE

ENV PROCESSOR_CLIENT_TIMEOUT_S=${PROCESSOR_CLIENT_TIMEOUT_S} \
    ENABLE_SVE=${ENABLE_SVE}

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application
COPY app ./app

EXPOSE 8080

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8080"]
