//
//  lua_mysqlaux.c
//
//  Created by changfeng on 6/17/14.
//  Copyright (c) 2014 changfeng. All rights reserved.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#define SHA1SIZE       20
#define ROTL(bits,word) (((word) << (bits)) | ((word) >> (32-(bits))))
typedef unsigned int uint32_t;

struct sha
{
    uint32_t digest[5];
    uint32_t w[80];
    uint32_t a,b,c,d,e,f;
    int err;
};


static uint32_t padded_length_in_bits(uint32_t len)
{
    if(len%64 == 56)
    {
        len++;
    }
    while((len%64)!=56)
    {
        len++;
    }
    return len*8;
}


static int calculate_sha1(struct sha *sha1, unsigned char *text, uint32_t length)
{
    unsigned int i,j;
    unsigned char *buffer=NULL, *pbuffer=NULL;
    uint32_t bits=0;
    uint32_t temp=0,k=0;
    uint32_t lb = length*8;
    
    if (!sha1)
    {
        return 0;
    }
    // initialize the default digest values
    sha1->digest[0] = 0x67452301;
    sha1->digest[1] = 0xEFCDAB89;
    sha1->digest[2] = 0x98BADCFE;
    sha1->digest[3] = 0x10325476;
    sha1->digest[4] = 0xC3D2E1F0;
    sha1->a=sha1->b=sha1->c=sha1->d=sha1->e=sha1->f=0;
    if (!text || !length)
    {
        return 0;
    }
    
    bits = padded_length_in_bits(length);
    buffer = (unsigned char *) malloc((bits/8)+8);
    memset(buffer,0,(bits/8)+8);
    if(buffer == NULL)
    {
    	return 1;
    }
    pbuffer = buffer;
    memcpy(buffer, text, length);
    
    
    //add 1 on the last of the message..
    *(buffer+length) = 0x80;
    for(i=length+1; i<(bits/8); i++)
    {
        *(buffer+i) = 0x00;
    }
    
    *(buffer +(bits/8)+4+0) = (lb>>24) & 0xFF;
    *(buffer +(bits/8)+4+1) = (lb>>16) & 0xFF;
    *(buffer +(bits/8)+4+2) = (lb>>8) & 0xFF;
    *(buffer +(bits/8)+4+3) = (lb>>0) & 0xFF;
    
    
    //main loop
    for(i=0; i<((bits+64)/512); i++)
    {
        //first empty the block for each pass..
        for(j=0; j<80; j++)
        {
            sha1->w[j] = 0x00;
        }
        
        
        //fill the first 16 words with the characters read directly from the buffer.
        for(j=0; j<16; j++)
        {
            sha1->w[j] =buffer[j*4+0];
            sha1->w[j] = sha1->w[j]<<8;
            sha1->w[j] |= buffer[j*4+1];
            sha1->w[j] = sha1->w[j]<<8;
            sha1->w[j] |= buffer[j*4+2];
            sha1->w[j] = sha1->w[j]<<8;
            sha1->w[j] |= buffer[j*4+3];
        }
        
        //fill the rest 64 words using the formula
        for(j=16; j<80; j++)
        {
            sha1->w[j] = (ROTL(1,(sha1->w[j-3] ^ sha1->w[j-8] ^ sha1->w[j-14] ^ sha1->w[j-16])));
        }
        
        
        //initialize hash for this chunck reading that has been stored in the structure digest
        sha1->a = sha1->digest[0];
        sha1->b = sha1->digest[1];
        sha1->c = sha1->digest[2];
        sha1->d = sha1->digest[3];
        sha1->e = sha1->digest[4];
        
		//for all the 80 32bit blocks calculate f and use k accordingly per specification.
        for(j=0; j<80; j++)
        {
            if((j>=0) && (j<20))
            {
                sha1->f = ((sha1->b)&(sha1->c)) | ((~(sha1->b))&(sha1->d));
                k = 0x5A827999;
                
            }
            else if((j>=20) && (j<40))
            {
                sha1->f = (sha1->b)^(sha1->c)^(sha1->d);
                k = 0x6ED9EBA1;
            }
            else if((j>=40) && (j<60))
            {
                sha1->f = ((sha1->b)&(sha1->c)) | ((sha1->b)&(sha1->d)) | ((sha1->c)&(sha1->d));
                k = 0x8F1BBCDC;
            }
            else if((j>=60) && (j<80))
            {
                sha1->f = (sha1->b)^(sha1->c)^(sha1->d);
                k = 0xCA62C1D6;
            }
            
            temp = ROTL(5,(sha1->a)) + (sha1->f) + (sha1->e) + k + sha1->w[j];
            sha1->e = (sha1->d);
            sha1->d = (sha1->c);
            sha1->c = ROTL(30,(sha1->b));
            sha1->b = (sha1->a);
            sha1->a = temp;
            
            //reset temp to 0 to be in safe side only, not mandatory.
            temp =0x00;
            
            
        }
        
        // append to total hash.
        sha1->digest[0] += sha1->a;
        sha1->digest[1] += sha1->b;
        sha1->digest[2] += sha1->c;
        sha1->digest[3] += sha1->d;
        sha1->digest[4] += sha1->e;
        
        
		//since we used 512bit size block per each pass, let us update the buffer pointer accordingly.
        buffer = buffer+64;
        
    }
    free(pbuffer);
    return 0;
}

static void int2ch4(int intVal,unsigned char *result)
{
    result[0]= (unsigned char)((intVal>>24) & 0x000000ff);
    result[1]= (unsigned char)((intVal>>16) & 0x000000ff);
    result[2]= (unsigned char)((intVal>> 8) & 0x000000ff);
    result[3]= (unsigned char)((intVal>> 0) & 0x000000ff);
}


static int sha1_bin (lua_State *L) {
    void * msg = NULL;
    size_t len =0;

    if( lua_gettop(L) != 1 ){
        return 0;
    }	
    if( lua_isnil(L,1) ) {
        msg = NULL;
        len =0;
    }else{
        msg=luaL_checklstring(L,1,&len);
    }
    struct sha tmpsha;
    calculate_sha1( &tmpsha, msg, (uint32_t)len);
    unsigned char tmpret[SHA1SIZE+8];
    memset(tmpret,0,SHA1SIZE+8);
    int i=0;
    for ( i=0; i<5; i++)
    {
        int2ch4(tmpsha.digest[i], tmpret+i*4);
    }
    
    lua_pushlstring(L, (char *)tmpret, SHA1SIZE);
    return 1;
}

static unsigned int num_escape_sql_str(unsigned char *dst, unsigned char *src, size_t size)
{
    unsigned int n =0;
    while (size) {
        /* the highest bit of all the UTF-8 chars
         * is always 1 */
        if ((*src & 0x80) == 0) {
            switch (*src) {
                case '\0':
                case '\b':
                case '\n':
                case '\r':
                case '\t':
                case 26:  /* \z */
                case '\\':
                case '\'':
                case '"':
                    n++;
                    break;
                default:
                    break;
            }
        }
        src++;
        size--;
    }
    return n;
}
static unsigned char*
escape_sql_str(unsigned char *dst, unsigned char *src, size_t size)
{
    
      while (size) {
        if ((*src & 0x80) == 0) {
            switch (*src) {
                case '\0':
                    *dst++ = '\\';
                    *dst++ = '0';
                    break;
                    
                case '\b':
                    *dst++ = '\\';
                    *dst++ = 'b';
                    break;
                    
                case '\n':
                    *dst++ = '\\';
                    *dst++ = 'n';
                    break;
                    
                case '\r':
                    *dst++ = '\\';
                    *dst++ = 'r';
                    break;
                    
                case '\t':
                    *dst++ = '\\';
                    *dst++ = 't';
                    break;
                    
                case 26:
                    *dst++ = '\\';
                    *dst++ = 'z';
                    break;
                    
                case '\\':
                    *dst++ = '\\';
                    *dst++ = '\\';
                    break;
                    
                case '\'':
                    *dst++ = '\\';
                    *dst++ = '\'';
                    break;
                    
                case '"':
                    *dst++ = '\\';
                    *dst++ = '"';
                    break;
                    
                default:
                    *dst++ = *src;
                    break;
            }
        } else {
            *dst++ = *src;
        }
        src++;
        size--;
    } /* while (size) */
    
    return  dst;
}




static int
quote_sql_str(lua_State *L)
{
    size_t                   len, dlen, escape;
    unsigned char                  *p;
    unsigned char                  *src, *dst;
    
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument");
    }
    
    src = (unsigned char *) luaL_checklstring(L, 1, &len);
    
    if (len == 0) {
        dst = (unsigned char *) "''";
        dlen = sizeof("''") - 1;
        lua_pushlstring(L, (char *) dst, dlen);
        return 1;
    }
    
    escape = num_escape_sql_str(NULL, src, len);
    
    dlen = sizeof("''") - 1 + len + escape;
    p = lua_newuserdata(L, dlen);
    
    dst = p;
    
    *p++ = '\'';
    
    if (escape == 0) {
        memcpy(p, src, len);
        p+=len;
    } else {
        p = (unsigned char *) escape_sql_str(p, src, len);
    }
    
    *p++ = '\'';
    
    if (p != dst + dlen) {
        return luaL_error(L, "quote sql string error");
    }
    
    lua_pushlstring(L, (char *) dst, p - dst);
    
    return 1;
}


static struct luaL_Reg mysqlauxlib[] = {
    {"sha1_bin", sha1_bin},
    {"quote_sql_str",quote_sql_str},
    {NULL, NULL}
};


int luaopen_mysqlaux_c (lua_State *L) {
    lua_newtable(L);
    luaL_setfuncs(L, mysqlauxlib, 0);
    return 1;
}

