FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk

WORKDIR /app
VOLUME /app/build

RUN apt update -o Acquire::Check-Valid-Until=false -y && apt install -y git
RUN git clone https://github.com/alliedmodders/ambuild
RUN cd ambuild && python3 setup.py install
RUN git config --global --add safe.directory /app

COPY . .
CMD [ "/bin/bash", "./docker-entrypoint.sh" ]
