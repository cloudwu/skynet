/**
*  $Id: md5.c,v 1.2 2008/03/24 20:59:12 mascarenhas Exp $
*  Hash function MD5
*  @author  Marcela Ozorio Suarez, Roberto I.
*/


#include <string.h>

#include "md5.h"


#define WORD 32
#define MASK 0xFFFFFFFF
#if __STDC_VERSION__ >= 199901L
#include <stdint.h>
typedef uint32_t WORD32;
#else
typedef unsigned int WORD32;
#endif


/**
*  md5 hash function.
*  @param message: aribtary string.
*  @param len: message length.
*  @param output: buffer to receive the hash value. Its size must be
*  (at least) HASHSIZE.
*/
void md5 (const char *message, long len, char *output);



/*
** Realiza a rotacao no sentido horario dos bits da variavel 'D' do tipo WORD32.
** Os bits sao deslocados de 'num' posicoes
*/
#define rotate(D, num)  (D<<num) | (D>>(WORD-num))

/*Macros que definem operacoes relizadas pelo algoritmo  md5 */
#define F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~(z))))


/*vetor de numeros utilizados pelo algoritmo md5 para embaralhar bits */
static const WORD32 T[64]={
                     0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
                     0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                     0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
                     0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                     0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
                     0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                     0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
                     0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                     0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
                     0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                     0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
                     0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                     0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
                     0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                     0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
                     0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};


static void word32tobytes (const WORD32 *input, char *output) {
  int j = 0;
  while (j<4*4) {
    WORD32 v = *input++;
    output[j++] = (char)(v & 0xff); v >>= 8;
    output[j++] = (char)(v & 0xff); v >>= 8;
    output[j++] = (char)(v & 0xff); v >>= 8;
    output[j++] = (char)(v & 0xff);
  }
}


static void inic_digest(WORD32 *d) {
  d[0] = 0x67452301;
  d[1] = 0xEFCDAB89;
  d[2] = 0x98BADCFE;
  d[3] = 0x10325476;
}


/*funcao que implemeta os quatro passos principais do algoritmo MD5 */
static void digest(const WORD32 *m, WORD32 *d) {
  int j;
  /*MD5 PASSO1 */
  for (j=0; j<4*4; j+=4) {
    d[0] = d[0]+ F(d[1], d[2], d[3])+ m[j] + T[j];       d[0]=rotate(d[0], 7);
    d[0]+=d[1];
    d[3] = d[3]+ F(d[0], d[1], d[2])+ m[(j)+1] + T[j+1]; d[3]=rotate(d[3], 12);
    d[3]+=d[0];
    d[2] = d[2]+ F(d[3], d[0], d[1])+ m[(j)+2] + T[j+2]; d[2]=rotate(d[2], 17);
    d[2]+=d[3];
    d[1] = d[1]+ F(d[2], d[3], d[0])+ m[(j)+3] + T[j+3]; d[1]=rotate(d[1], 22);
    d[1]+=d[2];
  }
  /*MD5 PASSO2 */
  for (j=0; j<4*4; j+=4) {
    d[0] = d[0]+ G(d[1], d[2], d[3])+ m[(5*j+1)&0x0f] + T[(j-1)+17];
    d[0] = rotate(d[0],5);
    d[0]+=d[1];
    d[3] = d[3]+ G(d[0], d[1], d[2])+ m[((5*(j+1)+1)&0x0f)] + T[(j+0)+17];
    d[3] = rotate(d[3], 9);
    d[3]+=d[0];
    d[2] = d[2]+ G(d[3], d[0], d[1])+ m[((5*(j+2)+1)&0x0f)] + T[(j+1)+17];
    d[2] = rotate(d[2], 14);
    d[2]+=d[3];
    d[1] = d[1]+ G(d[2], d[3], d[0])+ m[((5*(j+3)+1)&0x0f)] + T[(j+2)+17];
    d[1] = rotate(d[1], 20);
    d[1]+=d[2];
  }
  /*MD5 PASSO3 */
  for (j=0; j<4*4; j+=4) {
    d[0] = d[0]+ H(d[1], d[2], d[3])+ m[(3*j+5)&0x0f] + T[(j-1)+33];
    d[0] = rotate(d[0], 4);
    d[0]+=d[1];
    d[3] = d[3]+ H(d[0], d[1], d[2])+ m[(3*(j+1)+5)&0x0f] + T[(j+0)+33];
    d[3] = rotate(d[3], 11);
    d[3]+=d[0];
    d[2] = d[2]+ H(d[3], d[0], d[1])+ m[(3*(j+2)+5)&0x0f] + T[(j+1)+33];
    d[2] = rotate(d[2], 16);
    d[2]+=d[3];
    d[1] = d[1]+ H(d[2], d[3], d[0])+ m[(3*(j+3)+5)&0x0f] + T[(j+2)+33];
    d[1] = rotate(d[1], 23);
    d[1]+=d[2];
  }
  /*MD5 PASSO4 */
  for (j=0; j<4*4; j+=4) {
    d[0] = d[0]+ I(d[1], d[2], d[3])+ m[(7*j)&0x0f] + T[(j-1)+49];
    d[0] = rotate(d[0], 6);
    d[0]+=d[1];
    d[3] = d[3]+ I(d[0], d[1], d[2])+ m[(7*(j+1))&0x0f] + T[(j+0)+49];
    d[3] = rotate(d[3], 10);
    d[3]+=d[0];
    d[2] = d[2]+ I(d[3], d[0], d[1])+ m[(7*(j+2))&0x0f] + T[(j+1)+49];
    d[2] = rotate(d[2], 15);
    d[2]+=d[3];
    d[1] = d[1]+ I(d[2], d[3], d[0])+ m[(7*(j+3))&0x0f] + T[(j+2)+49];
    d[1] = rotate(d[1], 21);
    d[1]+=d[2];
  }
}


static void bytestoword32 (WORD32 *x, const char *pt) {
  int i;
  for (i=0; i<16; i++) {
    int j=i*4;
    x[i] = (((WORD32)(unsigned char)pt[j+3] << 8 |
           (WORD32)(unsigned char)pt[j+2]) << 8 |
           (WORD32)(unsigned char)pt[j+1]) << 8 |
           (WORD32)(unsigned char)pt[j];
  }

}


static void put_length(WORD32 *x, long len) {
  /* in bits! */
  x[14] = (WORD32)((len<<3) & MASK);
  x[15] = (WORD32)(len>>(32-3) & 0x7);
}


/*
** returned status:
*  0 - normal message (full 64 bytes)
*  1 - enough room for 0x80, but not for message length (two 4-byte words)
*  2 - enough room for 0x80 plus message length (at least 9 bytes free)
*/
static int converte (WORD32 *x, const char *pt, int num, int old_status) {
  int new_status = 0;
  char buff[64];
  if (num<64) {
    memcpy(buff, pt, num);  /* to avoid changing original string */
    memset(buff+num, 0, 64-num);
    if (old_status == 0)
      buff[num] = '\200';
    new_status = 1;
    pt = buff;
  }
  bytestoword32(x, pt);
  if (num <= (64 - 9))
    new_status = 2;
  return new_status;
}



void md5 (const char *message, long len, char *output) {
  WORD32 d[4];
  int status = 0;
  long i = 0;
  inic_digest(d);
  while (status != 2) {
    WORD32 d_old[4];
    WORD32 wbuff[16];
    int numbytes = (len-i >= 64) ? 64 : len-i;
    /*salva os valores do vetor digest*/
    d_old[0]=d[0]; d_old[1]=d[1]; d_old[2]=d[2]; d_old[3]=d[3];
    status = converte(wbuff, message+i, numbytes, status);
    if (status == 2) put_length(wbuff, len);
    digest(wbuff, d);
    d[0]+=d_old[0]; d[1]+=d_old[1]; d[2]+=d_old[2]; d[3]+=d_old[3];
    i += numbytes;
  }
  word32tobytes(d, output);
}

