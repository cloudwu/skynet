#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>

#include <iconv.h>

struct word {
    wchar_t *word;
    struct word* next;
};

struct word* words[0xFFFF+1]; 

unsigned int _hash(wchar_t c) {
    unsigned low = c & 0xFFFF;
    unsigned high = (c>>16) & 0xFFFF;
    return high ^ low;
}

wchar_t* utf82wc(char* utf8, size_t len) {
    if(len == 0) return 0;

    iconv_t cd = iconv_open("WCHAR_T", "UTF8");

    size_t outlen = len*4;
    char*  out    = malloc(outlen);

    size_t nconv = 0;
    char* ptr = out;

    nconv = iconv(cd,&utf8, &len, &ptr, &outlen);
    iconv_close(cd);

    if(nconv != 0) {
        printf("string:[%s],len:[%zd] is not a valid utf8 string\n", utf8, len);
        free(out);
        return 0;
    }
    return (wchar_t*)out;

}

size_t wc2utf8(wchar_t* wcstr, char* out, size_t outbuf){
    if(wcstr == NULL) {
        return 0;
    }
    iconv_t cd = iconv_open("UTF8", "WCHAR_T");
    size_t len = (wcslen(wcstr)+1) * sizeof(wchar_t);

    size_t nconv = iconv(cd, (char**)&wcstr, &len, &out, &outbuf);
    iconv_close(cd);
    return nconv;
}

void add_word(wchar_t* word) {
    if(word == 0 || wcslen(word) == 0) {
        return;
    }

    struct word* w = calloc(sizeof(*w), 1);
    w->word = word;

    unsigned h= _hash(word[0]);
    struct word* node = words[h];

    if(node == NULL) {
        words[h] = w;
    } else {
        size_t wlen = wcslen(word);
        struct word* pre  = NULL;
        for(;node != NULL && wcslen(node->word) >= wlen ; pre = node, node = node->next);
        w->next = node;
        if(pre == NULL) {
            words[h] = w;
        } else {
            pre->next = w;
        }
    }
}

struct word* find_word(wchar_t wc) {
    unsigned h = _hash(wc);
    return words[h];
}

int _crab(wchar_t* wmsg) {
    int crabbed = 0;
    if(!wmsg) return crabbed;

    wchar_t wc = wmsg[0];
    while(wc != 0) {
        struct word* w = find_word(wc);
        for(;w != NULL; w=w->next) {
            size_t n = wcslen(w->word);
            if(memcmp(wmsg, w->word, n*sizeof(wchar_t)) == 0) {
                size_t i = 0;
                for(;i<n; ++i) {
                    wmsg[i] = L'*';
                }
                wmsg = wmsg + (n-1);
                crabbed = 1;
                break;
            }
        }
        wc = *(++wmsg);
    }
    return crabbed;
}

char* _filter(const char* msg) {
    char* tmp = (char*)msg;
    size_t slen = strlen(msg) + 1;
    wchar_t* wmsg = utf82wc(tmp, slen);
    char* new_msg = 0;
    if(wmsg) {
        if(_crab(wmsg)) {
            new_msg = malloc(slen);
            wc2utf8(wmsg, new_msg, slen);
        }
        free(wmsg);
    }

    return new_msg;
}

int main(int argc, char **argv ) {
    if(argc != 3) {
        printf("Usage: %s crab_list msg\n", argv[0]);
        return 1;
    }

    FILE* fp = NULL;
    char* utf8= NULL;
    size_t buf = 0;
    size_t len = 0;

    fp = fopen(argv[1], "r");
    if(fp == NULL)
        return 2;

    memset(words, 0, sizeof(words));

    size_t   outbuf = 0;

    while((len = getline(&utf8, &buf, fp)) != -1) {
        // override '\n'
        utf8[len-1] = '\0';

        wchar_t* word = utf82wc(utf8,len);
        add_word(word);
    }
    
    fclose(fp);

    /*
    fp = fopen(argv[2], "r");
    if(fp == NULL)
        return 3;

    while((len = getline(&utf8, &buf, fp)) != -1) {
        char* line = _filter(utf8);
        if(line) {
            printf("%s", line);
            free(line);
        } else {
            printf("%s", utf8);
        }
    }
    */

    char* sentence = argv[2];
    char* new_sentence = _filter(sentence);
    if(new_sentence) {
        printf("crabbed: %s\n", new_sentence);
        free(new_sentence);
    } else {
        printf("no crabbed\n");
    }

    free(utf8);
    return 0;
}

