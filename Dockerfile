# Referee — two-stage image. Build on Noble (native ANTLR 4.10 toolchain, per README's version note),
# ship a minimal runtime with the ldd-derived shared libs (robust against runtime package renames).
#
#   docker build -t referee:0.1.0 .
#   docker run --rm -v $PWD:/w referee:0.1.0 execute /w/spec.ref /w/trace.csv

FROM ubuntu:24.04 AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      g++ gcc meson ninja-build pkg-config cmake llvm llvm-dev \
      libantlr4-runtime-dev libcli11-dev libfmt-dev libgtest-dev \
      libspdlog-dev libyaml-cpp-dev \
      default-jre-headless ca-certificates curl git \
    && rm -rf /var/lib/apt/lists/*
RUN curl -L -o /antlr.jar https://www.antlr.org/download/antlr-4.10.1-complete.jar
COPY . /src
RUN cd /src \
 && meson setup build-rel -Dantlr4_jar=/antlr.jar -Dbuildtype=release \
 && ninja -C build-rel referee rdb referee-lsp
# collect the exact shared libs the binaries link (no guessing at runtime package names)
RUN mkdir -p /opt/deps \
 && for b in /src/build-rel/referee /src/build-rel/rdb /src/build-rel/referee-lsp; do \
      ldd "$b" | awk '$3 ~ /^\// {print $3}' | sort -u; \
    done | sort -u | xargs -I{} cp -L --parents {} /opt/deps/

FROM ubuntu:24.04
COPY --from=build /opt/deps/ /
COPY --from=build /src/build-rel/referee /src/build-rel/rdb /src/build-rel/referee-lsp /usr/local/bin/
RUN ldconfig
ENTRYPOINT ["referee"]
CMD ["--help"]
