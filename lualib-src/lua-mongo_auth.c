#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "mongoc-b64-private.h"
#define MONGOC_SCRAM_HASH_SIZE 20

static int
encode_base64(lua_State *L) {
	size_t      len;
	const char *src = luaL_checklstring(L, 1, &len);
	char       *output = lua_newuserdata(L, len*2);
	
	len = mongoc_b64_ntop((uint8_t*)src, len, output, len*2);
	
	lua_pushstring(L, output);
	
	return 1;
}

static int
decode_base64(lua_State *L) {
	const char *src = luaL_checkstring(L, 1);
	size_t      len = strlen(src);
	char       *output = lua_newuserdata(L, len);
	
	len = mongoc_b64_pton(src, (uint8_t*)output, len);
	
	if (len <= 0) {
		return 0;
	}
	
	lua_pushlstring(L, output, len);
	
	return 1;
}

static int
xor_str(lua_State *L) {
	size_t              la, lb;
	const unsigned char *a, *b;
	unsigned char       *output;
	int i;
	a = (const unsigned char*)luaL_checklstring(L, 1, &la);
	b = (const unsigned char*)luaL_checklstring(L, 2, &lb);
	if (la != lb) {
		return 0;
	}
	output = lua_newuserdata(L, la);
	for (i=0; i<la; i++) {
		output[i] = a[i] ^ b[i];
	}
	
	lua_pushlstring(L, (char*)output, la);
	
	return 1;
}

static int
hmac_sha1(lua_State *L) {
	const char    *sec, *sts;
	size_t        lsec, lsts;
	size_t        md_len = EVP_MAX_MD_SIZE;
	unsigned char md[EVP_MAX_MD_SIZE];
	
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expecting 2 arguments, but got %d",lua_gettop(L));
	}
	
	sec = luaL_checklstring(L, 1, &lsec);
	sts = luaL_checklstring(L, 2, &lsts);
	
	HMAC(EVP_sha1(), (unsigned char*)sec, lsec, (unsigned char*)sts, lsts, md, (unsigned int*)&md_len);
	
	lua_pushlstring(L, (char*)md, md_len);
	
	return 1;
}

static int
sha1_bin(lua_State *L) {
	EVP_MD_CTX * digest_ctx;
	int           rval = 0;
	size_t        len;
	const char   *str;
	unsigned char output[20];
	str = luaL_checklstring(L, 1, &len);
	
	digest_ctx = EVP_MD_CTX_new();
	
	EVP_MD_CTX_init (digest_ctx);
	
	for (;;) {
		if (1 != EVP_DigestInit_ex (digest_ctx, EVP_sha1 (), NULL)) {
			break;
		}
		
		if (1 != EVP_DigestUpdate (digest_ctx, str, len)) {
			break;
		}
		
		if (EVP_DigestFinal_ex (digest_ctx, output, NULL) == 1) {
			lua_pushlstring(L, (const char*)output, 20);
			rval = 1;
		}
		break;
	}
	EVP_MD_CTX_free(digest_ctx);
	
	return rval;
}

static void
_mongoc_scram_salt_password(
	uint8_t        *output,
	const char     *password,
	size_t          password_len,
	const uint8_t  *salt,
	size_t        salt_len,
	int           iterations
) {
	uint8_t intermediate_digest[MONGOC_SCRAM_HASH_SIZE];
	uint8_t start_key[MONGOC_SCRAM_HASH_SIZE];
	
	uint32_t hash_len = 0;
	int i, k;
	
	memcpy (start_key, salt, salt_len);
	start_key[salt_len] = 0;
	start_key[salt_len + 1] = 0;
	start_key[salt_len + 2] = 0;
	start_key[salt_len + 3] = 1;
	
	/* U1 = HMAC(password, salt + 0001) */
	HMAC (EVP_sha1 (),
		password, 
		password_len, 
		start_key, 
		sizeof (start_key), 
		output,
		&hash_len);
	
	memcpy (intermediate_digest, output, MONGOC_SCRAM_HASH_SIZE);
	
	/* intermediateDigest contains Ui and output contains the accumulated XOR:ed result */
	for (i = 2; i <= iterations; i++) {
		HMAC (EVP_sha1 (), 
			password, 
			password_len, 
			intermediate_digest, 
			sizeof (intermediate_digest), 
			intermediate_digest, 
			&hash_len);
      for (k = 0; k < MONGOC_SCRAM_HASH_SIZE; k++) {
         output[k] ^= intermediate_digest[k];
      }
   }
}

static int
salt_password(lua_State *L) {
	size_t lp, ls;
	const char * password, *salt;
	unsigned int iterations;
	unsigned char * output = lua_newuserdata(L, MONGOC_SCRAM_HASH_SIZE);
	password = luaL_checklstring(L, 1, &lp);
	salt = luaL_checklstring(L, 2, &ls);
	iterations = luaL_checkinteger(L, 3);
	
	_mongoc_scram_salt_password(output, password, lp, (const unsigned char*)salt, ls, iterations);
	
	lua_pushlstring(L, (char*)output, MONGOC_SCRAM_HASH_SIZE);
	return 1;
}

int
luaopen_mongo_auth(lua_State *L) {
	luaL_checkversion(L);
	
	luaL_Reg l[] = {
		{ "encode_base64", encode_base64 },
		{ "decode_base64", decode_base64 },
		{ "xor_str", xor_str },
		{ "hmac_sha1", hmac_sha1 },
		{ "sha1_bin", sha1_bin },
		{ "salt_password", salt_password },
		{ NULL, NULL},
	};
	
	luaL_newlib(L, l);
	return 1;
}
