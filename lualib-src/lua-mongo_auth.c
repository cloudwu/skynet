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

#define Assert(...)

static const char Base64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char Pad64 = '=';

static int mongoc_b64rmap_initialized = 0;
static unsigned char mongoc_b64rmap[256];

static const unsigned char mongoc_b64rmap_special = 0xf0;
static const unsigned char mongoc_b64rmap_end = 0xfd;
static const unsigned char mongoc_b64rmap_space = 0xfe;
static const unsigned char mongoc_b64rmap_invalid = 0xff;

/**
 * Initializing the reverse map is not thread safe.
 * Which is fine for NSD. For now...
 **/
void
mongoc_b64_initialize_rmap (void)
{
	int i;
	unsigned char ch;

	/* Null: end of string, stop parsing */
	mongoc_b64rmap[0] = mongoc_b64rmap_end;

	for (i = 1; i < 256; ++i) {
		ch = (unsigned char)i;
		/* Whitespaces */
		if (isspace(ch))
			mongoc_b64rmap[i] = mongoc_b64rmap_space;
		/* Padding: stop parsing */
		else if (ch == Pad64)
			mongoc_b64rmap[i] = mongoc_b64rmap_end;
		/* Non-base64 char */
		else
			mongoc_b64rmap[i] = mongoc_b64rmap_invalid;
	}

	/* Fill reverse mapping for base64 chars */
	for (i = 0; Base64[i] != '\0'; ++i)
		mongoc_b64rmap[(unsigned char)Base64[i]] = i;

	mongoc_b64rmap_initialized = 1;
}

static int
mongoc_b64_pton_do(char const *src, unsigned char *target, size_t targsize)
{
	int tarindex, state, ch;
	unsigned char ofs;

	state = 0;
	tarindex = 0;

	while (1)
	{
		ch = *src++;
		ofs = mongoc_b64rmap[ch];

		if (ofs >= mongoc_b64rmap_special) {
			/* Ignore whitespaces */
			if (ofs == mongoc_b64rmap_space)
				continue;
			/* End of base64 characters */
			if (ofs == mongoc_b64rmap_end)
				break;
			/* A non-base64 character. */
			return (-1);
		}

		switch (state) {
		case 0:
			if ((size_t)tarindex >= targsize)
				return (-1);
			target[tarindex] = ofs << 2;
			state = 1;
			break;
		case 1:
			if ((size_t)tarindex + 1 >= targsize)
				return (-1);
			target[tarindex]   |=  ofs >> 4;
			target[tarindex+1]  = (ofs & 0x0f)
						<< 4 ;
			tarindex++;
			state = 2;
			break;
		case 2:
			if ((size_t)tarindex + 1 >= targsize)
				return (-1);
			target[tarindex]   |=  ofs >> 2;
			target[tarindex+1]  = (ofs & 0x03)
						<< 6;
			tarindex++;
			state = 3;
			break;
		case 3:
			if ((size_t)tarindex >= targsize)
				return (-1);
			target[tarindex] |= ofs;
			tarindex++;
			state = 0;
			break;
		default:
			abort();
		}
	}

	/*
	 * We are done decoding Base-64 chars.  Let's see if we ended
	 * on a byte boundary, and/or with erroneous trailing characters.
	 */

	if (ch == Pad64) {		/* We got a pad char. */
		ch = *src++;		/* Skip it, get next. */
		switch (state) {
		case 0:		/* Invalid = in first position */
		case 1:		/* Invalid = in second position */
			return (-1);

		case 2:		/* Valid, means one byte of info */
			/* Skip any number of spaces. */
			for ((void)NULL; ch != '\0'; ch = *src++)
				if (mongoc_b64rmap[ch] != mongoc_b64rmap_space)
					break;
			/* Make sure there is another trailing = sign. */
			if (ch != Pad64)
				return (-1);
			ch = *src++;		/* Skip the = */
			/* Fall through to "single trailing =" case. */
			/* FALLTHROUGH */

		case 3:		/* Valid, means two bytes of info */
			/*
			 * We know this char is an =.  Is there anything but
			 * whitespace after it?
			 */
			for ((void)NULL; ch != '\0'; ch = *src++)
				if (mongoc_b64rmap[ch] != mongoc_b64rmap_space)
					return (-1);

			/*
			 * Now make sure for cases 2 and 3 that the "extra"
			 * bits that slopped past the last full byte were
			 * zeros.  If we don't check them, they become a
			 * subliminal channel.
			 */
			if (target[tarindex] != 0)
				return (-1);
		default:
			break;
		}
	} else {
		/*
		 * We ended by seeing the end of the string.  Make sure we
		 * have no partial bytes lying around.
		 */
		if (state != 0)
			return (-1);
	}

	return (tarindex);
}


static int
mongoc_b64_pton_len(char const *src)
{
	int tarindex, state, ch;
	unsigned char ofs;

	state = 0;
	tarindex = 0;

	while (1)
	{
		ch = *src++;
		ofs = mongoc_b64rmap[ch];

		if (ofs >= mongoc_b64rmap_special) {
			/* Ignore whitespaces */
			if (ofs == mongoc_b64rmap_space)
				continue;
			/* End of base64 characters */
			if (ofs == mongoc_b64rmap_end)
				break;
			/* A non-base64 character. */
			return (-1);
		}

		switch (state) {
		case 0:
			state = 1;
			break;
		case 1:
			tarindex++;
			state = 2;
			break;
		case 2:
			tarindex++;
			state = 3;
			break;
		case 3:
			tarindex++;
			state = 0;
			break;
		default:
			abort();
		}
	}

	/*
	 * We are done decoding Base-64 chars.  Let's see if we ended
	 * on a byte boundary, and/or with erroneous trailing characters.
	 */

	if (ch == Pad64) {		/* We got a pad char. */
		ch = *src++;		/* Skip it, get next. */
		switch (state) {
		case 0:		/* Invalid = in first position */
		case 1:		/* Invalid = in second position */
			return (-1);

		case 2:		/* Valid, means one byte of info */
			/* Skip any number of spaces. */
			for ((void)NULL; ch != '\0'; ch = *src++)
				if (mongoc_b64rmap[ch] != mongoc_b64rmap_space)
					break;
			/* Make sure there is another trailing = sign. */
			if (ch != Pad64)
				return (-1);
			ch = *src++;		/* Skip the = */
			/* Fall through to "single trailing =" case. */
			/* FALLTHROUGH */

		case 3:		/* Valid, means two bytes of info */
			/*
			 * We know this char is an =.  Is there anything but
			 * whitespace after it?
			 */
			for ((void)NULL; ch != '\0'; ch = *src++)
				if (mongoc_b64rmap[ch] != mongoc_b64rmap_space)
					return (-1);

		default:
			break;
		}
	} else {
		/*
		 * We ended by seeing the end of the string.  Make sure we
		 * have no partial bytes lying around.
		 */
		if (state != 0)
			return (-1);
	}

	return (tarindex);
}

int
mongoc_b64_pton(char const *src, unsigned char *target, size_t targsize)
{
	if (!mongoc_b64rmap_initialized)
		mongoc_b64_initialize_rmap ();

	if (target)
		return mongoc_b64_pton_do (src, target, targsize);
	else
		return mongoc_b64_pton_len (src);
}

int Base64encode_len(int len)
{
	return ((len + 2) / 3 * 4) + 4;
}

int Base64encode(char *target, int targsize, const char *src, int srclength)
{
   int datalength = 0;
   unsigned char input[3];
   unsigned char output[4];
   int i;

   while (2 < srclength) {
	  input[0] = *src++;
	  input[1] = *src++;
	  input[2] = *src++;
	  srclength -= 3;

	  output[0] = input[0] >> 2;
	  output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
	  output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);
	  output[3] = input[2] & 0x3f;
	  Assert (output[0] < 64);
	  Assert (output[1] < 64);
	  Assert (output[2] < 64);
	  Assert (output[3] < 64);

	  if (datalength + 4 > targsize) {
		 return -1;
	  }
	  target[datalength++] = Base64[output[0]];
	  target[datalength++] = Base64[output[1]];
	  target[datalength++] = Base64[output[2]];
	  target[datalength++] = Base64[output[3]];
   }

   /* Now we worry about padding. */
   if (0 != srclength) {
	  /* Get what's left. */
	  input[0] = input[1] = input[2] = '\0';

	  for (i = 0; i < srclength; i++) {
		 input[i] = *src++;
	  }
	  output[0] = input[0] >> 2;
	  output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
	  output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);
	  Assert (output[0] < 64);
	  Assert (output[1] < 64);
	  Assert (output[2] < 64);

	  if (datalength + 4 > targsize) {
		 return -1;
	  }
	  target[datalength++] = Base64[output[0]];
	  target[datalength++] = Base64[output[1]];

	  if (srclength == 1) {
		 target[datalength++] = Pad64;
	  } else{
		 target[datalength++] = Base64[output[2]];
	  }
	  target[datalength++] = Pad64;
   }

   if (datalength >= targsize) {
	  return -1;
   }
   target[datalength] = '\0'; /* Returned value doesn't count \0. */
   return (int)datalength;
}

static int
encode_base64(lua_State *L){
	size_t len, olen;
	const char * str;
	char *ostr;
	str = luaL_checklstring(L, 1, &len);
	olen = Base64encode_len(len);
	ostr = lua_newuserdata(L, olen);
	olen = Base64encode(ostr, olen, str, len);
	
	lua_pushlstring(L, ostr, olen);
	
	return 1;
}

static int
decode_base64(lua_State *L){
	int olen;
	const char * str;
	char *ostr;
	str = luaL_checkstring(L, 1);
	olen = 1024;
	ostr = lua_newuserdata(L, olen);
	olen = mongoc_b64_pton(str, (unsigned char*)ostr, olen);
	if (olen <= 0) {
		return 0;
	}
	lua_pushlstring(L, ostr, olen);
	
	return 1;
}

static int
hmac_sha1(lua_State *L) {
	u_char				  *sec, *sts;
	size_t				   lsec, lsts;
	unsigned int			 md_len;
	unsigned char			md[EVP_MAX_MD_SIZE];
	const EVP_MD			*evp_md;

	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expecting 2 arguments, but got %d",
						  lua_gettop(L));
	}

	sec = (u_char *) luaL_checklstring(L, 1, &lsec);
	sts = (u_char *) luaL_checklstring(L, 2, &lsts);

	evp_md = EVP_sha1();

	HMAC(evp_md, sec, lsec, sts, lsts, md, &md_len);

	lua_pushlstring(L, (char *) md, md_len);

	return 1;
}

static int
xor_str(lua_State *L) {
	siz_t la, lb;
	int i;
	const unsigned char *a, *b;
	unsigned char * output;
	a = (const unsigned char*)luaL_checklstring(L, 1, &la);
	b = (const unsigned char*)luaL_checklstring(L, 2, &lb);
	if (la != 20 || lb != 20) {
		return 0;
	}
	output = lua_newuserdata(L, 20);
	for (i=0; i<20; i++) {
		output[i] = a[i] ^ b[i];
	}
	
	lua_pushlstring(L, output, 20);
	
	return 1;
}
#define MONGOC_SCRAM_HASH_SIZE 20
static int
sha1_bin(lua_State *L)
{
	EVP_MD_CTX * digest_ctx;
	int rval = 0;
	size_t len;
	const char * str;
	unsigned char output[20];
	str = luaL_checklstring(L, 1, &len);

	digest_ctx = EVP_MD_CTX_new();
	EVP_MD_CTX_init (digest_ctx);

	if (1 != EVP_DigestInit_ex (digest_ctx, EVP_sha1 (), NULL)) {
		goto cleanup;
	}
	
	if (1 != EVP_DigestUpdate (digest_ctx, str, len)) {
		goto cleanup;
	}
	
	if (EVP_DigestFinal_ex (digest_ctx, output, NULL) == 1) {
		lua_pushlstring(L, (const char*)output, 20);
		rval = 1;
	}
	
cleanup:
	EVP_MD_CTX_free(digest_ctx);
	
	return rval;
}

static void
_mongoc_scram_salt_password (unsigned char       *output,
							 const char          *password,
							 size_t               password_len,
							 const unsigned char *salt,
							 size_t               salt_len,
							 int                  iterations)
{
   unsigned char intermediate_digest[MONGOC_SCRAM_HASH_SIZE];
   unsigned char start_key[MONGOC_SCRAM_HASH_SIZE];

   /* Placeholder for HMAC return size, will always be scram::hashSize for HMAC SHA-1 */
   uint32_t hash_len = 0;
   int i;
   int k;

   memcpy (start_key, salt, salt_len);

   start_key[salt_len] = 0;
   start_key[salt_len + 1] = 0;
   start_key[salt_len + 2] = 0;
   start_key[salt_len + 3] = 1;

   /* U1 = HMAC(input, salt + 0001) */
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
	unsigned char * output = lua_newuserdata(L, 20);
	password = luaL_checklstring(L, 1, &lp);
	salt = luaL_checklstring(L, 2, &ls);
	iterations = luaL_checkinteger(L, 3);
	
	_mongoc_scram_salt_password(output, password, lp, (const unsigned char*)salt, ls, iterations);
	
	lua_pushlstring(L, (char*)output, 20);
	return 1;
}

int
luaopen_mongo_auth(lua_State *L) {
	luaL_checkversion(L);
	
	luaL_Reg l[] = {
		{ "encode_base64", encode_base64 },
		{ "decode_base64", decode_base64 },
		{ "hmac_sha1", hmac_sha1 },
		{ "xor_str", xor_str },
		{ "sha1_bin", sha1_bin },
		{ "salt_password", salt_password },
		{ NULL, NULL},
	};
	
	luaL_newlib(L, l);
	return 1;
}
