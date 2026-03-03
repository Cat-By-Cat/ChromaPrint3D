FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

LABEL maintainer="neroued <neroued@gmail.com>"
LABEL org.opencontainers.image.title="ChromaPrint3D"
LABEL org.opencontainers.image.description="Multi-color 3D print image processor"
LABEL org.opencontainers.image.license="Apache-2.0"
LABEL org.opencontainers.image.source="https://github.com/neroued/ChromaPrint3D"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libgomp1 curl \
        libopencv-core4.5d \
        libopencv-imgproc4.5d \
        libopencv-imgcodecs4.5d \
        libpotrace0 \
    && rm -rf /var/lib/apt/lists/*

HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -sf http://localhost:8080/api/v1/health || exit 1

COPY build/bin/chromaprint3d_server /app/bin/chromaprint3d_server
COPY build/_deps/onnxruntime-src/lib/libonnxruntime*.so* /app/lib/
COPY web/frontend/dist/  /app/web/
COPY data/dbs/        /app/data/dbs/
COPY data/recipes/    /app/data/recipes/
COPY data/models/     /app/data/models/
COPY data/presets/    /app/data/presets/
COPY data/model_pack/ /app/model_pack/

ENV LD_LIBRARY_PATH=/app/lib

EXPOSE 8080

ENTRYPOINT ["/app/bin/chromaprint3d_server"]
CMD ["--data", "/app/data", "--web", "/app/web", "--model-pack", "/app/model_pack/model_package.json", "--port", "8080"]
