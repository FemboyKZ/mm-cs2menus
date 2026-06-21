FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest AS build

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
        python3 \
        python3-pip \
        git \
    && git clone http://github.com/alliedmodders/ambuild \
    && pip3 install ./ambuild \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Ensure submodules are present (they should be via .dockerignore exclusions or COPY).
# If building from a tarball without submodules, uncomment:
# RUN git submodule update --init --recursive

RUN mkdir -p build && cd build && \
    python3 ../configure.py \
        --sdks cs2 \
        --targets x86_64 \
        --mms_path ../metamod-source \
        --hl2sdk-manifests ../metamod-source/hl2sdk-manifests \
        --enable-optimize

RUN cd build && ambuild

# minimal image with just the built artifact
FROM alpine:latest AS output

COPY --from=build /src/build/package /package

# copy all build artifacts (addons/ and cfg/) to /output volume
CMD ["sh", "-c", "cp -r /package/* /output/"]
