FROM debian@sha256:3a39a0592364683e6bab97937b72cad5a8fa6dcbbee90edb3bb48c7f8e94f258

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq \
    && apt-get install -y -qq cmake ninja-build g++ python3 libsdl2-dev

WORKDIR /src
