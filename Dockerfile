FROM alpine:latest

MAINTAINER weihua(hanyu363@qq.com)

COPY repositories /etc/apk/repositories
RUN apk update

RUN apk add --no-cache --virtual .build-deps \
    git \ 
    make \ 
    autoconf \
    gcc \
    g++ \
    readline \
    readline-dev \
    && git clone https://github.com/cloudwu/skynet.git \
    && cd skynet && make linux \
    && rm -rf 3rd HISTORY.md LICENSE Makefile README.md lualib-src platform.mk service-src skynet-src test .git \
    && apk del .build-deps 


ENV SKYNET_ROOT /skynet
ENV PATH $SKYNET_ROOT:$PATH

WORKDIR /skynet

ENTRYPOINT ["skynet"]

CMD ["example/config"]
