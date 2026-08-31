# Build stage — compile the real binary from source, fresh, inside a
# clean Linux container (don't reuse your local build/ directory; that
# might be a Windows/MSYS2 binary and won't run in this image).
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential python3 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make clean && make all

# Runtime stage — only what's needed to serve the demo.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    ttyd ca-certificates && rm -rf /var/lib/apt/lists/*

# Non-root user — never run the exposed pty as root.
RUN useradd -m -s /bin/bash demo
WORKDIR /app
COPY --from=build /src/build/pngdecoder /app/build/pngdecoder
COPY --from=build /src/demo /app/demo
COPY demo_menu.sh /app/demo_menu.sh
RUN chmod +x /app/demo_menu.sh /app/build/pngdecoder && chown -R demo:demo /app

USER demo
EXPOSE 7681
CMD ["ttyd", "-p", "7681", "bash", "/app/demo_menu.sh"]
