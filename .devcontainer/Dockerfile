FROM --platform=linux/amd64 mcr.microsoft.com/devcontainers/base:1-bookworm
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash \
      build-essential \
      gcc \
      gdb \
      qemu-system-x86 \
      qemu-utils \
      expect \
      emacs \
      vim \
      nano \
      clang-format \
     && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /pintos
ENV PINTOS_HOME=/pintos
ENV PATH=/pintos/src/utils:$PATH
ENV TZ=Europe/Berlin
WORKDIR /pintos
