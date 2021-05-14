#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h> 
#include <openssl/err.h>
#include <openssl/dh.h>
#include <openssl/ssl.h>
#include <openssl/conf.h>
#include <openssl/engine.h>
#include <lua.h>
#include <lauxlib.h>


static bool TLS_IS_INIT = false;

struct tls_context {
    SSL* ssl;
    BIO* in_bio;
    BIO* out_bio;
    bool is_server;
    bool is_close;
};

struct ssl_ctx {
    SSL_CTX* ctx;
};

// static int
// _ssl_verify_peer(int ok, X509_STORE_CTX* ctx) {
//     return 1;
// }


static void
_init_bio(lua_State* L, struct tls_context* tls_p, struct ssl_ctx* ctx_p) {
    tls_p->ssl = SSL_new(ctx_p->ctx);
    if(!tls_p->ssl) {
        luaL_error(L, "SSL_new faild");
    }

    tls_p->in_bio = BIO_new(BIO_s_mem());
    if(!tls_p->in_bio) {
        luaL_error(L, "new in bio faild");
    }
    BIO_set_mem_eof_return(tls_p->in_bio, -1); /* see: https://www.openssl.org/docs/crypto/BIO_s_mem.html */

    tls_p->out_bio = BIO_new(BIO_s_mem());
    if(!tls_p->out_bio) {
        luaL_error(L, "new out bio faild");
    }
    BIO_set_mem_eof_return(tls_p->out_bio, -1); /* see: https://www.openssl.org/docs/crypto/BIO_s_mem.html */

    SSL_set_bio(tls_p->ssl, tls_p->in_bio, tls_p->out_bio);
}


static void
_init_client_context(lua_State* L, struct tls_context* tls_p, struct ssl_ctx* ctx_p) {
    tls_p->is_server = false;
    _init_bio(L, tls_p, ctx_p);
    SSL_set_connect_state(tls_p->ssl);
}

static void
_init_server_context(lua_State* L, struct tls_context* tls_p, struct ssl_ctx* ctx_p) {
    tls_p->is_server = true;
    _init_bio(L, tls_p, ctx_p);
    SSL_set_accept_state(tls_p->ssl);
}

static struct tls_context *
_check_context(lua_State* L, int idx) {
    struct tls_context* tls_p = (struct tls_context*)lua_touserdata(L, idx);
    if(!tls_p) {
        luaL_error(L, "need tls context");
    }
    if(tls_p->is_close) {
        luaL_error(L, "context is closed");
    }
    return tls_p;
}

static struct ssl_ctx *
_check_sslctx(lua_State* L, int idx) {
    struct ssl_ctx* ctx_p = (struct ssl_ctx*)lua_touserdata(L, idx);
    if(!ctx_p) {
        luaL_error(L, "need sslctx");
    }
    return ctx_p;
}

static int
_ltls_context_finished(lua_State* L) {
    struct tls_context* tls_p = _check_context(L, 1);
    int b = SSL_is_init_finished(tls_p->ssl);
    lua_pushboolean(L, b);
    return 1;
}

static int
_ltls_context_close(lua_State* L) {
    struct tls_context* tls_p = lua_touserdata(L, 1);
    if(!tls_p->is_close) {
        SSL_free(tls_p->ssl);
        tls_p->ssl = NULL;
        tls_p->in_bio = NULL; //in_bio and out_bio will be free when SSL_free is called
        tls_p->out_bio = NULL;
        tls_p->is_close = true;
    }
    return 0;
}

static int
_bio_read(lua_State* L, struct tls_context* tls_p) {
    char outbuff[4096];
    int all_read = 0;
    int read = 0;
    int pending = BIO_ctrl_pending(tls_p->out_bio);
    if(pending >0) {
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        while(pending > 0) {
            read = BIO_read(tls_p->out_bio, outbuff, sizeof(outbuff));
            // printf("BIO_read read:%d pending:%d\n", read, pending);
            if(read <= 0) {
                luaL_error(L, "BIO_read error:%d", read);
            }else if(read <= sizeof(outbuff)) {
                all_read += read;
                luaL_addlstring(&b, (const char*)outbuff, read);
            }else {
                luaL_error(L, "invalid BIO_read:%d", read);
            }
            pending = BIO_ctrl_pending(tls_p->out_bio);
        }
        if(all_read>0) {
            luaL_pushresult(&b);
        }
    }
    return all_read;
}


static void
_bio_write(lua_State* L, struct tls_context* tls_p, const char* s, size_t len) {
    char* p = (char*)s;
    size_t sz = len;
    while(sz > 0) {
        int written = BIO_write(tls_p->in_bio, p, sz);
        // printf("BIO_write written:%d sz:%zu\n", written, sz);
        if(written <= 0) {
            luaL_error(L, "BIO_write error:%d", written);
        }else if (written <= sz) {
            p += written;
            sz -= written;
        }else {
            luaL_error(L, "invalid BIO_write:%d", written);
        }
    }
}


static int
_ltls_context_handshake(lua_State* L) {
    struct tls_context* tls_p = _check_context(L, 1);
    size_t slen = 0;
    const char* exchange = lua_tolstring(L, 2, &slen);

    // check handshake is finished
    if(SSL_is_init_finished(tls_p->ssl)) {
        luaL_error(L, "handshake is finished");
    }

    // handshake exchange
    if(slen > 0 && exchange != NULL) {
        _bio_write(L, tls_p, exchange, slen);
    }

    // first handshake; initiated by client
    if(!SSL_is_init_finished(tls_p->ssl)) {
        int ret = SSL_do_handshake(tls_p->ssl);
        if(ret == 1) {
            return 0;
        } else if (ret < 0) {
            int err = SSL_get_error(tls_p->ssl, ret);
            ERR_clear_error();
            if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                int all_read = _bio_read(L, tls_p);
                if(all_read>0) {
                    return 1;
                }
            } else {
                luaL_error(L, "SSL_do_handshake error:%d ret:%d", err, ret);
            }
        } else {
            int err = SSL_get_error(tls_p->ssl, ret);
            ERR_clear_error();
            luaL_error(L, "SSL_do_handshake error:%d ret:%d", err, ret);
        }
    }
    return 0;
}


static int
_ltls_context_read(lua_State* L) {
    struct tls_context* tls_p = _check_context(L, 1);
    size_t slen = 0;
    const char* encrypted_data = lua_tolstring(L, 2, &slen);

    // write encrypted data
    if(slen>0 && encrypted_data) {
        _bio_write(L, tls_p, encrypted_data, slen);
    }

    char outbuff[4096];
    int read = 0;
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    do {
        read = SSL_read(tls_p->ssl, outbuff, sizeof(outbuff));
        if(read <= 0) {
            int err = SSL_get_error(tls_p->ssl, read);
            ERR_clear_error();
            if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                break;
            }
            luaL_error(L, "SSL_read error:%d", err);
        }else if(read <= sizeof(outbuff)) {
            luaL_addlstring(&b, outbuff, read);
        }else {
            luaL_error(L, "invalid SSL_read:%d", read);
        }
    }while(true);
    luaL_pushresult(&b);
    return 1;
}


static int
_ltls_context_write(lua_State* L) {
    struct tls_context* tls_p = _check_context(L, 1);
    size_t slen = 0;
    char* unencrypted_data = (char*)lua_tolstring(L, 2, &slen);

    while(slen > 0) {
        int written = SSL_write(tls_p->ssl, unencrypted_data,  slen);
        if(written <= 0) {
            int err = SSL_get_error(tls_p->ssl, written);
            ERR_clear_error();
            luaL_error(L, "SSL_write error:%d", err);
        }else if(written <= slen) {
            unencrypted_data += written;
            slen -= written;
        }else {
            luaL_error(L, "invalid SSL_write:%d", written);
        }
    }

    int all_read = _bio_read(L, tls_p);
    if(all_read <= 0) {
        lua_pushstring(L, "");
    }
    return 1;
}


static int
_lctx_gc(lua_State* L) {
    struct ssl_ctx* ctx_p = _check_sslctx(L, 1);
    if(ctx_p->ctx) {
        SSL_CTX_free(ctx_p->ctx);
        ctx_p->ctx = NULL;
    }
    return 0;
}

static int
_lctx_cert(lua_State* L) {
    struct ssl_ctx* ctx_p = _check_sslctx(L, 1);
    const char* certfile = lua_tostring(L, 2);
    const char* key = lua_tostring(L, 3);
    if(!certfile) {
        luaL_error(L, "need certfile");
    }

    if(!key) {
        luaL_error(L, "need private key");
    }

    int ret = SSL_CTX_use_certificate_chain_file(ctx_p->ctx, certfile);
    if(ret != 1) {
        luaL_error(L, "SSL_CTX_use_certificate_chain_file error:%d", ret);
    }

    ret = SSL_CTX_use_PrivateKey_file(ctx_p->ctx, key, SSL_FILETYPE_PEM);
    if(ret != 1) {
        luaL_error(L, "SSL_CTX_use_PrivateKey_file error:%d", ret);
    }
    ret = SSL_CTX_check_private_key(ctx_p->ctx);
    if(ret != 1) {
        luaL_error(L, "SSL_CTX_check_private_key error:%d", ret);
    }
    return 0;
}

static int
_lctx_ciphers(lua_State* L) {
    struct ssl_ctx* ctx_p = _check_sslctx(L, 1);
    const char* s = lua_tostring(L, 2);
    if(!s) {
        luaL_error(L, "need cipher list");
    }
    int ret = SSL_CTX_set_tlsext_use_srtp(ctx_p->ctx, s);
    if(ret != 0) {
        luaL_error(L, "SSL_CTX_set_tlsext_use_srtp error:%d", ret);
    }
    return 0;
}


static int
lnew_ctx(lua_State* L) {
    struct ssl_ctx* ctx_p = (struct ssl_ctx*)lua_newuserdatauv(L, sizeof(*ctx_p), 0);
    ctx_p->ctx = SSL_CTX_new(SSLv23_method());
    if(!ctx_p->ctx) {
        unsigned int err = ERR_get_error();
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        luaL_error(L, "SSL_CTX_new client faild. %s\n", buf);
    }

    if(luaL_newmetatable(L, "_TLS_SSLCTX_METATABLE_")) {
        luaL_Reg l[] = {
            {"set_ciphers", _lctx_ciphers},
            {"set_cert", _lctx_cert},
            {NULL, NULL},
        };

        luaL_newlib(L, l);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, _lctx_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}


static int
lnew_tls(lua_State* L) {
    struct tls_context* tls_p = (struct tls_context*)lua_newuserdatauv(L, sizeof(*tls_p), 1);
    tls_p->is_close = false;
    const char* method = luaL_optstring(L, 1, "nil");
    struct ssl_ctx* ctx_p = _check_sslctx(L, 2);
    lua_pushvalue(L, 2);
    lua_setiuservalue(L, -2, 1); // set ssl_ctx associated to tls_context

    if(strcmp(method, "client") == 0) {
        _init_client_context(L, tls_p, ctx_p);
    }else if(strcmp(method, "server") == 0) {
        _init_server_context(L, tls_p, ctx_p);
    } else {
        luaL_error(L, "invalid method:%s e.g[server, client]", method);
    }

    if(luaL_newmetatable(L, "_TLS_CONTEXT_METATABLE_")) {
        luaL_Reg l[] = {
            {"close", _ltls_context_close},
            {"finished", _ltls_context_finished},
            {"handshake", _ltls_context_handshake},
            {"read", _ltls_context_read},
            {"write", _ltls_context_write},
            {NULL, NULL},
        };
        luaL_newlib(L, l);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, _ltls_context_close);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}

int
luaopen_ltls_c(lua_State* L) {
    if(!TLS_IS_INIT) {
        luaL_error(L, "ltls need init, Put enablessl = true in you config file.");
    }
    luaL_Reg l[] = {
        {"newctx", lnew_ctx},
        {"newtls", lnew_tls},
        {NULL, NULL},
    };
    luaL_checkversion(L);
    luaL_newlib(L, l);
    return 1;
}

// for ltls init
static int
ltls_init_constructor(lua_State* L) {
#ifndef OPENSSL_EXTERNAL_INITIALIZATION
    if(!TLS_IS_INIT) {
        SSL_library_init();
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        OpenSSL_add_all_algorithms();
    }
#endif
    TLS_IS_INIT = true;
    return 0;
}

static int
ltls_init_destructor(lua_State* L) {
#ifndef OPENSSL_EXTERNAL_INITIALIZATION
    if(TLS_IS_INIT) {
        ENGINE_cleanup();
        CONF_modules_unload(1);
        ERR_free_strings();
        EVP_cleanup();
        sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
        CRYPTO_cleanup_all_ex_data();
    }
#endif
    TLS_IS_INIT = false;
    return 0;
}

int
luaopen_ltls_init_c(lua_State* L) {
    luaL_Reg l[] = {
        {"constructor", ltls_init_constructor},
        {"destructor", ltls_init_destructor},
        {NULL, NULL},
    };
    luaL_checkversion(L);
    luaL_newlib(L, l);
    return 1;
}
