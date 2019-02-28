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

#include "spinlock.h"


static struct spinlock LOCK = {0};
#define TLS_LOCK() spinlock_lock(&LOCK)
#define TLS_UNLCOK() spinlock_unlock(&LOCK)

static bool TLS_IS_INIT = false;

struct tls_context {
    SSL_CTX* ctx;
    SSL* ssl;
    BIO* in_bio;
    BIO* out_bio;
    bool is_server;
    bool is_close;
};


static void 
_tls_init() {
    if(TLS_IS_INIT) {
        return;
    }
    TLS_LOCK();
    TLS_IS_INIT = true;
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
    TLS_UNLCOK();
}


// would be never called
/*
static void
_tls_destory() {
    TLS_IS_INIT = false;
    ERR_remove_state(0);
    ENGINE_cleanup();
    CONF_modules_unload(1);
    ERR_free_strings();
    EVP_cleanup();
    sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
    CRYPTO_cleanup_all_ex_data();
}
*/

// static int
// _ssl_verify_peer(int ok, X509_STORE_CTX* ctx) {
//     return 1;
// }


static void
_init_bio(lua_State* L, struct tls_context* tls_p) {
    tls_p->ssl = SSL_new(tls_p->ctx);
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
_init_client_context(lua_State* L, struct tls_context* tls_p) {
    tls_p->ctx = SSL_CTX_new(TLSv1_2_client_method());
    if(!tls_p->ctx) {
        luaL_error(L, "SSL_CTX_new client faild.");
    }

    tls_p->is_server = false;
    _init_bio(L, tls_p);
    SSL_set_connect_state(tls_p->ssl);
}

static void
_init_server_context(lua_State* L, struct tls_context* tls_p) {
    luaL_error(L, "new server todo it!");

    tls_p->is_server = true;
    _init_bio(L, tls_p);
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
        SSL_CTX_free(tls_p->ctx);
        tls_p->ctx = NULL;
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
        do {
            read = BIO_read(tls_p->out_bio, outbuff, sizeof(outbuff));
            // printf("_bio_read:%d pending:%d\n", read, pending);
            if(read > sizeof(outbuff)) {
                luaL_error(L, "invalid BIO_read:%d", read);
            }else if(read == -2) {
                luaL_error(L, "BIO_read not implemented in the specific BIO type");
            }else if (read > 0) {
                all_read += read;
                luaL_addlstring(&b, (const char*)outbuff, read);
            }
        }while(read == sizeof(outbuff));
        if(all_read>0){
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
        if(written > sz) {
            luaL_error(L, "invalid BIO_write");
        }else if(written > 0) {
            p += written;
            sz -= written;
        }else if (written == -2) {
            luaL_error(L, "BIO_write not implemented in the specific BIO type");
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
        } else if (ret == -1) {
            int all_read = _bio_read(L, tls_p);
            if(all_read>0) {
                return 1;
            }
        } else {
            int err = SSL_get_error(tls_p->ssl, ret);
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
        if(read < 0) {
            int err = SSL_get_error(tls_p->ssl, read);
            if(err == SSL_ERROR_WANT_READ) {
                break;
            }
            luaL_error(L, "SSL_read error:%d", err);
        }else if(read > sizeof(outbuff)) {
            luaL_error(L, "invalid SSL_read");
        }else if (read > 0) {
            luaL_addlstring(&b, outbuff, read);
        }
    }while(read == sizeof(outbuff));
    luaL_pushresult(&b);
    return 1;
}


static int
_ltls_context_write(lua_State* L) {
    struct tls_context* tls_p = _check_context(L, 1);
    size_t slen = 0;
    char* unencrypted_data = (char*)lua_tolstring(L, 2, &slen);

    if(slen <=0 || !unencrypted_data) {
        luaL_error(L, "need unencrypted data");
    }

    while(slen >0) {
        int written = SSL_write(tls_p->ssl, unencrypted_data,  slen);
        if(written < 0) {
            int err = SSL_get_error(tls_p->ssl, written);
            luaL_error(L, "SSL_write error:%d", err);
        }else if(written > slen) {
            luaL_error(L, "invalid SSL_write");
        }else if(written>0) {
            unencrypted_data += written;
            slen -= written;
        }
    }

    int all_read = _bio_read(L, tls_p);
    if(all_read <= 0) {
        luaL_error(L, "bio_read error when _ltls_context_write");
    }
    return 1;
}


static int
lnew_tls_context(lua_State* L) {
    /* create a new context using DTLS */
    struct tls_context* tls_p = (struct tls_context*)lua_newuserdata(L, sizeof(*tls_p));
    tls_p->is_close = false;
    const char* method = luaL_optstring(L, 1, "client");
    if(strcmp(method, "client") == 0) {
        _init_client_context(L, tls_p);
    }else if(strcmp(method, "server") == 0) {
        _init_server_context(L, tls_p);
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
    luaL_checkversion(L);
    luaL_Reg l[] = {
        {"new_tls_context", lnew_tls_context},
        {NULL, NULL},
    };

    _tls_init(); // init openssl lib
    luaL_newlib(L, l);
    return 1;
}
